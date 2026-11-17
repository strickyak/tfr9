********************************************************************
* Boot - Non-blocking Byte-FIFO EmuDsk Boot for CoCo 2 / CoCo 3 (Centipede 32z)
*
* Loads OS9Boot from EmuDsk ($FF80-$FF86) using non-blocking FIFO Read ($10)
* so Core 1 on Centipede 32z never stalls the continuously-running 6809E E/Q bus.

 ifndef DEVADDR
DEVADDR             equ       $FF80
 endif

 ifndef DISKNUM
DISKNUM             equ       0
 endif

 ifndef LSN24BIT
LSN24BIT            equ       1
 endif

 ifndef FLOPPY
FLOPPY              equ       0
 endif

                    nam       Boot
                    ttl       Centipede 32z Non-Blocking FIFO EmuDsk Boot
tylg                set       Systm+Objct
atrv                set       ReEnt+rev
rev                 set       0

* Static storage used by boot_common
                    org       0
seglist             rmb       2                   pointer to segment list
blockloc            rmb       2                   pointer to memory requested
blockimg            rmb       2                   duplicate of the above
bootsize            rmb       2                   size in bytes
LSN0Ptr             rmb       2                   In memory LSN0 pointer
size                equ       .

                    ifp1
                    use       defsfile
                    endc

                    mod       eom,name,tylg,atrv,start,size
name                fcs       /Boot/

* boot_common calls HWInit, HWRead, and HWTerm.
                    use       boot_common.asm

HWInit
HWTerm              clrb
                    rts

* HWRead - Read a 256 byte sector via non-blocking EmuDsk FIFO ($10)
*   Entry: Y = Device Address ($FF80)
*          B,X = 24-bit LSN
*          blockloc,U = 256-byte destination buffer
*   Exit:  X = ptr to data (MUST equal blockloc,u!)
*          Y, U preserved
*          Carry Set = Error, Carry Clear = OK
HWRead              stb       0,y                 put LSN[23:16]
                    stx       1,y                 put LSN[15:0]
                    ldb       Drive,pcr           load drive number
                    stb       6,y                 put drive ($FF86)
                    ldb       #$10                FIFO Read command (non-blocking)
                    stb       3,y                 trigger Core-0 VFS read ($FF83)
wait@               ldb       3,y                 poll status (bit 7 = busy)
                    bmi       wait@
                    bne       err@                non-zero status = error
                    ldx       blockloc,u          destination buffer
                    clrb                          256 bytes
rdlp@               lda       4,y                 pop byte from EmuDsk FIFO ($FF84)
                    sta       ,x+
                    decb
                    bne       rdlp@
                    ldx       blockloc,u          restore X to start of buffer!
                    andcc     #^Carry             clear carry (success)
                    rts
err@                ldx       blockloc,u
                    orcc      #Carry              set carry (error)
                    rts

* Pad module to exact $1D0 bytes so KRN lands at $F000 ($EE30 + $1D0 = $F000).
Filler              fill      $39,$1D0-3-2-1-*

Address             fdb       DEVADDR
Drive               fcb       DISKNUM
                    emod

eom                 equ       *
                    end
