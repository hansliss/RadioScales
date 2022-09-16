/*
 * Radio Scale Sender - will send weight measurements when the weight changes more than a
 * given threshold, over LoRa.
 * 
 * This code wasn't written to be shared. Please consider the mess an impetus for learning
 * to read badly commented code. I have no excuse for the lack of error checking, though,
 * except that it wasn't all that important for this application and would have taken a lot
 * more time to add. Maybe later.
 */

#include <heltec.h>
#include <HX711.h>
#include <EEPROM.h>
#include <AESLib.h>

#include "secrets.h"

#undef DEBUG

#ifdef DEBUG
#define SERIALFLAG true
#else
#define SERIALFLAG false
#endif

// LoRa settings
const int LORA_BAND=433E6;
const int LORA_SYNCWORD=0x12;

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN=17;
const int LOADCELL_SCK_PIN=13;

/* 
 *  Generic buffer length. With no real error checking, this has to be large enough for
 * all strings, even when encrypted and Base64-encoded.
 */
 #define BUFLEN 256

/*
 * Config struct to store in EEPROM (since we can't recalibrate the scales on every startup)
 */
struct config_s {
  long offset;
  float scale;
  int delayTime;
  int sendThreshold;
} config;


/*
 * This is used for random seed, for the AES IV. It's updated in loop() by setting
 * one of the bytes to the value from LoRa.Random(), iterating over the array over
 * four loop() cycles. This means the seed consists of four different values of LoRa
 * signal strength, taken at some different intervals. Since this value is just used
 * to seed the PRNG every time we make a new IV, so it's most important that it's not
 * always the same value, the lack of variation of the signal strength shouldn't be 
 * a massive problem. 
 */
unsigned char seedbuf[4];
int seedbufCounter=0;

// The last measurement value from the scales
float lastVal=0;

/*
 * This is for the interrupt-triggered reception of LoRa messages, and will actually
 * contain decoded and decrypted messages when received is true.
 */
char loraReceiveBuf[BUFLEN];
boolean received=false;

/*
 * Encryption stuff
 * 
 * We basically only handle one model here: A string gets encrypted wit AES-128-CBC, and
 * the single-use IV is prepended to the result, which is then Base64-encoded.
 */
AESLib aesLib;

void initAES() {
  aesLib.set_paddingmode((paddingMode)0);
}

/*
 * Encrypt a string,using a newly created IV, prepend the IV and Base64-encode the
 * result.
 */
void encryptText(char *clearText, char *b64Text) {
  static unsigned char ciphertext[2 * BUFLEN + 16];
  static unsigned char iv[16];
  randomSeed(*((unsigned long *)seedbuf));

  for (int i=0; i<16; i++) {
    iv[i] = random(255);
    ciphertext[i] = iv[i];
  }
  int cipherlength = aesLib.encrypt((byte*)clearText, strlen(clearText) + 1, ciphertext + 16, aes_key, sizeof(aes_key), iv);
  base64_encode(b64Text, (char *)ciphertext, cipherlength + 16);
}


/*
 * Base64-decode an encrypted string, decrypt it using the included IV, and leave
 * it in the clearText buffer.
 */
void decryptText(char *b64Text, char *clearText) {
  static unsigned char ciphertext[2 * BUFLEN + 16];
  int ciphlen = base64_decode((char *)ciphertext, b64Text, strlen(b64Text));
  aesLib.decrypt(ciphertext + 16, ciphlen - 16, (byte *)clearText, aes_key, sizeof(aes_key), ciphertext);
}

/*
 * Handle the stored configuration in EEPROM
 */
void saveConfig() {
  unsigned char *configP = (unsigned char *)(&config);
  for (int i = 0; i < sizeof(config); i++) {
    EEPROM.write(i, *configP);
    configP++;
  }
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig() {
  unsigned char *configP = (unsigned char *)(&config);
  boolean first = true;
  for (int i = 0; i < sizeof(config); i++) {
    *configP = EEPROM.read(i);
    if (*configP != 0xFF) {
      first = false;
    }
    configP++;
  }

  // Reasonable values for first-time use
  if (first) {
    config.offset = 0;
    config.scale = 1;
    config.delayTime = 10;
    config.sendThreshold = 1;
    saveConfig();
  }
  if (config.scale == 0) {
    config.scale = 1;
    saveConfig();
  }
#ifdef DEBUG
  Serial.println(String("Config: {") + config.offset + "," + config.scale + "," + config.delayTime + "," + config.sendThreshold +"}");
#endif
}

/*
 * LoRa stuff
 */

void initLoRa() {
  LoRa.setSyncWord(LORA_SYNCWORD);
  LoRa.enableCrc();
  LoRa.onReceive(onReceive);
  LoRa.receive();
}

void sendText(char *text) {
  LoRa.beginPacket();
  LoRa.setTxPower(14,RF_PACONFIG_PASELECT_PABOOST);
  LoRa.print(text);
  LoRa.endPacket();
  LoRa.receive();
}

void sendEncrypted(char *text) {
  static char msgbuf[BUFLEN * 2 + 24];
  encryptText(text, msgbuf);
  sendText(msgbuf);
}

int receiveText(char *buf, int bufsize) {
  int i=0;
  while (i < (bufsize - 1) && LoRa.available()) {
    buf[i++] = LoRa.read();
  }
  while (LoRa.available()) {
    LoRa.read();
  }
  buf[i] = '\0';
  return i;
}

void onReceive(int packetSize) {
  static char inbuf[BUFLEN];
  if (receiveText(inbuf, BUFLEN)) {
    decryptText(inbuf, loraReceiveBuf);
    received = true;
  }
}

/*
 * HX711 load cell/scales stuff
 */

HX711 scale;

void initScales() {
  scale.begin(LOADCELL_DOUT_PIN,LOADCELL_SCK_PIN);
  scale.set_gain(128);
  scale.set_offset(config.offset);
  scale.set_scale(config.scale);  
}

/*
 * Read the current value from the scales, and only send it if the difference from
 * the previous value exceeds the threshold.
 */
void checkScales() {
  float curVal = scale.get_units(10) / 1000;
  static char txtbuf[BUFLEN];
#ifdef DEBUG
  Serial.println(String("Read value ") + curVal);
#endif
  if (abs(curVal - lastVal) > config.sendThreshold) {
#ifdef DEBUG
    Serial.println("Sending...");
#endif
    sprintf(txtbuf, "L:%.2f", curVal);
    sendEncrypted(txtbuf);
    lastVal = curVal;
  }
}

/* Commands:
 * t   (tare)
 * s:scale
 * d:delay
 * T:sendThreshold
 * i   (reinit scale)
 */
void handleCommand(char *cmd) {
  static char responseBuf[BUFLEN];
  boolean changed=false;
  switch(cmd[0]) {
    case 't':
#ifdef DEBUG
      Serial.println("Calibrating...");
#endif
      scale.tare();
      if (config.offset != scale.get_offset()) {
#ifdef DEBUG
        Serial.println(String("Changing offset from ") + config.offset + " to " + scale.get_offset());
#endif
        sprintf(responseBuf, "S:Changing offset from %ld to %ld", config.offset, scale.get_offset());
        sendEncrypted(responseBuf);
        config.offset = scale.get_offset();
        changed = true;
      }
      break;
    case 's':
      if (cmd[1] == ':') {
#ifdef DEBUG
        Serial.println("Scaling...");
#endif
        float calWeight = atof(&(cmd[2])) * 1000;
        scale.set_scale();
        float calReading=scale.get_units(10);
        scale.set_scale(calReading/calWeight);
        if (config.scale != calReading/calWeight) {
#ifdef DEBUG
          Serial.println(String("Changing scale factor from ") + config.scale + " to " + calReading/calWeight);
#endif
          sprintf(responseBuf, "S:Changing scale factor from %.2f to %.2f", config.scale, calReading/calWeight);
          sendEncrypted(responseBuf);
          config.scale = calReading/calWeight;
          changed = true;
        }
      }
      break;
    case 'd':
      if (cmd[1] == ':') {
        int newDelay = atoi(&(cmd[2]));
        if (config.delayTime != newDelay) {
          sprintf(responseBuf, "S:Changing delay from %d to %d", config.delayTime, newDelay);
          sendEncrypted(responseBuf);
          config.delayTime = newDelay;
          changed = true;
        }
      }
      break;
    case 'T':
      if (cmd[1] == ':') {
        int newSendThreshold = atoi(&(cmd[2]));
        if (config.sendThreshold != newSendThreshold) {
          sprintf(responseBuf, "Changing send threshold from %d to %d", config.sendThreshold, newSendThreshold);
          sendEncrypted(responseBuf);
          config.sendThreshold = newSendThreshold;
          changed = true;
        }
      }
      break;
  }
  if (changed) {
    saveConfig();
  }
}

void setup() {
  Heltec.begin(false /*DisplayEnable Enable*/, true /*LoRa Disable*/, SERIALFLAG /*Serial Enable*/, true /*PABOOST Enable*/, LORA_BAND /**/);
  EEPROM.begin(sizeof(config));
  loadConfig();
  initLoRa();
  initScales();
  initAES();
  sendEncrypted("S:Started");
}

void loop() {
  seedbuf[(seedbufCounter++) % 4] = LoRa.random();
  checkScales();
  if (received) {
#ifdef DEBUG
    Serial.println(String("Received: [") + loraReceiveBuf + "]");
#endif
    handleCommand(loraReceiveBuf);
    received = false;
  }
  delay(1000 * config.delayTime);
}
