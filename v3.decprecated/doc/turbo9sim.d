                    ifne      TURBO9SIM.D-1
TURBO9SIM.D         set       1

********************************************************************
* Turbo9SimDefs - TurbOS System Definitions for the Turbo9 Simulator
*
* This is a high level view of the memory map as setup by TurbOS
*
*     $0000----> ==================================
*               |                                  |
*               |        TurbOS Globals/Stack      |
*               |                                  |
*     $0500---->|==================================|
*               |                                  |
*                 . . . . . . . . . . . . . . . . .
*               |                                  |
*               |   RAM available for allocation   |
*               |         by TurbOS and Apps       |
*               |                                  |
*                 . . . . . . . . . . . . . . . . .
*               |                                  |
*     $FF00---->|==================================|
*               |                I/O               |
*               |            &  Vectors            |
*                ==================================
*
* Edt/Rev  YYYY/MM/DD  Modified by
* Comment
* ------------------------------------------------------------------
*          2023/02/07  Boisy G. Pitre
* Started.

********************************************************************
* Ticks per second
*
TkPerSec            set       60

********************************************************************
*
* TurbOS Section
*

********************************************************************
* Mapped I/O boundaries
MappedIOStart  set       $FF00
MappedIOEnd    set       $FFFF

********************************************************************
* I/O definitions
Term.Out       set       $00      character written to this port appears in terminal
Term.In        set       $01      character read from this port comes from keyboard (when TERMRX.READY == 1)
Reg.Stat       set       $02      status (Read/Write)
Timer.Ready    equ       %00000001 set if timer is ready (write to clear interrupt)
Term.RxReady   equ       %00000010 set if terminal input is ready (write to clear interrupt)
Reg.Ctrl       set       $03      control (Read/Write)
Ctrl.TimrIRQ   set       %00000001 timer interrupt flag
Ctrl.TermIRQ   set       %00000010 terminal receive interrupt flag

********************************************************************
* Boot definitions for TurbOS
*
* These definitions are not strictly for 'Boot', but are for booting the
* system.
*
HW.Page             set       $FF                 device descriptor hardware page
                    endc
