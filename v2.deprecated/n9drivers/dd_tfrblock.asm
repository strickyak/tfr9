********************************************************************
* DD_tfrblock : Default Disk: LEMMA Block Disk Device 0
*
* Modified from pipe.asm by Henry Strickland (github.com/strickyak)

DRIVE_NUMBER equ 0

         nam   DD(tfrblock)
         ttl   DD(tfrblock) default disk: lemma block disk device descriptor

         ifp1  
         use   defsfile
         endc  

tylg     set   Devic+Objct
atrv     set   ReEnt+rev
rev      set   $00

         mod   eom,name,tylg,atrv,mgrnam,drvnam

         fcb   $87          ; mode byte
         fcb   DRIVE_NUMBER ; M$PORT[3]: $0E: extended controller address
         fdb   DRIVE_NUMBER ;                 physical controller address (match IT.DRV below)
         fcb   iEnd-iBegin  ; M$Opt:  $11: initialization table size

				 ; Initialization Table
iBegin   equ   *
         fcb   DT.RBF       ; M$DTyp: $12: device type
         fcb   DRIVE_NUMBER ; IT.DRV: $13: drive number
         fcb   1            ; IT.STP: $14: step rate
         fcb   TYP.HARD     ; IT.TYP: $15: hard drive
iEnd     equ   *

name     fcs   /DD/
mgrnam   fcs   /RBF/
drvnam   fcs   /TfrBlock/

         emod  
eom      equ   *
         end   
