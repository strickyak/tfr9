********************************************************************
* EmuDsk - Non-blocking Byte-FIFO Virtual Disk Driver ($FF80-$FF86)
*
* Uses EmuDsk FIFO commands ($10 = Read, $11 = Write Reset, $12 = Write Commit)
* while polling bit 7 of $FF83 so Centipede 32z Core 1 never stalls the 6809E
* E/Q bus during USB VFS sector I/O, and works identically on both CoCo 2
* (`gerbil_gime`) and real CoCo 3 motherboard RAM (`gerbil_io`).

LSN                 equ       $FF80               24-bit logical sector number
command             equ       $FF83               command (W) / status (R, bit 7 = busy)
buffer              equ       $FF84               FIFO read ($FF84) / FIFO write ($FF85)
vhdnum              equ       $FF86               drive select (0..MaxVhd-1)

 ifndef MaxVhd
MaxVhd              equ       2
 endif

                    nam       EmuDsk
                    ttl       Non-blocking FIFO EmuDsk RBF driver

                    ifp1
                    use       defsfile
                    endc

tylg                set       Drivr+Objct
atrv                set       ReEnt+rev
rev                 set       $03

                    mod       eom,name,tylg,atrv,start,size

                    org       0
                    rmb       DRVBEG+(DRVMEM*MaxVhd)
prevdr              rmb       1
residual            rmb       30
size                equ       (.+255)&$FF00

                    fcb       $FF                 driver permissions
name                fcs       /EmuDsk/
                    fcb       7                   edition

INIT                ldd       #$FF00+MaxVhd
                    stb       V.NDRV,u
                    leax      DRVBEG,u
init2               sta       DD.TOT+2,x
                    sta       V.TRAK,x
                    leax      DRVMEM,x
                    decb
                    bne       init2
                    stb       prevdr,u
                    stb       >vhdnum

TERM
GETSTA              clrb
                    rts

start               bra       INIT
                    nop
                    bra       READ
                    nop
                    bra       WRITE
                    nop
                    bra       GETSTA
                    nop
                    lbra      SETSTA
                    bra       TERM
                    nop

READ                clra                          0 = Read
                    bsr       GetSect
                    bne       reterr
                    tstb                          MSB of LSN == 0?
                    bne       noerr
                    leax      ,x                  LSW of LSN == 0?
                    bne       noerr
* Copy LSN0 DD header into RBF drive table
                    ldx       PD.BUF,y
                    leau      DRVBEG,u
                    lda       PD.DRV,y
                    ldb       #DRVMEM
                    mul
                    leau      d,u
                    ldb       #DD.SIZ
copy.1              lda       ,x+
                    sta       ,u+
                    decb
                    bne       copy.1

noerr               clrb
                    rts

WRITE               lda       #$01                1 = Write
                    bsr       GetSect
                    bne       reterr
                    clrb
                    rts

reterr              tfr       a,b
                    coma                          set Carry
                    rts

* GetSect
*   Entry: A = 0 (read) or 1 (write)
*          B,X = 24-bit LSN
*          Y = path descriptor ptr
*          U = device static storage ptr
*   Exit:  A = OS-9 error code (0 = OK; CC.Z reflects A)
*          B, X, Y, U preserved
GetSect             pshs      b,x                 save LSN (B:X)
                    pshs      a                   save cmd (0=read, 1=write)
                    lda       PD.DRV,y
                    cmpa      #MaxVhd
                    bhs       DriveErr
                    cmpa      prevdr,u
                    beq       gs.1
                    sta       >vhdnum
                    sta       prevdr,u
gs.1                stb       >LSN
                    stx       >LSN+1
                    ldx       PD.BUF,y
                    puls      a                   pop cmd (0=read, 1=write)
                    tsta                          PULS does not set CC on 6809!
                    bne       gs.write

* Non-blocking FIFO Read ($10):
                    lda       #$10
                    sta       >command
gs.rwait            lda       >command            bit 7 = busy
                    bmi       gs.rwait
                    bne       gs.err
                    clrb                          256 bytes
gs.rloop            lda       >buffer             pop FIFO byte from $FF84
                    sta       ,x+
                    decb
                    bne       gs.rloop
                    puls      b,x                 restore LSN
                    clra                          A = 0, CC.Z = 1
                    rts

* Non-blocking FIFO Write ($11 reset, 256 bytes -> $FF85, $12 commit):
gs.write            lda       #$11
                    sta       >command
                    clrb                          256 bytes
gs.wloop            lda       ,x+
                    sta       >buffer+1           push FIFO byte to $FF85
                    decb
                    bne       gs.wloop
                    lda       #$12
                    sta       >command
gs.wwait            lda       >command            bit 7 = busy
                    bmi       gs.wwait
                    bne       gs.err
                    puls      b,x                 restore LSN
                    clra                          A = 0, CC.Z = 1
                    rts

DriveErr            puls      a                   discard cmd
                    puls      b,x                 restore LSN
                    bra       NotRdy

gs.err              puls      b,x                 restore LSN; A holds emulator error
FixErr              cmpa      #02
                    beq       NotRdy
                    cmpa      #255
                    beq       NotRdy
                    cmpa      #5
                    beq       WP
                    cmpa      #6
                    beq       CRC
                    cmpa      #12
                    beq       CRC
Seek                lda       #E$Seek
                    rts
NotRdy              lda       #E$NotRdy
                    rts
WP                  lda       #E$WP
                    rts
CRC                 lda       #E$CRC
                    rts

SETSTA              ldx       PD.RGS,y
                    ldb       R$B,x
                    cmpb      #SS.WTrk
                    beq       format
                    cmpb      #SS.Reset
                    beq       format
                    cmpb      #SS.SQD
                    beq       park
                    comb
                    ldb       #E$UnkSvc
                    rts

park                lda       PD.DRV,y
                    cmpa      #MaxVhd
                    bhs       format
                    sta       >vhdnum
                    ldb       #$02
                    stb       >command
                    clr       >vhdnum
                    clr       prevdr,u
format              clrb
                    rts

                    emod
eom                 equ       *
                    end
