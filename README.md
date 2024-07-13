# tfr9

The TFR/9 Single Board Computer for Hitachi 6309E or Motorola 6809E CPUs (TFR/901)

## TFR/901 (2024-07-13)

The TFR/901 is the first prototype for the TFR/9 architecture.

It contains slots for a Hitachi 63C09E and a Raspberry Pi Pico, and just
enough multiplexing, latching, and voltage level shifting to connect them.

The hope is that the Pico can provide all the RAM and IO facilities
needed for the 6309/6809 CPU.  It also provides the E and Q clocks,
and there are connections for all inputs and outputs of the CPU chip.

Since this is for prototyping, there are optional header pins for all
interesting signals, and through-hole DIPs are used for all the chips.
There should be ample room to bodge stuff, if mistakes were made.
