********************************************************************
* Clock2 - CoCo / Centipede 32z real-time clock driver (e1 map: $FF72-$FF79)
*
* Reads the PC's wall-clock time (sent by tether via T_WALLTIME) from
* RtcPort at $FF72-$FF79 (century, year, month, day, weekday, hour, min, sec).

                    nam       Clock2
                    ttl       Centipede 32z / e1 RTC Driver ($FF72)

                    ifp1
                    use       defsfile
                    endc

tylg                set       Sbrtn+Objct
atrv                set       ReEnt+rev
rev                 set       $00
edition             set       2

RTC.Base            equ       $FF72

                    mod       eom,name,tylg,atrv,JmpTable,RTC.Base

name                fcs       "Clock2"
                    fcb       edition

JmpTable            bra       GetTime             Init
                    nop
                    bra       GetTime             GetTime
                    nop
                    bra       GetTime             SetTime: the PC's clock wins

GetTime             ldx       #RTC.Base
                    ldd       ,x                  get year (CCYY)
                    suba      #20
                    bmi       yr1                 19xx, OK as is
yr0                 addb      #100                20xx adjustment
                    deca
                    bpl       yr0
yr1                 stb       <D.Year             set year (~YY)
                    ldd       2,x                 get date (MMDD)
                    std       <D.Month
                    ifne      Level-1
                    ldd       4,x                 get weekday + hour (wwhh)
                    sta       <D.Daywk
                    else
                    ldb       5,x
                    endc
                    stb       <D.Hour
                    ldd       6,x                 get time (mmss)
                    std       <D.Min
                    rts

                    emod
eom                 equ       *
                    end
