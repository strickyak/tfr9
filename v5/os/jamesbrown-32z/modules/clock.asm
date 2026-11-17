********************************************************************
* Clock - Level 2 Clock Module for CoCo 2 / CoCo 3 + Centipede 32z
*
* Uses motherboard PIA0 ($FF00-$FF03) 60Hz VSYNC _IRQ (present on BOTH
* CoCo 2 and CoCo 3 motherboards, since the 40-pin cartridge port has no
* _IRQ line) and unconditionally runs DoPoll every tick so polled I/O
* devices (such as sc6850 /Term at $FF88) are serviced 60 times/sec.
* Also disables GIME $FF92 IRQs so a real CoCo 3 does not double-fire.

                    nam       Clock
                    ttl       Clock for CoCo 2/3 Level 2 (Centipede 32z)

TkPerTS             equ       2                   ticks per time slice

                  IFP1
                    use       defsfile
                    use       cocovtio.d
                  ENDC

Edtn                equ       10
Vrsn                equ       1

                    mod       len,name,Systm+Objct,ReEnt+Vrsn,Init,0

name                fcs       "Clock"
                    fcb       Edtn

NewSvc              fcb       F$Time
                    fdb       F.Time-*-2
                    fcb       F$VIRQ
                    fdb       F.VIRQ-*-2
                    fcb       F$Alarm
                    fdb       F.ALARM-*-2
                    fcb       F$STime
                    fdb       F.STime-*-2
                    fcb       $80

*------------------------------------------------------------
* IRQ handler entry point (no stack yet — do not push!)
*
SvcIRQ              lda       >PIA0Base+3         check PIA0 CRB bit 7 (VSYNC flag)
                    bpl       NotClock            not PIA0 VSYNC
                    lda       >PIA0Base+2         read PIA0 Port B data to clear VSYNC latch
                    ldx       <D.VIRQ             schedule SvcVIRQ
                    clr       <D.QIRQ             0 = clock tick IRQ
                    bra       ContIRQ

NotClock            leax      DoPoll,pcr          non-clock IRQ: poll devices
                    lda       #$FF
                    sta       <D.QIRQ
ContIRQ             stx       <D.SvcIRQ
                    jmp       [D.XIRQ]

*------------------------------------------------------------
* SvcVIRQ - re-entered on every 60Hz VSYNC tick with system stack active
*
SvcVIRQ             clra
                    pshs      a
                    ldy       <D.CLTb
                    bra       virqent

virqloop            ldd       Vi.Cnt,x
                    subd      #$0001
                    bne       notzero
                    lda       Vi.Stat,x
                    bmi       doreset
                    lbsr      DelVIRQ
doreset             ora       #$01
                    sta       Vi.Stat,x
                    lda       #$80
                    sta       ,s
                    ldd       Vi.Rst,x
notzero             std       Vi.Cnt,x
virqent             ldx       ,y++
                    bne       virqloop

                    puls      a
* Always poll registered IRQ/polled devices (sc6850 at $FF88 has no _IRQ wire):
                    bsr       DoPoll

KbdCheck            jsr       [>D.AltIRQ]

                    dec       <D.Tick
                    bne       VIRQend
                    lda       #TkPerSec
                    sta       <D.Tick

                    inc       <D.Sec
                    lda       <D.Sec
                    cmpa      #60
                    blo       VIRQend
                    clr       <D.Sec

                    ldx       <D.Clock2           re-sync from Clock2 ($FF72) every minute
                    jsr       $03,x

NoGet               ldd       >WGlobal+G.AlPID
                    ble       VIRQend
                    ldd       >WGlobal+G.AlPckt+3
                    cmpd      <D.Hour
                    bne       VIRQend
                    ldd       >WGlobal+G.AlPckt+1
                    cmpd      <D.Month
                    bne       VIRQend
                    ldb       >WGlobal+G.AlPckt+0
                    cmpb      <D.Year
                    bne       VIRQend
                    ldd       >WGlobal+G.AlPID
                    cmpd      #1
                    beq       checkbel
                    os9       F$Send
                    bra       endalarm
checkbel            ldb       <D.Sec
                    andb      #$F0
                    beq       dobell
endalarm            ldd       #$FFFF
                    std       >WGlobal+G.AlPID
                    bra       VIRQend
dobell              ldx       >WGlobal+G.BelVec
                    beq       VIRQend
                    jsr       ,x
VIRQend             jmp       [>D.Clock]

*------------------------------------------------------------
* Poll all devices in D.Poll until Carry Set
*
DoPoll              pshs      b
                    clrb
DoPollLp            jsr       [>D.Poll]
                    bcs       DoPollDn
                    incb
                    bra       DoPollLp
DoPollDn            tstb
                    puls      b,pc

DoToggle            rts

*------------------------------------------------------------
* F$VIRQ
*
F.VIRQ              pshs      cc
                    orcc      #IntMasks
                    ldy       <D.CLTb
                    ldx       <D.Init
                    ldb       PollCnt,x
                    ldx       R$X,u
                    beq       RemVIRQ
FindVIRQ            ldx       ,y++
                    beq       AddVIRQ
                    decb
                    bne       FindVIRQ
                    puls      cc
                    comb
                    ldb       #E$Poll
                    rts

AddVIRQ             leay      -2,y
                    ldx       R$Y,u
                    stx       ,y
                    ldy       R$D,u
                    sty       ,x
                    bra       virqexit

RemVIRQ             ldx       ,y++
                    beq       virqexit
                    cmpx      R$Y,u
                    bne       RemVIRQ
                    bsr       DelVIRQ
virqexit            puls      cc
                    clrb
                    rts

DelVIRQ             pshs      x,y
DelVLup             ldx       ,y++
                    stx       -4,y
                    bne       DelVLup
                    puls      x,y
                    leay      -2,y
                    rts

*------------------------------------------------------------
* F$Alarm
*
F.ALARM             ldx       #WGlobal+G.AlPckt
                    ldd       R$D,u
                    bne       DoAlarm
                    std       G.AlPID-G.AlPckt,x
                    rts

DoAlarm             tsta
                    bne       SetAlarm
                    cmpd      #1
                    bne       GetAlarm
SetAlarm            std       G.AlPID-G.AlPckt,x
                    ldy       <D.Proc
                    lda       P$Task,y
                    ldb       <D.SysTsk
                    ldx       R$X,u
                    ldu       #WGlobal+G.AlPckt
                    ldy       #5
                    bra       FMove

GetAlarm            cmpd      #2
                    bne       AlarmErr
                    ldd       G.AlPID-G.AlPckt,x
                    std       R$D,u
                    bra       RetTime
AlarmErr            comb
                    ldb       #E$IllArg
                    rts

*------------------------------------------------------------
* F$Time
*
F.Time              ldx       #D.Time
RetTime             ldy       <D.Proc
                    ldb       P$Task,y
                    lda       <D.SysTsk
                    ldu       R$X,u
STime.Mv            ldy       #6
FMove               os9       F$Move
                    rts

*------------------------------------------------------------
* F$STime
*
F.STime             ldx       <D.Proc
                    lda       P$Task,x
                    ldx       R$X,u
                    ldu       #D.Time
                    ldb       <D.SysTsk
                    bsr       STime.Mv
                    lda       #TkPerSec
                    sta       <D.Tick
                    ldx       <D.Clock2
                    jsr       $06,x
                    rts

Clock2              fcs       "Clock2"

*------------------------------------------------------------
* Clock Init - link Clock2, configure PIA0 60Hz VSYNC _IRQ, install handlers
*
Init                ldx       <D.Proc
                    pshs      x
                    ldx       <D.SysPrc
                    stx       <D.Proc

                    leax      <Clock2,pcr
                    lda       #Sbrtn+Objct
                    os9       F$Link

                    puls      x
                    stx       <D.Proc

                    bcc       LinkOk
                    lda       #E$MNF
                    jmp       <D.Crash
LinkOk              sty       <D.Clock2

                    pshs      cc
                    orcc      #IntMasks

* Disable GIME VBORD IRQs so a real CoCo 3 never double-fires alongside PIA0:
                    clr       <D.IRQER
                    clr       >IRQEnR

* Configure motherboard PIA0 ($FF00-$FF03) for 60Hz VSYNC _IRQ on CB1:
                    ldx       #PIA0Base
                    clra
                    sta       1,x                 select DDRA
                    sta       ,x                  Port A all inputs
                    sta       3,x                 select DDRB
                    coma
                    sta       2,x                 Port B all outputs
                    ldd       #$3435              CRA=$34 (HSYNC off), CRB=$35 (VSYNC _IRQ ON!)
                    sta       1,x
                    stb       3,x
                    sta       $23,x               PIA1 CRB=$34 (CART FIRQ off)
                    lda       ,x                  clear any pending PIA0 Port A IRQ
                    lda       2,x                 clear any pending PIA0 VSYNC IRQ

                    ldd       #59*256+TkPerTS
                    std       <D.Sec
                    lda       #TkPerSec
                    sta       <D.Tick

                    stb       <D.TSlice
                    stb       <D.Slice
                    leax      SvcIRQ,pcr
                    stx       <D.IRQ
                    leax      SvcVIRQ,pcr
                    stx       <D.VIRQ
                    leay      NewSvc,pcr
                    os9       F$SSvc
                    ldy       <D.Clock2
                    jsr       ,y                  read initial PC wall-clock time from $FF72
                    puls      cc,pc

                    emod
len                 equ       *
                    end
