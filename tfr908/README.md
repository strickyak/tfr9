# tfr9/tfr908/

Wirings direct between the RP2350B-Dev-Board DIP-60
and the Hitachi 63C09E DIP-40.

( see https://github.com/jvanderberg/RP2350B-Dev-Board )

G means GPIO:

| 6309 | RP2350B-Dev-Board |
|------|-------------------|
| D0:7 | G0:7 |
| A0:15 | G32:47 |
| R/W | G31 |
| E | G30 |
| Q | G29 |
| /RESET | G28 |
| /NMI | G27 |
| LIC | G26 |
| AVMA | G22 |
| /IRQ | G21 |
| /FIRQ | G20 |
| /HALT | G11 |
| BUSY | G10 |
| BS | G9 |
| BA | G8 |
| TSC | GND |
| Vcc | VGUS |
| Vss | GND |


## Hint

```
mkdir build
cd build
PICO_SDK_PATH=/home/strick/modoc/coco-shelf/pico-sdk PICOTOOL_FETCH_FROM_GIT_PATH=/home/strick/modoc/coco-shelf/picotool cmake ../demo
make
```
