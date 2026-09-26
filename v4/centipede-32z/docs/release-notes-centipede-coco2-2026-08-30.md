# release-notes-centipede-coco2-2026-08-30.md

This has been tested on a coco2 with 16K RAM.

It seems like it should work on coco1's.

It might work on coco3 if you turn off the 64k RAM emulation.

The tether program is tested on Ubuntu 24.04.4 LTS
but usually works on any platform.

On windows,
the tether.*.exe program does not accept `=` in defining
command-line flags .  i.e. instead of `--pc=/tmp/pc`
you must use `--pc /tmp/pc`.

## FLASHING firmware to your centipede.

It is very easy to upload a new firmware.

There are two buttons on the Centipede board, close to
the USB-C connector.  

*   The one closest to the USB-C connector is the RESTART button -- it reboots the firmware.
*   The one farther is the FLASH button.

Hold the FLASH button while the Centipede powers up 
or restarts.   That is, hold the FLASH while you plug in
the USB cable, or if it is already plugged in, while you
use the RESTART button to reboot the firmware.

Then the USB cable will present a USB Disk Drive to your PC.
On Linux it will mount as /media/$USER/RP2340.  ($USER is your user name.)

Allow it to mount, and then copy the file
`centipede.uf2` into that new mount or folder.

```sh
cp -fv centipede.uf2 /media/$USER/RP2340/
```

After it copies (it takes a few seconds), the device will automatically
unmount or "eject" itself.

## Running the tether program

On Linux, the "tether.sh" program is designed to make it easy
to run the tether program.

```
sh tether.sh
```

or

```
./tether.sh
```

This program is more robust than it used to be.  But still, you have
the most success if you HOLD DOWN THE RESTART BUTTON on the Centipede
while you launch the tether program.

If it does not work, look in the file named `_log`.  This is a temporary
file that is created by the tether program for error messages and logging
messages. (It is where the "stderr" path of the tether program is redirected.)

The `_log` file can become quite large if tracing reads and writes are
both turned on.  By default, the `_log` output has a limit of 1GB
to prevent you from accidentally filling your disk.  You can use the
Linux `tail` command to see the final lines of the `_log` which is usually
where an important error message will be.  This shows the last 33 lines:

```
tail -33 _log
```

## RESETTING YOUR TERMINAL

Most of the time the tether command restores your terminal mode correctly.
If it does not (like, if you no longer see echos of what you type),
here is a formula to fix it on Linux.  The `^J` means Control-J:

```
^J reset ^J
```

## Loading the base filesystem

Connect the USB from your PC to your Centipede.
If it is also in the Coco2, hold down BREAK while
it reboots (keep it down 3 seconds, so it is still down
when it finished booting).  If not in Coco2, just hit
the RESTART button on the Centipede.

Then execute this command:
```
sh tether.sh --quick-upload ./basefs.zip
```

You should see lots of spam ending with `QUICK-INJECT SUCCESS`.

(This will be the preferred method of distributing software
for the centipede as a .zip file and uploading it.
More details later.)

## BOOTING THE COCO

Once you have flashed the Centipede with firmware, you are ready to
insert it into your Coco2.  Connect the USB cable to your PC.

Turn on the Coco2.  If your screen does not become black/white monochrome,
either reboot the Coco2 with the reboot switch on the back, or restart
the centipede with the RESTART button.

There are three ways to restart:

* Into the Centipede's "BIOS Menu"
* Into the TCL Shell
* Loading a preset configuration and going directly into BASIC.

If you hold down no key during reboot, you should get the BIOS Menu.

If you hold down BREAK (keep it down for 3 seconds, until the
reboot is over), you get the TCL Shell.

If you hold down a preset key, like 1 or 2 or 3 or 4 or 5 or 6,
it loads a preset configuration and goes directly to BASIC.

Presets:

*   1: Use /fd/f[0123] for floppies, with NO tracing.
*   2: Use /fd/f[0123] for floppies, with WRITE tracing.
*   3: Use /fd/f[0123] for floppies, with READ & WRITE tracing.
*   4: Use /pc/f[0123] for floppies, with NO tracing.
*   5: Use /pc/f[0123] for floppies, with WRITE tracing.
*   6: Use /pc/f[0123] for floppies, with READ & WRITE tracing.

The difference between /fd and /pc is that the /pc directory
is a magic mount for accessing files on your PC, by default
the directory /tmp/pc (if it exists).

/fd is a normal directory on your Centipede's flash storage.

## TCL Shell

The TCL Shell can be used from either your PC or your Coco2.
Keys can be typed on either, and things are displayed on both.
The actual shell runs on the Centipede RP2350B ARM cores.

The most important thing to know is how to get out:  Type `bye`.
That exits the Tcl shell and boots into Coco2 BASIC.

The second thing to know is that the simplest forms of the
simplest UNIX/Linux commands work:

*   `ls` to list directories
*   `pwd` print your current working directory
*   `cd` change your current working directory
*   `cp` copy files
*   `mv` rename files
*   `rm` delete files
*   `mkdir` make directories
*   `rmdir` delete directories
*   `df` how much disk is free?
*   `du` how much disk does a file use
*   `cat` list a text file
*   `hd` hex-dump a file
*   `head -N` Show first N lines of a file
*   `tail -N` Show last N lines of a file
*   `grep` Simple grep command.
*   `wc` lines, words, chars count.
*   `md5` cryptographic hash.

Some others that are different from UNIX:

*   `zip names` directory of .zip archive
*   `zip get` get file from .zip archive
*   `lsort [info comm]` show available commands
*   `edit` a very simple visual text editor
   *      (Ctrl-S or Clear-S to save.)
   *      (Ctrl-Q or Clear-Q to quit without saving.)

The third thing to know is that if you have a directory
named /fd then files in it named f0 f1 f2 and f3 are your
floppy drives, if you boot with this configuration.  So just
copy a floppy image to /fd/f0 and that will be DRIVE 0.

Fourth, you have access to normal Tcl commands as of
Tcl version 6.7.  That's an old, small, easily embeddable
version of Tcl.  It's been enhanced with lots of commands.
TODO: More about this.

Fifth, the REPL has a couple of special features.
One is history: use up, down, left, right to move through
history.  Another is globbing:  If your command has no
characters that are metacharacters to TCL, then your command
will be prefixed by the `fs` command which does two things:
1. Expands glob patterns into filenames, if possible
2. Understands `>filename` to save its output as that filename.
If you don't want those features, add a `;` to the end of your
command.  That's a Tcl metacharacter and it will prevent
using `fs` for those features.

Sixth, you can suffix a filename with `!` if it is
a zip archive, a DECB disk image, or an OS9 disk 
image.  Then the name is treated as a directory.

You can suffix filenames with these also:

* `!hex` -- a read/write view of the file as hex digits
* `!cr` -- views a text file with LF-terminated lines as CR-terminated.
* `!lf` -- views a text file with CR-terminated lines as LF-terminated.

## To get Erico's ugBASIC DEMO running

Create /tmp/pc on your PC:  ` mkdir /tmp/pc `

Copy `OPIL_BR_2026-04-22.dsk` and `OPIL_EN_2026-04-22.dsk`
into it: ` cp -fv OPIL*.dsk /tmp/pc `

Then copy one of those to be /tmp/pc/f0:
` cp /tmp/pc/OPIL_BR_2026-04-22.dsk /tmp/pc/f0`

Then restart, holding down `5` (Use /pc/f[0123] for floppies, with WRITE tracing).

```
DIR
LOAD "LOADER"
RUN
```

Optional: with your web browser, go to `http://localhost:8080/`

## END
