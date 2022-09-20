/*
 * Radio Scale Receiver - will receive weight measurements over LoRa, and send them on to
 * an http-based receiver.
 * 
 * This code wasn't written to be shared. Please consider the mess an impetus for learning
 * to read badly commented code. I have no excuse for the lack of error checking, though,
 * except that it wasn't all that important for this application and would have taken a lot
 * more time to add. Maybe later.
 */

#include <heltec.h>
#include <AESLib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include "secrets.h"

#undef DEBUG

#ifdef DEBUG
#define SERIALFLAG true
#define DISPLAYFLAG true
#else
#define SERIALFLAG false
#define DISPLAYFLAG true
#endif

#define URLBASE "http://chuao.home.liss.pp.se/saltScale/value.php?v="

// LoRa settings
#define BAND    433E6  //you can set band here directly,e.g. 868E6,915E6
const int LORA_SYNCWORD=0x12;

/* 
 *  Generic buffer length. With no real error checking, this has to be large enough for
 * all strings, even when encrypted and Base64-encoded.
 */
#define BUFLEN 256

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

/*
 * This is for the interrupt-triggered reception of LoRa messages, and will actually
 * contain decoded and decrypted messages when received is true.
 */
char loraReceiveBuf[BUFLEN];
boolean received=false;

/*
 * This is used for the WiFi reconnect logic.
 */
unsigned long previousMillis = 0;
unsigned long interval = 30000;

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
    iv[i] = random(256);
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
 * WiFi
 */

void initWiFi() {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID,WIFI_PPHRASE);
  delay(100);
#ifdef DEBUG
  Heltec.display -> clear();
#endif
  byte count = 0;
  while(WiFi.status() != WL_CONNECTED && count < 15)
  {
    count ++;
    delay(2000);
#ifdef DEBUG
    Heltec.display -> drawString(0, 0, "Connecting...");
    Heltec.display -> display();
#endif
  }

#ifdef DEBUG
  Heltec.display -> clear();
  if(WiFi.status() == WL_CONNECTED)
  {
    Heltec.display -> drawString(0, 0, "Connecting...OK.");
    Heltec.display -> display();
  }
  else
  {
    Heltec.display -> clear();
    Heltec.display -> drawString(0, 0, "Connecting...Failed");
    Heltec.display -> display();
  }
#endif
}

void checkWiFi() {
  unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >=interval)) {
#ifdef DEBUG
    Serial.print(millis());
    Serial.println(": Reconnecting to WiFi...");
#endif
    WiFi.disconnect();
    WiFi.reconnect();
    previousMillis = currentMillis;
  }
}

/*
 * A couple of operations to manipulate the scales. These are currently only available
 * when using a serial connection.
 */
void tare() {
  static char sendbuf[BUFLEN];
  sendEncrypted("t");
}

void changeDelay() {
  static char sendbuf[BUFLEN];
  sendEncrypted("d:2");
}

void calibrate() {
  static char sendbuf[BUFLEN];
#ifdef DEBUG
  Serial.println("Place weight");
  delay(5000);
#endif
  sendEncrypted("s:12.7");
}

void setup() {
  Heltec.begin(DISPLAYFLAG /*DisplayEnable Enable*/, true /*Heltec.LoRa Disable*/, SERIALFLAG /*Serial Enable*/, true /*PABOOST Enable*/, BAND /*long BAND*/);
  initAES();
  initLoRa();
  initWiFi();
}

void loop() {
  checkWiFi();
  // Check if we've received anything
  if (received) {
#ifdef DEBUG
    Serial.println(String("Received: [") + loraReceiveBuf + "]");
#endif
    // We currently only care about "L:" messages, which are sent directly to the web server
    if (loraReceiveBuf[0] == 'L' && loraReceiveBuf[1] == ':') {
      static char url[64];
      WiFiClient client;
      HTTPClient http;
      sprintf(url, "%s%s", URLBASE, &(loraReceiveBuf[2]));
      if (http.begin(client, url)) {
        int httpCode = http.GET();
        http.end();
      }
    }
    received = false;
  }
#ifdef DEBUG
  // Handle a couple of commands to manipulate the scales
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't') {
      tare();
    } else if (c == 'c') {
      calibrate();
    } else if (c == 'd') {
      changeDelay();
    }
  }
#endif
  // Update the seed buffer
  seedbuf[(seedbufCounter++) % 4] = LoRa.random();
  delay(1000);
}
