********************************************************************
* Init - NitrOS-9 Configuration module
*
* Edt/Rev  YYYY/MM/DD  Modified by
* Comment
* ------------------------------------------------------------------
* 204      1998/10/12  Boisy G. Pitre
* Original OS-9 L2 Tandy distribution.
*
* 205      1998/10/20  Boisy G. Pitre
* Added CC3IO and Clock sections.
*
* 205r2    1998/10/20  Boisy G. Pitre
* Removed clock information from here.
*
*   1      2003/01/08  Boisy G. Pitre
* Restarted edition number back to 1, removed CMDS/cc3go reference and
* just have cc3go so that in certain cases, cc3go can be in the bootfile,
* and so that ROMmed systems don't have to have a special init module.
*
*          2003/11/05  Robert Gault
* Corrected CC3IO info regards mouse. Changed from fcb to fdb low res/ right
* Corrected OS9Defs to match.
*
*	   2006/07/06	P.Harvey-Smith.
* Conditionally excluded port messages on Dragon Alpha, due to insufficient
* space !
*
* 2024/07/29      Henry Strickland    github.com/strickyak
* Edition 7 for TFR/901

                    nam       Init
                    ttl       NitrOS-9 Configuration module

                    use       defsfile
                    ifgt      Level-1
                    use       cocovtio.d
                    endc

tylg                set       Systm+$00
atrv                set       ReEnt+rev
rev                 set       $00
edition             set       7

*
* Usually, the last two words here would be the module entry
* address and the dynamic data size requirement. Neither value is
* needed for this module so they are pressed into service to show
* MaxMem and PollCnt. For example:
* $0FE0,$0015 means
* MaxMem = $0FE000
* PollCnt = $0015
*
                    mod       eom,name,tylg,atrv,$0FE0,$0015

***** USER MODIFIABLE DEFINITIONS HERE *****

*
* refer to
* "Configuration Module Entry Offsets"
* in os9.d
*
start               equ       *
                    fcb       $27                 entries in device table
                    fdb       DefProg             offset to program to fork
                    ifne      f256
                    fdb       $0000
                    else
                    fdb       DefDev              offset to default disk device
                    endc
                    fdb       DefCons             offset to default console device
                    fdb       DefBoot             offset to boot module name
                    fcb       $01                 write protect flag (?)
                    fcb       Level               OS level
                    fcb       3             OS version
                    fcb       33            OS major revision
                    fcb       33            OS minor revision
                    ifne      H6309
                    fcb       Proc6309+CRCOff     feature byte #1
                    else
                    fcb       CRCOff              feature byte #1
                    endc
                    fcb       $00                 feature byte #2
                    fdb       OSStr
                    fdb       InstStr
                    fcb       0,0,0,0             reserved

                    ifgt      Level-1
* CC3IO section
                    fcb       Monitor             monitor type
                    fcb       0,1                 mouse info, low res right mouse
                    fcb       $1E                 key repeat start constant
                    fcb       $03                 key repeat delay constant
                    endc

name                fcs       "Init"
                    fcb       edition

DefProg             fcs       "SysGo"
DefDev              fcs       "/DD"
DefCons             fcs       "/Term"
DefBoot             fcs       "Boot"

                    ifeq      dalpha
OSStr               equ       *
                    fcc       "NitrOS-9/"
                    ifne      H6309
                    fcc       /6309 /
                    else
                    fcc       /6809 /
                    endc
                    fcc       /Level /
                    fcb       '0+Level
                    fcb       0

InstStr             equ       *
                    fcc       "TFR/901"
                    fcb       0                   null-terminate the name string
                    else
* DragonAlpha
OSStr               equ       *
InstStr             equ       *
                    fcb       0                   null-length string
                    endc                          match IFEQ dalpha

                    emod
eom                 equ       *
                    end
