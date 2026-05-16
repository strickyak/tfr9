# RP2350B-Dev-Board

This is an RP2350B Dev board that breaks out all 48 GPIO pins.

It follows the original footprint of the RP2040 dev board, but adds 10 pins on each side.

It is not pin compatible with the original RP2040, though a version that shares the same pinout, at least for the first 20 pins on each side would be interesting.

Pinout:

```
1	GPIO0	        60	VBUS
2	GPIO1	        59	VSYS
3	GPIO2	        58	GND
4	GPIO3	        57	3v3
5	GPIO4	        56	3v3_EN
6	GPIO5	        55	ADC_VREF
7	GPIO6	        54	GND
8	GPIO7	        53	GPIO47/ADC7
9	GPIO8	        52	GPIO46/ADC6
10	GPIO9	        51	GPIO45/ADC5
11	GND	            50	GPIO44/ADC4
12	GPIO10	        49	GND
13	GPIO11	        48	GPIO43/ADC3
14	GPIO12	        47	GPIO42/ADC2
15	GPIO13	        46	GPIO41/ADC1
16	GPIO14	        45	GPIO40/ADC0
17	GPIO15	        44	GPIO39
18	GPIO16	        43	GPIO38
19	GPIO17	        42	GPIO37
20	GPIO18	        41	GPIO36
21	GPIO19	        40	GPIO35
22	GND	            39	GND
23	GPIO20	        38	GPIO34
24	GPIO21	        37	GPIO33
25	GPIO22	        36	GPIO32
26	GPIO23	        35	GPIO31
27	GPIO24	        34	GPIO30
28	GPIO25	        33	GPIO29
29	GPIO26	        32	GPIO28
30	GPIO27	        31	RUN
```

## Production files for JLCPCB

The production files for JLCPCB are in the 'production' directory.

RP2350_Dev_board.zip is the zipped Gerber files.

bom.csv and positions.csv are the Bill of materials and placements files respectively.

These were produced with the 'Fabrication Toolkit' plugin, which you will need to install if you need to regenerate them.

## Source Designs

Thie design borrows from https://pro.easyeda.com/editor#id=a0a21c97b13d436db5579fc2a8b1625d, which is an RP2350A design. I adapted it for the RP2350B, but it shares most of the schematic, if not the PCB layout.

I also looked at the RP2350B reference designs available here: https://datasheets.raspberrypi.com/rp2350/Minimal-KiCAD.zip
