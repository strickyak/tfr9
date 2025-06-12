# tfr9

The TFR/9 Single Board Computer for Hitachi 6309E or Motorola 6809E CPUs (TFR/901)

This represents one approach to mixing retro computers with modern
attachments: The CPU will be genuine retro, a 6809E or 6309E from the
era, but all the other parts are replaced, as much as possible, by one or
more Raspberry Pi Picos, including the RAM and the E/Q clock generation.

## TFR/905h

Finished, tested boards can be ordered (or soon can be ordered) from
https://computerconect.com/collections/coco-hardware

Here is the Hardware Reference Manual:
https://github.com/strickyak/tfr9/wiki/TFR-905-Hardware-Reference

Here is the latest software:
https://github.com/strickyak/tfr9/tree/main/v3

Here are the Kicad design files, and Gerbers and CPL and BOM for JLCPCB.COM:
https://github.com/strickyak/tfr9/tree/main/tfr905h

It works best with a Hitachi HD63C09EP and a Pi Pico 2.

The design goals of the 905h were
 
* Cheap. Two layer, 100mm x 100mm, common parts.

* Works with all Pi Picos {1, 1w, 2, 2w}.

* Uses only 12 GPIO pins, from 0 to 11.

* That leaves plenty of GPIO lines free for audio, video,
interfacing, experimentation ... including all 8 HSTX (high
speed transmit) pins.

* Anti-goals: Maybe not the fastest or the simplest,
but it's fast enough and simple enough.

## TFR/903

This board should work with either 6809E or 6309E, but hereafter we'll
just call it the 6309, but we probably mean a Hitachi 63C09E.  The E is
important -- the CPU must be externally clocked.

We're starting with one Pico in the first prototype, but we might
add more:

* The first Pico is the controller.
  It provides clocks and simulates RAM (hopefully with MMU)
  and a terminal device.  Probably also disk and ethernet.

* A second Pico can generate video output.  This use of a Pico
  has been demonstrated several times, with VGA or DVI output,
  and even with an "HDMI connector".

* A third Pico could be a Pico W which does wifi internet.

The hope is to make very affordable Single Board Computers running
Nitros-9 Level 2 OS, since a 6309 is under $5, a Pi Pico is around $4,
and a Pi Pico W is around $6.

(Another board taking this kind of approach is the Olimex Neo6502 which
sells for 30 Euro.)

## TFR/901 (2024-07-13)

The TFR/901 is the first prototype for the TFR/9 architecture.

It contains slots for a Hitachi 63C09E and one Raspberry Pi Pico, and just
enough multiplexing, latching, and voltage level shifting to connect them.

The hope is that the Pico can provide all the RAM and IO facilities
needed for the 6309/6809 CPU.  It also provides the E and Q clocks,
and there are connections for all inputs and outputs of the CPU chip.

Since this is for prototyping, there are optional header pins for all
interesting signals, and through-hole DIPs are used for all the chips.
There should be ample room to bodge stuff, if mistakes were made.

### First Pico

The first Pi Pico is the controller.  It has the ability to completely
control the 6309 CPU, as if the CPU lives suspended in the matrix.
And yet the 6309, and it alone, will do all the things you expect the
CPU to do.  It is not emulated or simulated, just controlled.

### First Pico Pins

GPIO pins G0 to G15 are a general purpose bidirectional bus.  What is
on this bus will depend on which Y mode is chosen, below.

GPIO pins 16 and 17 are output for the E and Q clock signals to the CPU,
These can be slowed down or sped up at the will of the Pi Pico.

G18, G19, and G20 are outputs to a 74138 3-to-8 decoder, which chooses
one of 8 modes, named Y0 to Y7.

Y0 does nothing.

Y1 maps CPU output Adddress lines A0-A15 to GPIO inputs G0 to G15.

Y2 maps CPU output Data lines D0-D7 to GPIO inputs G0 to G7, and also
various output signals R/W, AVMA, LIC, BUSY, BA, BS...  to GPIO inputs
in the range G8 to G15.

Y3 maps GPIO outputs G0 to G7 to CPU inputs D0-D7.

Y4 latches signals on G8 to G15 to various CPU inputs like RESET, HALT,
NMI, IRQ, FIRQ.   These are "slow" inputs that typically last more than
one CPU cycle, so they are latched, unlike the other signals above.

Y5, Y6, and Y7 will be trigger inputs to the second, third, etc, Pi
Picos, to send them data or receive data from them, over the G0 to G15
general bus.
