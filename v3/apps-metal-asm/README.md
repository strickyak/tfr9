## tfr9/v3/apps-metal-asm

# TFR/905 Engine requirements

These program run on a TFR/905 with these IO ports:

* TurboSim emulation (on ports 0xFF00 to 0xFF03).

    | Address | Purpose |
    |---|---|
    | 0xFF00 | Tx data to tethered PC |
    | 0xFF01 | Rx data from tethered PC |
    | 0xFF02 | Status register |
    | 0xFF03 | Control register |

* tmanager/pico-io.h GPIO on ports 0xFF04 to 0xFF07.

    | Address | Purpose |
    |---|---|
    | 0xFF04 | Write LED with least significant bit |
    | 0xFF05 | Read random bytes |
    | 0xFF06 | GPIO[12:19] Init & Direction (0=input 1=output) |
    | 0xFF07 | GPIO[12:19] Data Read inputs or Write outputs |

## (Linux) PC Commands

First use `make all` or `make all flash` (in the "v3" directory)
to build the programs.  (First unplug the USB, hold down the white
button, and plug in the USB, if you `make ... flash`.)

Then use these commands (in the "v3" directory) to run:

* `make run LOAD=hello.srec`

* `make run LOAD=count-g12-g19.srec`

* `make run LOAD=apps-metal-asm/ditto-g12-g19-even-in-odd-out.srec`

*  etc

Start those commands *before* you plug in the USB.

After about 7 punctuation characters `.:,;`,
the SRecord file will be loaded (you'll see something
like `SSSSS(..)(..)(..)(..)(.)`).  After that,
you can type `3` or `8` to run a fast or a slow TurboSim engine.

* `3` for a fast engine
* `z3` for a really fast (overclocked) engine
* `8` for a slow engine
* `zed8` for an overclocked slow engine with every cycle logged to `_log`.

If your TFR/905 fails with overclocking at `z`, you can try `x` or `c` instead,
which are overclocking a little slower.

## Electronics kit suggestions

Remember electronics can be fiddly and parts can be broken,
so having some redundant parts & different options will be useful.

The sensors and displays may vary a little, depending on from whom and
when you got it.  The components may be substituted and the designs
evolve.  Some may not work at all, due to poor design.  But usually,
the most basic usage will be fairly stable.  You can find lots of
YouTube videos demonstrating success.

### Basic Electronics

This little kit from ELEGOO is under $15 on Amazon, and has parts that
will help you demonstrate the GPIO pins:

[ELEGOO Upgraded Electronics Fun Kit w/Power Supply Module, Jumper Wire, Precision Potentiometer, 830 tie-Points Breadboard Compatible with Arduino, STM32...](https://www.amazon.com/dp/B09YRJQRFF)

It has a breadboard, jumper wires, buzzer, LEDs, pushbutton switches,
and good sampling of resistors.  (I use the "1K" resistors as my
current-limiting resistors for the LEDs.)

### Sensor Kit

This is under $25 on Amazon.  The outputs are a variety of types,
digital, analog, OneWire, SPI, etc, and you can try adapting the
TFR/905 to understand all those.

[16 in 1 Smart Home Sensor Modules Kit for Arduino Raspberry Pi DIY Professional](https://www.amazon.com/dp/B01J9GD3DG)

It's easy to search for these parts to find datasheets and code:

Includes: "1x 20pin 15cm male to female jumper wires, 1x 40pcs 15cm male
to male jumper wires, 1x 10pin 20cm female to female jumper wires, 1x
BMP180 digital air pressure sensor, 1x HC-SR501 infrared sensor module,
1x Two-way relay module,, 1x Flame detection sensor,1x Water level sensor
module, 1x Digital touch sensor module, 1x Vibration sensor module,, 1x
Sound sensor module, 1x Photosensitive sensor module, 1x Passive buzzer
module,, 1x Voltage sensor, 1x MQ2 sensor, 1x MQ5 sensor, 1x MQ7 sensor,1x
DS18B20 temperature sensor, 1x DHT11 temperature humidity sensor."

### Tiny I2C Displays

The popular GME12864 uses the SSD1306 display chip, so search for SSD1306 to find driver code for the Pi Pico.
It interfaces with the I2C "eye squared sea" (or IIC) two-wire protocol (plus GND and +V power wires).
My boards have their I2C address select set to 0x3C.

These are typically $3 to $5.  Here's five for $15:

[5 Pcs 0.96 Inch OLED I2C IIC Display Module 12864 128x64 Pixel SSD1306 Mini Self-Luminous OLED Screen Board Compatible with Arduino Raspberry Pi (White)](https://www.amazon.com/Hosyond-Display-Self-Luminous-Compatible-Raspberry/dp/B09T6SJBV5/ref=pd_sbs_d_sccl_2_1/139-7008077-6655047)

YouTube Demos:

[Raspberry Pi Pico OLED ( SSD1306) display tutorial using CircuitPython : educ8s.tv](https://www.youtube.com/watch?v=c64WG4iJuEo)

[Raspberry Pi Pico: OLED Display (SSD1306) : Tinker Tech Trove](https://www.youtube.com/watch?v=YSqGV6NGWYM)

At first glance, the datasheet looks fairly complicated.
How hard is it to use just the simplest case?
https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
