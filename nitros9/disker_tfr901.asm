; github.com/strickyak/tfr9/nitros9/disker_tfr901.asm: block disk device for TFR/901
;
; assembled by Henry Strickland (github.com/strickyak).
; Original contributions by Henry Strickland are MIT license.
;
; Based on github.com/strickyak/frobio/frob2/drivers/rblemma.asm,
; which was based on code from nitros9/level1/modules/emudsk.asm
; by Alan DeKok and others, with the following notes and license:
;
* EmuDisk floppy disk controller driver
* Edition #1
* 04/18/96 : Written from scratch by Alan DeKok
*                                 aland@sandelman.ocunix.on.ca
*
*  This program is Copyright (C) 1996 by Alan DeKok,
*                  All Rights Reserved.
*  License is given to individuals for personal use only.
*
*  Comments: Ensure that device descriptors mark it as a hard drive


         nam   disker_tfr901.asm
         ttl   Disk Driver for tfr901

         ifp1
         use   defsfile
         endc

PORTAL   equ $FF10       ; data area for commands to Pico
COMMAND  equ $FF1F       ; Command and Status byte to Pico

N.Drives equ 4

         org   0
         rmb   DRVBEG+(DRVMEM*N.Drives) Normal RBF device mem for N Drives
         ; no extra variables
static_storage_size     equ   .
         org   0


tylg     set   Drivr+Objct
atrv     set   ReEnt+rev
rev      set   $01
edition  set   $01

         mod   eom,name,tylg,atrv,start,static_storage_size
         fcb   DIR.+SHARE.+PEXEC.+PWRIT.+PREAD.+EXEC.+UPDAT.
name     fcs   /RBLemma/
         fcb   edition

**************************************************************************
* Init
*
* Entry: Y=Ptr to device descriptor
*        U=Ptr to device mem
*        V.PAGE and V.PORT 24 bit device address
*
* Exit:  CC carry set on error
*        B  error code if any
*
* Actions:
*
*   Set V.NDRV to number of drives supported (2)
*   Set DD.TOT to something non-zero
*   Set V.TRACK to $FF
*   Initialize device control registers?
*
* Default to only one drive supported, there's really no need for more.
* Since MESS now offers second vhd drive, EmuDsk will support it. RG
**************************************************************************

INIT     ldd   #($FF00+N.Drives)  ; 'Invalid' value & # of drives
         stb   V.NDRV,u      ; Tell RBF how many drives
         leax  DRVBEG,u      ; Point to start of drive tables
init2    sta   DD.TOT+2,x    ; Set media size to bogus value $FF0000
         sta   V.TRAK,x      ; Init current track # to bogus value
         leax  DRVMEM,x      ; Advance X to next drive's mem.
         decb
         bne   init2
         clrb
         rts


start
toInit
         bra INIT
         nop
toRead
         bra READ
         nop
toWrite
         bra WRITE
         nop
toGetStat
         clrb
         rts
         nop
toSetStat
         clrb
         rts
         nop
toTerm
         clrb
         rts
         nop

**************************************************************************
* Read
*
* Entry: B:X = LSN
*        Y   = path dsc. ptr
*        U   = Device mem ptr
*
* Exit:  CC carry set on error
*        B  error code if any
*
* Actions:
*  Load A with read command and call TfrSector
*  If error return it in reg B
*  if LSN is not zero use GETSTA to return
*  If LSN is zero copy first DD.SIZ bytes of sector to drive table
*
**************************************************************************


READ     clra                 READ command value=0
         bsr   TfrSector        Get the sector
         bne   reterr         error return if not zero
         tstb                 test msb of LSN
         bne   noerr          if not sector 0, return
         leax  ,x             sets CC.Z bit if lsw of LSN not $0000
         bne   noerr          if not sector zero, return
* Copy LSN0 data to the drive table each time LSN0 is read
         ldx   PD.BUF,y       get ptr to sector buffer
         leau  DRVBEG,u       point to first drive table
         lda   PD.DRV,y       get vhd drive number from descriptor RG
         beq   copy.0         go if first vhd drive
         leau  DRVMEM,u       point to second drive table
       IFNE  H6309
copy.0   ldw   #DD.SIZ        # bytes to copy over
         tfm   x+,u+
       ELSE
copy.0   ldb   #DD.SIZ        # bytes to copy over
copy.1   lda   ,x+            grab from LSN0
         sta   ,u+            save into device static storage
         decb
         bne   copy.1
       ENDC
noerr    clrb
         rts

**************************************************************************
* Write
*
* Entry: B:X = LSN
*        Y   = path dsc. ptr
*        U   = Device mem ptr
*
* Exit:  CC carry set on error
*        B  error code if any
*
* Actions:
*  Load reg A with write command and call get sect
*  Return with error if any in reg B
**************************************************************************

WRITE    lda   #$01           WRITE command = 1
         bsr   TfrSector
         bne   reterr
         clrb
         rts

reterr   tfr    a,b           Move error code to reg B
         coma                 Set the carry flag
         rts

**************************************************************************
* TfrSector
*
* Entry: A = read/write command code (0/1)
*        B,X = LSN to read/write
*        Y = path dsc. ptr
*        U = Device static storage ptr
*
* Exit:  B = Error code, zero if none (also sets Carry)
*        X,Y,U are preserved
*
**************************************************************************

TfrSector
        pshs  u,y,x,cc          ; Save regs x and a
        orcc  #$50              ; disable interrupts throughout a sector operation.

        std PORTAL           ; command {r=0, w=1} and hi byte of Logical Sector Number
        stx PORTAL+2         ; low bytes of Logical Sector Number
        sty PORTAL+4         ; Path Desc
        stu PORTAL+6         ; Device Static Storage
        ldx PD.BUF,y
        stx PORTAL+8         ; sector buffer address
        ldx PD.DEV,y         ;
        stx PORTAL+10        ; Device Table Entry
        ldx V$DESC,x         ; device descriptor
        ldx V.PORT,x         ;
        stx PORTAL+12        ; "Port" number of the "Device"

        sta COMMAND          ; Tell Pico to do the command!
@busy
        ldb COMMAND          ; Status to B.
        beq @busy            ; Zero means NotYet.
        puls cc,x,y,u        ; retain B!

        tstb #1              ; One means OKAY.
        beq @okay
@bad
        coma                 ; Set carry.
        rts
@okay        
        clrb                 ; Clear B and carry.
        rts

        emod
eom     equ   *
