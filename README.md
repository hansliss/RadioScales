# RadioScales
Scales using LoRa to send measurements to a receiver for further sending to a web server

I used two Heltec ESP32-base LoRa/Wifi boards, and a cheap set of 50kg load cells plus a HX711-based A/D board for this.

You need to copy secrets_sample.h to "secrets.h" in each of the subdirectories, and edit them to add your actual secret stuff.

I've included some photos, including a snapshot from a video, explaining how to connect the load cells.

Here's where I found the best information about how to use the load cells: https://robotresearchlab.com/2020/08/28/four-load-cell-with-hx711-programming/

I used the AESLib by Matech Sychra (https://github.com/suculent/thinx-aes-lib) to begin with, but found a couple of issues with it and found it was abandonded, so I made my own fork, hopefully with better bugs: https://github.com/hansliss/thinx-aes-lib

NOTE: I have noticed unexplained restarts, which may have to do with an ESP32 watchdog timer that needs to be fed. The HX711 library has references to this, among others in read_average(byte times), which references an issue and includes a "delay(0)" for this purpose. However, the linked issue has comments to the effect that "delay(0)" will *not* feed the watchdog, so this may require further investigation.
