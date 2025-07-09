set DEFAULT_CMDS_LEVEL1 {
    asm             attr            backup                          bawk
    binex           build           calldbg         cmp             cobbler
    copy            cputype         date            dcheck          debug
    ded             deiniz          del             deldir          devs
    dir             dirsort         disasm          display         dmode
    dsave           dump                            echo            edit
    error           exbin           format          free
    grep            grfdrv          help                            ident
                    iniz                            irqs            link
    list            load            login           makdir
    mdir            megaread        merge           mfree           minted
    more            mpi             os9gen          padrom          park
    printerr        procs           prompt          pwd             pxd
    rename                          save            setime          shell_21
    shellplus       sleep                           tee
                    touch           tsmon           tuneport        unlink
    verify          xmode
    tmode=xmode,TMODE=1
}

set BASIC09_CMDS_LEVEL1 {
    basic09 runb gfx inkey syscall
}

set LINKED_CMDS_LEVEL1 {
    dw inetd telnet httpd
    grep megaread
}

set DEFAULT_SYS_LEVEL1 {
    errors
}

proc Log {args} {
    puts stderr "LOG: $args"
}

proc Range {n} {
    set z {}
    for {set i 0} {$i < $n} {incr i} {
        lappend z $i
    }
    return $z
}
# Get var value from closest scope it is defined in.
proc Get {varname} {
    set level [info level]
    for {set lev 1} {$lev <= $level} {incr lev} {
        upvar $lev $varname var
        if [info exists var] {
            return $var
        }
    }
    error "cannot Get variable from any scope: `$varname`"
}

proc Platform {name body} {
    set ::dirpath ../../../nitros9
    set ::withpath {}

    set ::Platforms($name) 1
    set ::Platform $name
    set ::PlatformProducts($name) {}
    eval $body
}
proc Using {dirname} {

    if {$dirname == "-reset-"} {
        set ::withpath {}
    } else {
        lappend ::withpath $::dirpath/$dirname
    }


}
proc With {dirname body} {
    set ::dirpath [Get dirpath]
    append ::dirpath /$dirname
    set ::withpath [Get withpath]
    lappend ::withpath $::dirpath
    eval $body
}

proc OncePerLine {list} {
    set z "\n"
    foreach it $list {
        append z "\t$it\n"
    }
    return $z
}

proc InitMakefile {} {
    set ::Makefile {}
}

proc LogGlobalVars {{pattern *}} {
    Log "{"
    foreach g [lsort [info global $pattern]] {
        upvar #0 $g A
        if [array exists A] {
            foreach name [lsort [array names A]] {
                Log Global $g : $name :: $A($name)  
            }
        } else {
            Log Global $g :: $A
        }
    }
    Log "}"
}

proc FinishMakefile {} {
    LogGlobalVars {[A-Z]*}

    lassign [clock format [clock seconds] -format "%Y %m %d"] Y M D

    set w [open "Makefile" "w"]
    puts $w "N9 = [ cd ../../../nitros9 ; pwd ]"
    puts $w "LWASM = lwasm"
    puts $w "LWLINK = lwlink"
    puts $w "SIMPLE_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --no-warn=ifp1 --format=os9"
    puts $w "BASIC09_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --no-warn=ifp1 --format=os9"
    puts $w "LINKED_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax,export --no-warn=ifp1 --format=os9"
    puts $w ""
    puts $w "YEAR=[expr $Y-2000]"
    puts $w "MONTH=[expr 1$M-100]"
    puts $w "DAY=[expr 1$D-100]"
    puts $w "VER=  -D'NOS9VER'=\$(YEAR) -D'NOS9MAJ'=\$(MONTH) -D'NOS9MIN'=\$(DAY)"
    puts $w ""
    puts $w "all: \\"
    foreach p [lsort [array names ::Platforms]] {
        puts $w "  platform-$p \\"
    }
    puts $w "  ##"

    foreach platform [lsort [array names ::Platforms]] {
        puts $w "platform-$platform: \\"
        foreach product $::PlatformProducts($platform) {
            puts $w "  product-$platform--$product \\"
        }
        puts $w "  ##"
        puts $w ""
    }

    foreach platform [lsort [array names ::Platforms]] {
        foreach product $::PlatformProducts($platform) {
            puts $w "product-$platform--$product: \\"
            foreach t [lsort -unique $::ProductTargets($platform--$product)] {
                puts $w "  $t \\"
            }
            puts $w "  ##"
            puts $w $::MakeProduct($platform--$product)
        }
    }
    foreach target [lsort [array names ::Targets]] {
       lassign $::Targets($target) kind src flags

        puts $w ""
        puts $w "$target: $src"

        if [lsearch $::BASIC09_CMDS_LEVEL1 $target]>=0 {
            puts $w [tabbed { $(LWASM) $(BASIC09_LWASM_FLAGS) -o'$@' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
        } elseif [lsearch $::LINKED_CMDS_LEVEL1 $target]>=0 {
            # lwlink --format=os9 -L /home/strick/modoc/coco-shelf/nitros9/lib -lnet -lcoco -lalib grep.o -ogrep
            puts $w [tabbed { $(LWASM) $(LINKED_LWASM_FLAGS) --format=obj -o'$@.o' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
            puts $w [tabbed { $(LWLINK) --format=os9 -L ../../../nitros9/lib  -lnet -lcoco -lalib $@.o -o'$@' } "" ]
        } else {
            # # # puts $w \[string cat "\t" $(LWASM) $(SIMPLE_LWASM_FLAGS) -o'$@' $< -I'.' -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $flags \$(VER) \]
            puts $w [tabbed { $(LWASM) $(SIMPLE_LWASM_FLAGS) -o'$@' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
        }
    }
    puts $w ""
    puts $w "clean:"
    puts $w [string cat "\t" {find * -type f ! -name '*.tcl' ! -name '*,v' -print0 | xargs -0 rm -f }]
}

proc tabbed {line flags} {
    return "\t$line $flags"
}

proc Assemble {target source flags} {
    set flags "-D'$::WhichCoco'=1 $flags"
    lappend ::ProductTargets($::Platform--$::Product) $target
    set ::Targets($target) [list lwasm $source $flags]
}

proc CreateFlagsFromArgs {targetVar srcVar flagsVar extraVar spec} {
Log CreateFlagsFromArgs << $targetVar , $srcVar , $flagsVar , $extraVar, $spec
    upvar 1 $targetVar target $flagsVar flags $srcVar src $extraVar extra
    set words [split $spec ","]
    set w0 [lindex $words 0]

    if [regexp {^([A-Za-z0-9_]+)=([A-Za-z0-9_]+)$} $w0 0 1 2] {
        set target $1
        set src $2
    } elseif [regexp {^([A-Za-z0-9_]+)$} $w0] {
        set target $w0
        set src $w0
    } else {
        error "Bad syntax in item: `$w0`"
    }

    set flags ""
    set extra ""
    foreach w [lrange $words 1 end] {
        if [regexp {^([A-Za-z0-9_]+)=(.*)$} $w 0 1 2] {
              append flags " -D'$1'='[expr 0 + $2]'"
        }
        if [regexp {^([A-Za-z0-9_]+)[(]([A-Za-z0-9_]+)[)]=(.*)$} $w 0 1 2 3] {
              append extra " Extra-$1 $2 $3 $src $target" 
        }
    }
Log CreateFlagsFromArgs $spec >> src = $src , target = $target , flags = $flags , extra = $extra
}

proc Create_track35_style_primary_boot {name body} {
    set ::Product $name
    lappend ::PlatformProducts($::Platform) $name
    set ::ProductTargets($::Platform--$::Product) {}
    Log "T35: <$body>"
    regsub -all -line -lineanchor {[#](.*)$} $body {} body
    regsub -all {[@]([A-Za-z0-9_]+)} $body {[Get \1]} body
    Log "....: <$body>"
    #Log "....: <[OncePerLine [subst $body]]>"
    set manifest {}
    foreach it [subst $body] {
        CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindInWithPath  {modules modules/kernel} $src {.asm .as}] result ; set result]"
        if ![catch [list FindInWithPath  {modules modules/kernel} $src {.asm .as}] result] {
            Assemble $target $result $flags
            lappend manifest $target
        } else {
            error "CANNOT FIND `$src`: $result"
        }
    }
    #CatInto $manifest $name
    set ::MakeProduct($::Platform--$name) "\tcat $manifest > $name"
}

proc Create_os9boot_style_secondary_boot {name body} {
    set ::Product $name
    lappend ::PlatformProducts($::Platform) $name
    set ::ProductTargets($::Platform--$::Product) {}
    Log "O9b: <$body>"

    regsub -all -line -lineanchor {[#](.*)$} $body {} body
    regsub -all {[@]([A-Za-z0-9_]+)} $body {[Get \1]} body
    Log "....: <$body>"
    #Log "....: <[OncePerLine [subst $body]]>"
    set manifest {}
    foreach it [subst $body] {
        CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindInWithPath  {modules modules/kernel cmds} $src {.asm .as}] result ; set result]"
        if ![catch [list FindInWithPath  {modules modules/kernel cmds} $src {.asm .as}] result] {
            Assemble $target $result $flags
            lappend manifest $target
        } else {
            error "CANNOT FIND `$src`: $result"
        }
    }
    #CatInto $manifest $name
    set ::MakeProduct($::Platform--$name) "\tcat $manifest > $name"
}

proc Create_hard_disk {name body} {
    Log "Create_hard_disk: $name ..."
    set ::Product $name
    lappend ::PlatformProducts($::Platform) $name
    set ::ProductTargets($::Platform--$::Product) {}

    set params() {}
    foreach {k v} [subst $body] { set params(p$k) $v }
Log 000 [array get params]

    if [info exists params(p-cmds)] {
        set cmds_body $params(p-cmds)
Log 001 $cmds_body
        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body

Log 111 $cmds_body
        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body
Log 222  $cmds_body
Log 223  [subst $cmds_body]

        foreach it [subst $cmds_body] {
Log 333 $it
            CreateFlagsFromArgs target src flags extra $it
            Log " + [catch [list FindInWithPath  {cmds} $src {.asm .as}] result ; set result]"
            if ![catch [list FindInWithPath  {cmds} $src {.asm .as}] result] {
                Assemble $target $result $flags
                lappend manifest $target
            } else {
                Log "CANNOT FIND `$src`: $result"
            }
        }
    }

    set ::MakeProduct($::Platform--$name) "\t:
\tos9 format -l'$params(p-sectors)' $name
\tos9 gen -t'$params(p-track35)' -b'$params(p-os9boot)' $name
\tos9 makdir $name,CMDS
"
    foreach t $::ProductTargets($::Platform--$::Product) {
        append ::MakeProduct($::Platform--$name) "\tos9 copy -r $t $name,CMDS/$t\n"
        append ::MakeProduct($::Platform--$name) "\tos9 attr -q -r -w -e -pr -pe  $name,CMDS/$t\n"
    }

}

proc FindInWithPath {middirs name suffix} {
    set path [Get withpath]
    foreach d [lreverse $path] {
        foreach m $middirs {
            foreach s $suffix {
                if [file exists $d/$m/$name$s] {
                    return $d/$m/$name$s
                }
            }
            #if [file exists $d/$m/$name] {
            #    return $d/$m/$name
            #}
        }
    }
    error "cannot find `$name` in path=`$path` middirs=`$middirs` suffix=`$suffix`"
}

proc CreateDefsfiles {defs} {
    set w [open "defsfile" "w"]
    puts $w $defs
    close $w
}

###################################################

set Defs_coco1 {
Level    equ   1

         use   os9.d
         use   scf.d
         use   rbf.d
         use   coco.d
}

set PIPES { pipeman piper pipe }
set CLOCK_60HZ { clock_60=clock,PwrLnFrq=60 clock2_soft }

CreateDefsfiles $Defs_coco1
InitMakefile


Platform tfr9 {
    set ::WhichCoco coco1
    Using level1 
    Using level1/coco1
            Create_track35_style_primary_boot "tfr9-level1.t35" {
                rel krn krnp2 init
                boot_emu_h1=boot_emu,DISKNUM=1
            }
            Create_os9boot_style_secondary_boot "tfr9-level1.o9b" {
                ioman @CLOCK_60HZ @PIPES
                scf sc6850 term=term_sc6850,HwBASE=0xFF06
                rbf emudsk dd_h1=emudskdesc,DNum=1,DD=1
                [lmap i [Range 2] { string cat "h$i=emudskdesc,DNum=$i" }]
                sysgo shell_21
            }
            Create_hard_disk "tfr9-level1.dsk" {
                -sectors 9999
                -track35 "tfr9-level1.t35"
                -os9boot "tfr9-level1.o9b"
                -cmds {@DEFAULT_CMDS_LEVEL1 @BASIC09_CMDS_LEVEL1 @LINKED_CMDS_LEVEL1}
                -sys @DEFAULT_SYS_LEVEL1
                -startup "/dev/null"
            }


    set ::WhichCoco coco3
    Using level2 
    Using level2/coco3

                    Create_track35_style_primary_boot "tfr9-level2.t35" {
                        l2rel_80=rel,DWidth=80 l2boot_emu_h2=boot_emu,DISKNUM=2 l2krn=krn
                    }
                    Create_os9boot_style_secondary_boot "tfr9-level2.o9b" {
                        l2krnp2=krnp2
                        l2ioman=ioman
                        l2init=init
                        l2rbf=rbf
                        l2scf=scf
                        pipeman
                        piper
                        pipe
                        l2clock_60=clock,PwrLnFrq=60
                        l2clock2_soft=clock2_soft
                        term=term_sc6850,HwBASE=0xff06 sc6850
                        emudsk dd=emudskdesc,DNum=2,DD=1
                        [lmap i [Range 4] { string cat "h$i=emudskdesc,DNum=$i" }]

                        sysgo shell_21
                    }
                    Create_hard_disk "tfr9-level2.dsk" {
                        -sectors 9999
                        -track35 "tfr9-level2.t35"
                        -os9boot "tfr9-level2.o9b"
                        -cmds {@DEFAULT_CMDS_LEVEL1 @BASIC09_CMDS_LEVEL1 @LINKED_CMDS_LEVEL1}
                        -sys @DEFAULT_SYS_LEVEL1
                        -startup "/dev/null"
                    }
}

FinishMakefile
