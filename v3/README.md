# Hints for building & running /v3/

Right now, this only supports the pico2.

## Prepare

Probably specific to Linux on amd64,
even though not much needs to be fixed
for other platforms.

`make nitros9.done` on the coco-shelf.

`git clone git@github.com:strickyak/tfr9.git` onto the coco-shelf.

BUG: After you `git clone` tfr9 into the 
coco-shelf, you must `mkdir tfr9/v3/generated`

`( cd nitros9 ; NITROS9DIR=$PWD make PORTS=coco1 )`

`( cd nitros9 ; NITROS9DIR=$PWD make PORTS=coco3 )`

## Level 1

To build NitrOS-9 Level 1 for pico2:

`    sh make-level1-905-pico2.sh`

To build and flash the pico2:

`    sh make-level1-905-pico2.sh flash`

To build and flash the pico2 and run tconsole:

`    sh make-level1-905-pico2.sh flash run`

To build and flash the pico2 and run it really 
slowly with tconsole, and copious debugging
into `_log`,

`    sh make-level1-905-pico2.sh slow flash run`

BUG: When you get error 
`Cannot find source file:    ../../generated/level2.rom.h`
then run `make-level2-905-pico2.sh flash run`
and it will make that file.

## Level 2

For Level 2 for pico2, use those same commands,
but change level1 to level2, such as:

`    sh make-level2-905-pico2.sh flash run`

## Turbo9

For Turbo9 OS with the turbo9sim configuration,
change level? to turbo9sim, such as:

`    sh make-turbo9sim-905-pico2.sh flash run`

## Borges

In slow mode, if you have the "borges" bundle
of assembly language listings, you can get nice
joining of tracing output with assembly lines,
done by tconsole.

Download this file
    http://pizga.net/releases/tfr-905h/borges.zip
and unzip it somewhere, say in /tmp/
to create /tmp/borges/.

Then add the `-borges` flag with the directory
name to the `tconsole` command:

    `./tconsole -borges /tmp/borges/ ...`

## BUGS

In "slow" mode, console output gets printed
at least twice.  I don't know why.

Turbo9sim does not join with borges, yet.
