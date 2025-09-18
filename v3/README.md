Please see the docs/ directory for more recent info!

# Hints for building & running /v3/

Right now, this only supports the pico2 and pico1,
not the "W" (wireless) versions of the Pi Pico.

Instructions are originally written for Linux on amd64,
but we're trying to get it working on all (at least all
Unixy) platforms.

## Prepare

Use the coco-shelf to make your life easier.

` git clone git@github.com:strickyak/coco-shelf.git `

Then cd into coco-shelf and run `make nitros9.done`

## Borges

TODO -- update this section

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
