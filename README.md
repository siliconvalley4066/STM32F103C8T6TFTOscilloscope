# STM32F103C8T6TFTOscilloscope
STM32F103C8T6 TFT dual channel oscilloscope with Pulse Generator, DDS Function Generator and Frequency Counter

<img src="STM32F103TFT.jpg">

This displays an oscilloscope screen on a 320x240 TFT LCD.
The settings are controled on the touch screen of the TFT LCD and by the 5 direction switch.
It contains Pulse Generator, DDS Function Generator and Frequency Counter.

Specifications:
<li>Dual input channel</li>
<li>Input voltage range 0 to 3.3V</li>
<li>12 bit ADC 5.14 Msps single channel, 2.57 Msps dual channel</li>
<li>timebase magnification x2, x5 and x10 applying sin(x)/x interpolation</li>
<li>Measures minimum, maximum and average values</li>
<li>Measures frequency and duty cycle</li>
<li>Spectrum FFT analysis</li>
<li>Sampling rate selection</li>
<li>Built in Pulse Generator</li>
<li>Built in DDS Function Generator</li>
<li>Built in Frequency Counter up to 192MHz</li>
<br>
<p>
Develop environment is:<br>
Arduino IDE 1.8.19<br>
STM32F1xx/GD32F1xx boards by stm32duino version 2022.9.26<br>
  (additional URL: http://dan.drown.org/stm32duino/package_STM32duino_index.json )<br>
CPU speed 72MHz<br>
</p>

Libraries:<br>
Adafruit_ILI9341_STM<br>
XPT2046_Touchscreen<br>
arduinoFFT by Enrique Condes 2.0.0<br>

Adafruit_GFX_AS.h seems to be old, and in order to use Adafruit_GFX_AS.h for high-speed rendering, it was necessary to add<br>
#define textsize textsize_x<br>
around line 62 of the Adafruit_GFX_AS.cpp of the STM32 library.<br>

On Arduino_STM32 core you may get a compile error in SPI.h. In this case, edit SPI.h<br>
void transfer(uint8_t * trx_buf, uint32 len) { transfer((const uint8_t *)trx_buf, trx_buf, len); }<br>
to<br>
void transfer(uint8_t * trx_buf, size_t len) { transfer((const uint8_t *)trx_buf, trx_buf, len); }

There are conflicts with the swap macro in Adafruit_GFX_AS.h and the private function swap in arduinoFFT.h.  You have to edit the library source code arduinoFFT.h and place a line at the top as following:<br>
#undef swap<br>

For the frequency counter to work, you have to edit the line 774 of SPI.cpp temporarily as:
<pre>
static void disable_pwm(const stm32_pin_info *i) {
    if (i->timer_device) {
//        timer_set_mode(i->timer_device, i->timer_channel, TIMER_DISABLED);
    }
}
</pre>
Usually SPI.cpp is located in C:\Users\user\AppData\Local\Arduino15\packages\stm32duino\hardware\STM32F1\2022.9.26\libraries\SPI\src <br>
Please note that AppData is a hidden directory.

Schematics:<br>
<img src="STM32TFTOscillo.png">

Description is here, although it is written in Japanese language:
http://harahore.g2.xrea.com/STM32/STM32TFTOscillo.html
