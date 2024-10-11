********************************************************************
* console_tfr901 Driver
*

                    nam       console_tfr901
                    ttl       console_tfr901 Driver

                    use       defsfile

PORT                equ       $FF50

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
                    lbra      GetStt
                    lbra      SetStt
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
                    beq       Read          ;;; WAS ;;; NotReady
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


GetStt                
                    pshs    a       ; console:GetStt: show num on data bus
                    puls    a

                    ldb     #E$BTyp
                    cmpa    #SS.DSize
                    beq     Bad     ; not a disk.

                    ldb     #E$Unit
                    cmpa    #SS.KySns
                    beq     Bad     ; not a disk.

                    ldb     #E$SLF  ; Other GetStt are "Segment List Full" error.
                    bra Bad

SetStt                
                    pshs    a       ; console:SetStt: show num on data bus
                    puls    a

                    cmpa    #SS.Open
                    beq Okay
                    cmpa    #SS.Close
                    beq Okay
                    cmpa    #SS.ComSt
                    beq Okay

                    ldb     #E$NES  ; other SetStt are "Non-Existing Segment"
Bad
                    coma
                    rts

Okay
                    clra
                    clrb
                    rts




                    emod
ModSize             equ       *
                    end
