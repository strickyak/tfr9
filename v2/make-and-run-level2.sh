#!/bin/bash

cd $(dirname $0) || exit 1

cp -vf with-level2.ports tfr9ports.py

cp -vf with-level2.create create-dd.sh

- make    && cp -v  build-tmanager903/tmanager.uf2 /media/strick/RPI-RP2/ && ( cd tconsole ; time go run --tags=level2,coco3 tconsole.go  2>_2 | tee _1 )
