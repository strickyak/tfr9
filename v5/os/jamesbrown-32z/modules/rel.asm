*******************************************************************
* REL - Level 2 Relocation Routine for CoCo 2 / CoCo 3 + Centipede 32z
*
* Adapted from NitrOS-9 level1/coco1/modules/rel.asm (6809 Level 2 CoCo 3):
*   - Keeps SAM R1=0 ($FFD8, 0.895 MHz) instead of $FFD9 so a 16KB CoCo 2
*     MC6809E stays within spec and VDG video/DRAM refresh stays active.
*   - Avoids touching $FF40 SCS space on crash.
*   - Exact $130-byte total image size ($ED00..$EE2F; Boot at $EE30; KRN at $F000).

                    nam       REL
                    ttl       Relocation routine (CoCo 2/3 Centipede 32z)

                    IFP1
                    use       defsfile
                    ENDC

XX.Size             equ       6
Offset              equ       Bt.Start+XX.Size
ScStart             equ       $8008

tylg                set       Systm+Objct
atrv                set       ReEnt+rev
rev                 set       $06
edition             set       6

                    fcc       /OS/                sync bytes ($2600..$2601)
                    bra       (start+XX.Size+*-2) execution start ($2602)
                    fdb       $1205               filler bytes

Begin               mod       eom,name,tylg,atrv,start,size

                    org       0
size                equ       .

name                fcs       /REL/
                    fcb       edition

* GIME setup data stored at $FF90-$FF9F and in direct page ($0090-$009F)
L001F               fcb       $6C                 D.HINIT  MMU, IRQ, Vector page, SCS
                    fcb       $00                 D.TINIT  set MMU map type 0
                    fcb       $00                 D.IRQER  no GIME IRQ (PIA0 drives 60Hz VSYNC)
                    fcb       $00                 D.FRQER  no FIRQ
                    fdb       $0900               D.TIMxx  timer
                    fcb       $00                 D.RESV1  unused
                    fcb       $00                 D.RESV2  unused
                    IFEQ      TkPerSec-50
                    fcb       $0B                 D.VIDMD  50Hz refresh
                    ELSE
                    fcb       $03                 D.VIDMD  60Hz refresh
                    ENDC
                    IFEQ      Width-80
                    fcb       $34                 D.VIDRS  80 column monochrome
                    fcb       $01                 D.BORDR  dark blue border
BOOTLINE            set       11
                    ENDC
                    IFEQ      Width-40
                    fcb       $24                 D.VIDRS  40 column monochrome
                    fcb       $01                 D.BORDR
BOOTLINE            set       13
                    ENDC
                    IFEQ      Width-32
                    fcb       $20                 D.VIDRS  32 column monochrome
                    fcb       $01                 D.BORDR
BOOTLINE            set       13
                    ENDC
                    fcb       $00                 D.RESV3
                    fcb       $00                 D.VOFF2
                    fcb       Bt.Block*4          D.VOFF1
                    fcb       $01                 D.VOFF0
                    fcb       $00                 D.HOFF0

crash               lda       #'*
                    jsr       <D.BtBug
                    tfr       b,a
                    jsr       <D.BtBug
                    clrb
                    fcb       $8C                 skip 2 bytes (cmpx #...)

reset               equ       *
start               ldb       #$FF                negative - complete cold boot
                    clr       >$FFDF              all-RAM mode (SAM TY=1)

start1              orcc      #IntMasks           mask IRQs
                    clr       >PIA0Base+3         disable PIA0 VSYNC IRQ until Clock Init
                    clra
                    tfr       a,dp
                    clr       <D.CBStrt
                    clr       >DAT.Regs+0         map physical RAM block 0 at $0000..$1FFF
                    lds       #$1FFF
                    stb       ,-s                 0=crash, 1=reset, $FF=startup
                    beq       Cont
                    clrb
                    tfr       d,x
L0072               sta       ,x+                 clear direct page $0000..$00FF
                    incb
                    bne       L0072
                    clr       <D.Speed            0.895 MHz normal SAM rate
Cont                clrb
                    stb       >$FFD8              SAM R1=0 (keep 0.895 MHz + CoCo 2 VDG refresh!)
                    leay      <L001F,pcr
                    ldx       #$0090
                    deca                          D = $FF00
                    clrb
L0084               ldu       ,y++
                    stu       d,x                 write GIME $FF90..$FF9F
                    stu       ,x++                and shadow $0090..$009F
                    cmpx      #$00A0
                    blo       L0084
                    ldd       #$013F
                    std       >$FFB0
                    lda       #Bt.Block
                    sta       >$FFA4
                    ldx       #$8000
                    ldd       #Bt.Flag
                    tst       ,s
                    bmi       StoreQ
                    cmpd      ,x
                    beq       MoveTxt
StoreQ              std       ,x
                    ldd       #8
                    std       2,x
                    leax      8,x
                    ldy       #$2000-8
                    ldb       #$20
ClrLoop             stb       ,x+
                    leay      -1,y
                    bne       ClrLoop

MoveTxt             leau      <L0011,pcr
                    bsr       Move1
                    ldb       ,s+
                    bne       L00E2
                    bsr       Move1
L00E0               bra       L00E0

Move1               ldy       ,u++
Move                clra
                    ldb       ,u+
                    tfr       d,x
L00FD               lda       ,u+
                    sta       ,y+
                    leax      -1,x
                    bne       L00FD
                    rts

L0011               fdb       (ScStart+(BOOTLINE*Width)+((Width-L1)/2))
                    fcb       L1
T1                  fcc       /NITROS9 6809/
L1                  equ       *-T1
                    fdb       ScStart+((BOOTLINE+2)*Width)+((Width-LFail)/2)
                    fcb       LFail
TFail               fcc       /FAILED/
LFail               equ       *-TFail

L00E2               tfr       pc,d
                    cmpa      #$26                still executing at $26xx?
                    bne       L0101
                    ldu       #$2600              copy $1200-byte kernel track to $ED00..$FEFF
                    ldx       #$1200
                    ldy       #Bt.Start
                    bsr       L00FD
                    jmp       >Offset+L0101

BtDebug             pshs      cc,d,x
                    orcc      #IntMasks
                    ldb       #Bt.Block
                    stb       >DAT.Regs+0
                    ldx       >$0002
                    sta       ,x+
                    stx       >$0002
                    clr       >DAT.Regs+0
                    puls      cc,d,x,pc

L0101               lda       #$7E                JMP opcode
                    sta       <D.BtBug
                    leax      <BtDebug,pc
                    stx       <D.BtBug+1
                    leau      <R.Crash,pcr
                    ldy       #D.Crash
                    bsr       Move
                    ldx       #$F000              KRN starts at $F000
                    ldd       M$Exec,x
                    jmp       d,x

R.Crash             fcb       $10
L003F               clr       >$FF91
                    jmp       >Offset+crash

                    fcb       $00
                    fdb       $0074
                    nop
                    clr       >$FFDF
                    jmp       >Offset+reset

* Pad module so XX.Size (6) + module body (*) + CRC (3) == $130 (304 bytes).
Filler              fill      $39,$130-XX.Size-3-*

                    emod
eom                 equ       *
                    end
