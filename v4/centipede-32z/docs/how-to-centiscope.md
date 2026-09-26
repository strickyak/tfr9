# centiscope-2026-09-02

The Centiscope logs the machine cycles of your coco, except for reads
from $FFFF (which are usually, but not always, uninteresting idle cycles).

If you have "raw rom images" of the code being executed, and an lwasm
listing of that code, the Centiscope will try to join the assembly
language line, with comments, that goes with each instruction.

## All Cocos?

The Centiscope is thought to work on all Cocos (unlike the first
release of Centipede firmware, which works mainly on Coco2s).

Let me know if you find an exception!

## Flashing

Centiscope is a firmware you can flash onto your Centipede.  Follow the
instructions in the Centipede release notes for how to Flash it.  Use the
file named `centiscope.uf2`:

```
cp -fv centiscope.uf2 /media/$USER/RP*
```

You can always flash the Centipede board again with something else
(up to like 10,000 times).

## For a coco2:

Correct the paths to these files.  The file after --abslists is a lwasm
assembly listing of your BASIC ROMs with the correct absolute addresses.
It may also be comma-separated listing filenames.

The filenames at the end are raw ROM image files.  Since a raw binary has
no header telling where to load it, it must be between the dots in front
of the .rom extension.  If you don't specify these for the centiscope,
it will log 0x00 for all the opcode fetches in the ROM range.

The `_log` file will grow quickly.  You can start and stop the tether
program at will, to capture just the part you want to log.
Use Control-C to kill it.

For TETHER, use the same tether.*.exe from the centipede release.

```
TETHER --no_modules --abslists diskbasic.0x8000.list coco2.0x8000.rom disk11.0xC000.rom 2>&1 | tee _log
```


## For a coco3:

See the coco2 instructions.

```
TETHER --no_modules --abslists coco3.rom.list coco3.0x8000.rom  2>&1 | tee _log
```

## For a coco1:

Try the coco2 path.  Get different rom images and listings if needed.

## Where to get ROMs

Toolshed, in the cocoroms directory:

https://github.com/nitros9project/toolshed

Also try the Color Computer Archive:

https://colorcomputerarchive.com/

## END
