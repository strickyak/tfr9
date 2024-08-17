********************************************************************
* console_tfr901 Driver
*

                    nam       console_tfr901
                    ttl       console_tfr901 Driver

                    use       defsfile

PORT                equ       $FF10

regWbuf             rmb       2                   substitute for regW
RxBufDSz            equ       256-.               default Rx buffer gets remainder of page...
RxBuff              rmb       RxBufDSz            default Rx buffer
MemSize             equ       .

rev                 set       1
edition             set       1

                    mod       ModSize,ModName,Drivr+Objct,ReEnt+rev,ModEntry,MemSize

                    fcb       UPDAT.              access mode(s)

ModName             fcs       "console"
                    fcb       edition


ModEntry            lbra      Init
                    lbra      Read
                    lbra      Write
                    lbra      GStt
                    lbra      SStt
                    lbra      Term

* NOTE:  SCFMan has already cleared all device memory except for V.PAGE and
*        V.PORT.  Zero-default variables are:  CDSigPID, CDSigSig, Wrk.XTyp.
* Entry:
* Y = address of the device descriptor
* U = address of the device memory area
*
* Exit:
* CC = carry set on error
* B  = error code
Init                clrb                          default to no error...
                    rts


*
*
Term                clrb                          default to no error...
                    rts


* Input	U = Address of device static data storage
*	Y = Address of path descriptor module
*
* Output
*	A = Character read
*	CC = carry set on error, clear on none
*	B = error code if CC.C set.

Read
                    lda       >PORT
                    beq       NotReady
                    clrb
                    rts

NotReady             
                    ldb       #E$NotRdy
                    coma                         ; sets Carry
                    coma
                    rts

Write               
                    sta       >PORT
                    clrb                          default to no error...
                    rts


GStt                
                    pshs    a
                    clra
                    clrb
                    puls a,pc


SStt                
                    pshs    a
                    clra
                    clrb
                    puls a,pc

                    emod
ModSize             equ       *
                    end
