 nam tfr9log
 ttl tfr9log

HwAddr equ $FF08

* ifp1
 use defsfile
* endc

tylg                set       Prgrm+Objct
atrv                set       ReEnt+Rev
rev                 set       $01
edition             set       1


                    org       0
                    rmb       256
size                equ       .

                    mod       eom,name,tylg,atrv,start,size

name                fcs       /tfr9log/
                    fcb       edition

start:
loop:
 * Send param chars to port and stop after control char.
 ldb ,x+        ; next param char
 stb >HwAddr    ; poke to magic hardware port

 cmpb #' '      ; was it a control char?
 bhs loop       ; repeat if not.

finalize:
 * All done, exit okay.
 clrb          ; good status
 os9 F$Exit    ; not to return.

not_reached:
 bra not_reached  ; just in case it returned.
  
 emod
eom equ *
 end
