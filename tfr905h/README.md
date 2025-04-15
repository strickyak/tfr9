# TFR/905

The goals of this board

*   Cheap 2-layer 100x100mm board.

*   Just one Pi Pico; can be any of

    1) Pi Pico
    1) Pi Pico W
    1) Pi Pico 2
    1) Pi Pico 2 W

*   Free up many GPIO pins: GPIO 12-22 and 26-28 are available.

*   Those include the HSTX (high speed trasmit) pins, for video output.

The multiplexing buffer control is a bit different now.

The TFR/901 had GPIO 3 outputs to a '138 3-to-8 decoder to control the 3-state buffers.

The TFR/903 had GPIO outputs direct to the buffers.

Now we have a 3-bit counter feeding a '138,
so only 2 GPIO lines are required to 1. reset and 2. increment the counter
to control the buffers.

So there is an 8-bit bus on GPIO 0-7.

And only four side outputs: E, Q, CounterReset and CounterIncrement.

NOTICE: tfr905g had one error, fixed on tfr905h (OE of U2).
