 nam hypernop
 ttl HyperNOP for TFR9

* ifp1
 use defsfile
* endc

tylg                set       Prgrm+Objct
atrv                set       ReEnt+Rev
rev                 set       $01
edition             set       1


                    org       0
                    rmb       64               unused
hyper               rmb       64               hyper char sender
                    rmb       128              for the stack
size                equ       .

                    mod       eom,name,tylg,atrv,start,size

name                fcs       /HyperCmd/
                    fcb       edition

start:
 * Build the hyper command sequence: NOP, BRN ___, RTS.
 ldb #$12  ; NOP
 stb hyper,u
 ldb #$21  ; BRN
 stb hyper+1,u
 ldb #$39  ; RTS
 stb hyper+3,u

loop:
 * Send chars to hyper and stop after control char.
 ldb ,x+
 stb hyper+2,u
 jsr hyper,u

 cmpb #' '
 bhs loop

finalize:
 * All done, exit okay.
 clrb          ; good status
 os9 F$Exit    ; not to return.
not_reached:
 bra not_reached  ; just in case it returned.
  
 emod
eom equ *
 end
