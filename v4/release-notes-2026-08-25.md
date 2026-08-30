# tfr9/v4 Release Notes for TFR/911H

This v4 release supports the TFR/911H single board computer.

We could and should port it to the TFR/905H,
but for now, it's only the TFR/911H.

## Prerequisites

These instructions should work on Ubuntu.

Install `pico-sdk` and `picotool` in the superdirectory
of the tfr9 directory.  This is often the coco-shelf or
turbo-shelf directory.  I used version 2.3.0 of these tools.
The /v4/ Makefile can do this all for you:

```
$ cd tfr9/v4
$ make install-pico-sdk-and-picotool-on-shelf
```

You need the Go compiler.  If you don't have the latest,
you can probably edit the go.mod files to reduce the
version required.  Any version 1.20 or later probably works.

## Use the /v4/ directory to build.

```
cd tfr9/v4
make clean
make all
```

The products will be in the `build` directory.

```
/coco-shelf/tfr9/v4$ ls build
listings              tether.linux-amd64.exe  tether.linux-arm-7.exe  tether.mac-arm64.exe  tether.win-amd64.exe
tether.linux-386.exe  tether.linux-arm64.exe  tether.mac-amd64.exe    tether.win-386.exe    tfr911h_v4.uf2
```

## Flash and Run

Inside the /v4/ directory, you can `make flash` to flash 
new firmware into the TFR/911H.  (Hold down the button marked
FLASH while you either Plug In (power up) the board or
RESET the board with the button marked RESET.

```
make flash
```

To connect to the board over the USB cable with the tether program,
you can either `make run` or you can `sh tether.sh`.

```
make run           OR
sh tether.sh
```

If nothing happens, look in the file named `_log` for errors.

## The TCL Shell

In this release, when you restart the TFR911H, you will initially
get a TCL SHELL with the prompt "TCL>".   Hit Enter a time or two
if you don't see it.

This shell is not yet very useful, but soon it will be how you
configure what you want to run on your board.

To exit the shell and start running the 6809/6309, type "bye".

```
TCL> bye
```

This is an old version of Tcl from 1993, release 6.7,
that has been updated for modern gcc and enhanced with
commands for manipulating data on the TFR/911H.

## The Pico Filesystem

While you are in the TCL SHELL, you can manipulate the 11MB
filesystem on the FLASH Storage of the Pico.

THIS IS NOT USEFUL YET, but soon it will be.

Lots of simple UNIX commands work if you use them in
their simplest forms:

```
cd  pwd  ls  cp  mv  rm  mkdir  rmdir  cat  hd
df  du  file  wc  grep
```

There is always a special mount named "/pc".
This is a remote mount from a directory on the PC.
By default it is the directory "/tmp/pc".

To load files onto the Pico filesytem, first copy them
on your PC into the directory "/tmp/pc".  Then boot the
TFR/911h and use the TCL shell to copy files from "/pc" to
the place you want in the Pico filesystem.

## Turbo9 OS with Basic09

AT THE MOMENT, you don't have any choice what OS to run,
unless you hack it yourself.    You get Turbo9 OS with
a Basic09 command so you can experiment and do stuff.

We'll try and get more capabilities Really Soon Now.

If you type "MDIR" you can see the other commands
available to you.  This version of Turbo9 OS has no
disk device, so you cannot save or load files in it.

## Easiest way to hack your own OS9 onto TFR/911H

The file `v4/tfr911h/turbo9os.rom.h` contains OS9
modules.  (Notice it begins with $87 $CD).
The kernel MUST be first, because a simple heuristic
is used to find the kernel entry for the 6809 RESET vector.
Rewrite that file with the modules you want to boot.

Basic09 gets added on by this line:

```
/coco-shelf/tfr9/v4$ grep RomList tfr911h/v4_tfr911h_main.cpp
                    RomList<Turbo9os_Rom, Basic09_Rom>>,
/coco-shelf/tfr9/v4$
```

Notice since Kernel is first in Turbo9os_Rom,
it will be first in the RomList, as it must.
