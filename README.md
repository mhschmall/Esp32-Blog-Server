# Esp32-Blog-Server
Simple blogging platform that runs on an ESP32. Currently designed for a Waveshare ESP32-S3-LCD-1.47, it could run on any ESP32 with a few changes, including the little XIAO SEEED ESP32 S3 Sense which is about the size of a postage stamp. 

I was introduced to this Waveshare product via another project Im sorta contributing to (ESP32 powered offline media server). These boards are pretty feature packed and I definitely like the whole USB stick aspect to using and powering them.

If you're thinking that this is a lot like that Blog on a Wii project, you would be correct. I was definitely inspired by that project, but the final push was google's AI trying to convince me that an ESP32 could not host blogging software. 

This project is live on the internet at https://blogonesp32.duckdns.org/

-----------------

In the esp32blog.ino file:

Please refer to the settings_preview.png file for how to configure the arduino IDE for deploying to a Waveshare ESP32-S3.

Set the SSID and password for the access point you are connecting to. No connecting to a network behind a captive portal at this time.

Set the SD* pins for the device you want to use. Currently set for Waveshare ESP32-S3

Set an admin account name and unique admin password. 

Set the correct time parameters, or your posting dates/times will be wrong. 

Currently index.html only shows five blog entries at a time. This can be changed by setting ENTRIES_PER_PAGE to a new value. Admin page is fixed at 5 entries per page.

On boot, watch the serial output, it will tell you what IP your server is connected as. 

------------------

The footprint for this is pretty small, just 2 html files and a json file, so you don't need a huge sd card for storage. Just copy the items in SD_CARD_LAYOUT to your fat32 formatted SD card. 

Before you deploy, you will need to edit the index.html header to include your title, slogan, and other meta data, otherwise you're just advertising me. :)

Visiting http://your.ip/ will bring up the public blog


----------------------

visiting http://your.ip/admin will bring up the admin page. 

When crafting a new post, you can use html markup to add links, pull pics in, etc. You just can't put quotes around any of it or the editing buttons get wierd. 

I hugely recommend downloading your entries.json file after each new post. This way if you have problems due to json formating, you can hand edit the json file (as well as having a backup) and then reupload it using the import button.

If you need to power off the server, I recommend using the OFF button on the admin page. NOTE: It does not disable power to the device, but it puts the esp32 into deep sleep mode, which is as close as we can get to powering off via software. Although rare, I have incurred SD card corruption by just removing similar units from power. 

---------------

Notes about SSL: While technically possible, deploying SSL on the ESP32 requires the use of special versions of the web serving packages. While there is nothing wrong with that, you still have to hunt them down, manually install them and then hope it works. Then there's cooking the certs and private keys to work with said packages. Its a lot of work and when it does work, your program is using more memeory to handle the SSL overhead. 

What most people do (including me) is to leave the device working on http over port 80 and deploy a reverse proxy to handle the SSL traffic over port 443. I used Caddy, which took literally minutes to install and set up, took my new certs and keys (no cooking!) and I was up and running. 

