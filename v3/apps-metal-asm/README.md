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

## Electronics kit recommendation

This little kit from ELEGOO is under $15 on Amazon, and has parts that
will help you demonstrate the GPIO pins:

[https://www.amazon.com/dp/B09YRJQRFF](ELEGOO Upgraded Electronics Fun Kit w/Power Supply Module, Jumper Wire, Precision Potentiometer, 830 tie-Points Breadboard Compatible with Arduino, STM32...)

It has a breadboard, jumper wires, buzzer, LEDs, pushbutton switches,
and good sampling of resistors.  (I use the "1K" resistors as my
current-limiting resistors for the LEDs.)
