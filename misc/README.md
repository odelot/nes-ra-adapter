# Miscellaneous

Here you'll find various files and tools to assist with using or developing for the adapter.

## Web App

This web app replicates the behavior of the adapter's tiny LCD screen, while displaying a bit more information — such as active challenges and achievement progress — similar to how it appears in emulators.

It connects to the ESP32 via WebSocket, enabling real-time communication. As soon as the ESP32 displays information on the physical LCD, it simultaneously sends the same data to any connected client through WebSocket.

A minified version of this web app can be found in the `data` folder of the `nes-esp-firmware` project to save flash space. The ESP32 serves the page using mDNS at:
http://nes-ra-adapter.local

Please note that it runs over HTTP (not HTTPS) because the firmware doesn't have enough free RAM to support SSL. This limitation also affects some HTML5 features — such as push notifications and the ability to keep the screen always on. If you're using a browser, you'll need to manually adjust your device's display settings to prevent it from turning off.

I’ve also started developing an Android app to overcome most of these HTTP limitations, but it’s not ready for release just yet.

## TFT_eSPI_config 

There is a config This file is required only if you plan to build the ESP32 firmware yourself. It configures the TFT screen for the ESP32 C3 Super Mini and must be copied into the TFT_eSPI library folder. 

There is also a fix for a bug to the latest version of TFT_eSPI that avoid ESP32 to boot loop. You can overwrite the folder `Processor` inside TFT_eSPI library folder. This [issue](https://github.com/Bodmer/TFT_eSPI/issues/3284) reports about this problem.

## crc-md5-mapper

We use a mapping system that links CRC values (calculated from a portion of the cartridge ROM) to their corresponding MD5 hashes, extracted from a ROM set. The [games.txt](https://github.com/odelot/nes-ra-adapter/blob/main/nes-esp-firmware/data/games.txt) file, located in `/nes-esp-firmware/data`, contains a precomputed version of this mapping, generated using this Python script and the GoodNES ROM set.

This can be useful if you want to understand how we compute MD5 hashes (RA Hashes) or if you need to add new entries to the map, such as homebrew games. For example, I personally added Micro Mages, which isn't included in the GoodNES ROM set.

## shrink-lambda-function

The list of achievements retrieved from the RA API is designed for emulators, not microcontrollers with limited RAM. The Raspberry Pi Pico has only 32KB available for handling HTTP responses, while the ESP32 employs several optimizations to reduce response size. Currently, ESP32 can receive responses of up to 61,000 bytes — just enough to support Super Mario Bros. 3 — before shrinking the data to send to the Pico.

However, if you want to play a game with a large number of achievements, leaderboards, and other data, and its response exceeds 61,000 bytes, you can deploy this Node.js Lambda function on AWS with an API Gateway in front of it. This setup acts as a proxy, reducing the response size to 32KB by removing unnecessary fields—or, in extreme cases, trimming some achievements.

If you choose this approach (which I used extensively for testing), be sure to modify `nes-esp-firmware.ino` to enable it and set the Lambda URL. You'll also need to build and manually upload the modified firmware to the ESP32.

```
#define ENABLE_SHRINK_LAMBDA 1 // 0 - disable / 1 - enable
#define SHRINK_LAMBDA_URL "https://xxxxxxxxxx.execute-api.us-east-1.amazonaws.com/default/NES_RA_ADAPTER?" // your url
```
