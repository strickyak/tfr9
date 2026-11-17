* dwread_becker_to.asm -- 6809 Becker port DWRead with ~1.75s safety timeout
* Prevents hanging with IRQs masked if FujiNet is unplugged or unresponsive.
                    ifndef    BECKBASE
BECKBASE            equ       $FF41
                    endc
DWRead              clra                          ; C=0, Z=1
                    pshs      cc,x,u              ; save CC (Z=1, C=0), buf start X, U
                    leau      ,x                  ; U = destination buffer
                    ldx       #$0000              ; X = running 16-bit checksum
                    ifeq      NOINTMASK
                    orcc      #IntMasks
                    endc
dw.next             pshs      x                   ; save checksum
                    lda       #2                  ; outer loop (~1.75s at 0.895 MHz)
dw.outer            ldx       #$0000              ; 65536 inner polls
dw.poll             ldb       >BECKBASE
                    bitb      #$02
                    bne       dw.got
                    leax      -1,x
                    bne       dw.poll
                    deca
                    bne       dw.outer
* Timeout: return C=0, Z=0
                    puls      x
                    lda       ,s
                    anda      #^$05               ; clear Z and C
                    sta       ,s
                    tfr       x,y
                    puls      cc,x,u,pc
dw.got              puls      x
                    ldb       >BECKBASE+1
                    stb       ,u+
                    abx
                    leay      -1,y
                    bne       dw.next
                    tfr       x,y                 ; return checksum in Y (CC has C=0, Z=1)
                    puls      cc,x,u,pc
