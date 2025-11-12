# Digital Counter for MTG or any other TCG that could use a counter.

This project uses the ESP32-C6 board from Waveshare and a 1.43 inch AMOLED screen.
It has two functions. One for a life counter and one for a +X/+X counter.
Brightness can be adjusted as well.

## Products Needed
1. [ESP32-C6 with screen](https://www.amazon.com/dp/B0FJS3V5ZS)

   I like this one because it comes with a speaker you don't need, but you an cut off the wires to reuse the JST connector of the right size for the battery. The battery comes with a JST connector that is too big. That way you don't have to buy extra stuff.
2. [Battery](https://www.amazon.com/dp/B0C2PQND8H)

   You will need to splice the cables from this battery to a JST connector that is 1.25mm. The battery comes with 2mm JST connector.
3. 3D printed case ([file found in this repo](https://github.com/airyimbin/mtg_digital_counter/tree/main/3D%20Files))

## Flash Instructions
1. Install ESP-IDF for Visual Studio Code
https://www.waveshare.com/wiki/Install_Espressif_IDF_Plugin_Tutorial
2. Clone this repository
3. Use these instructions for setting up Visual Studio Code
https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.43#Use_the_IDF_Demos

Make sure to select ESP32-C6 at the bottom instead of ESP32-S3 like the instructions say.
4. Plug ESP32-C6 in to computer
5. Click the build button (small fire icon on the bottom row)

## Putting it together
1. Unscrew the bottom cover for the ESP32-C6.
2. Splice the cables from the unused speaker to the battery. Be sure to leave enough cable when cutting from either.
   *Warning: don't let the cables touch on the battery. It will short.*
4. Use some electric tape or heat shrink to cover the splice.
5. Plug battery into ESP32-C6.
6. Use the screws from taking off the cover to screw 3D printed case to the board.
7. There should be enough room for the 802535 battery to fit vertically.

<hr>

Enjoy. Code is built from demo code Waveshare uses, so there is slight jank. But all unnecessary functions like gyro, accelerometer, wifi, and ble should be disabled. 

I suck at coding, so I could be wrong.
