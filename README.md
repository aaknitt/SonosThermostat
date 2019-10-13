# SonosThermostat

ESP8266 Arduino firmware for the project described here:

http://www.radioetcetera.site/in-floor-heat-hacking/

Update the .ino with your WiFi information.

Once the .ino has been flashed to the ESP8266, future updates can be done via OTA and SPIFFS can be accessed via FTP

HTML and .txt files get stored on the ESP8266 SPIFFS.  You can put them there as follows:

Once you can ping the device on your network, setup a plain (insecure) FTP connection to the IP with the username:password as esp8266:esp8266
Copy the raw data from this repo for the two files; index.html and schedules.txt and paste the code using NP++ or something similar to avoid any formatting issues.
Copy these across to the device using FTP.

Reboot the device for the webpage to show up.
