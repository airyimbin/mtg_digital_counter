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

## Build Instructions
### 1. Install ESP-IDF for Visual Studio Code
https://www.waveshare.com/wiki/Install_Espressif_IDF_Plugin_Tutorial
### 2. Clone this repository
### 3. Use these instructions for setting up Visual Studio Code
https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.43#Use_the_IDF_Demos

Make sure to select ESP32-C6 at the bottom instead of ESP32-S3 like the instructions say.
### 4. Plug ESP32-C6 in to computer
### 5. Click the build button (small fire icon on the bottom row)
