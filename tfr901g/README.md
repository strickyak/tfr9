# CoPiCo PCB KiCad Files

The CoPiCo is a cartridge for the Tandy Color Computer that adds WiFi to and
serves as a boot ROM for the system. It is based on the Raspberry Pi Pico W
microcontroller dev board, which includes onboard WiFi.

![copico_rev0.2_front](assets/copico_rev0.2_front.png)

![copico_rev0.2_back](assets/copico_rev0.2_back.png)

![copico_rev0.2_schematic](assets/copico_rev0.2_schematic.png)
[copico_rev0.2_schematic_v0.2.9.pdf](assets/copico_rev0.2_schematic_v0.2.9.pdf)

## WiFi Connectivity

The firmware (see 'copico-firmware' repo) will behave more like a SPI WiFi
module or the WizNet chip (or perhaps both, depending on mode). It will, of
course, also allow listing and connecting to the desired WiFi network.

## ROM Emulation

CoPiCo will also support boot ROM emulation (initially just for a 1 KB ROM due
to limitations of first revision of the CoPiCo PCB). Henry Strickland
(@strickyak) plans to use this feature to load a version of his
"axiom" network boot ROM (see
https://github.com/strickyak/frobio/tree/main/frob3/booting).

## Other Future Capabilities

Future capabilities for the CoPiCo may include:
- VGA or HDMI output based on the captured contents of video RAM
- Logic capture / bus trace
- Remote debug, with breakpoints and watchpoints

## Stay Tuned to This Channel

Please star this repo and/or watch this space for updates as development
continues!

Thank you for being a part of the Retrocomputing community!


Thomas Shanks (copico@tshanks.org)    
