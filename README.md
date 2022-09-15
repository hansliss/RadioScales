# RadioScales
Scales using LoRa to send measurements to a receiver for further sending to a web server

I used two Heltec ESP32-base LoRa/Wifi boards, and a cheap set of 50kg load cells plus a HX711-based A/D board for this.

You need to copy secrets_sample.h to "secrets.h" in each of the subdirectories, and edit them to add your actual secret stuff.

I've included some photos, including a snapshot from a video, explaining how to connect the load cells.

Here's where I found the best information about how to use the load cells: https://robotresearchlab.com/2020/08/28/four-load-cell-with-hx711-programming/
