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
set DEFAULT_CMDS_LEVEL2 {
    asm      debug    dump    httpd       merge     procs      syscall
    attr     ded      dw      ident       mfree     prompt     tee
    backup   deiniz   echo    inetd       minted    pwd        telnet
    basic09  deldir   edit    iniz        mmap      pxd
    bawk     del      error   inkey       modpatch  reboot     touch
    binex    devs     exbin   irqs        montype   rename     tsmon
    build    dir      format  link        more      runb       tuneport
    cmp      dirsort  free    list        mpi       save       unlink
    cobbler  disasm           load        os9gen    setime     verify
    copy     display  gfx     login       padrom    shell_21   wcreate
    cputype  dmem     grep    makdir      park      shellplus  xmode
    date     dmode    grfdrv  mdir        pmap      sleep
    dcheck   dsave    help    megaread    proc      smap

    gfx2,include+=level2/coco3/defs
    tmode=xmode,TMODE=1
}

set BASIC09_CMDS_LEVEL1 {
    basic09 runb gfx inkey syscall
}

set LINKED_CMDS_LEVEL1 {
    dw inetd telnet httpd
    grep megaread
}

# NOT USED YET.
set DEFAULT_SYS_LEVEL1 { errors }
set DEFAULT_SYS_LEVEL2 { errors }

# A pure Tcl implementation of a new builtin `lmap`.
proc lmap {varName valueList body} {
    set z {}
    upvar 1 $varName i
    foreach i $valueList {
        lappend z [uplevel 1 $body]
    }
    return $z
}

# A pure Tcl implementation of a new builtin `string cat`.
proc string_cat {args} {
    set z ""
    foreach i $args {
        append z $i
    }
    return $z
}

proc Log {args} {
    puts stderr "LOG: $args"
}

proc ~ {args} {  # Debugging trace, prefixes a command.
    puts stderr "==Exec $args"
    if [catch {uplevel 1 $args} result] {
        puts stderr "==Error $args -> $result"
        error $result
    } else {
        puts stderr "==Result $args -> $result"
        return $result
    }
}

proc Range {n} {
    set z {}
    for {set i 0} {$i < $n} {incr i} {
        lappend z $i
    }
    return $z
}

# Get var value from closest scope it is defined in.
# Rarely used.  Do I still need it?
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

set N9Root [ exec /bin/sh -c "cd ../../nitros9 ; pwd" ]
set Build ./build
set Using __UNSET__

proc Platform {name body} {
    ~ set ::Platform $name

    eval $body
}
proc Level {name body} {  # TODO: Misnomer.
    ~ set ::Level $name
    ~ file mkdir $::Build/$::Platform/$name

    ~ InitMakefile
    ~ eval $body
    ~ FinishMakefile
}
proc Machine {name} {
    set ::Machine $name
}
proc Using {args} {
    set ::Using {}
    foreach a $args {
        lappend ::Using "$::N9Root/$a"
    }
}

proc OncePerLine {list} {
    set z "\n"
    foreach it $list {
        append z "\t$it\n"
    }
    return $z
}

proc InitMakefile {} {
    set ::Makefile ""
    set ::Product __UNKNOWN__
    set ::Products {}  ;# remember these in order
}

proc LogGlobalVars {w {pattern *}} {
    puts $w "#{{{{{{{{{{{{{{{{{{{{"
    foreach g [lsort [info global $pattern]] {
        upvar #0 $g A
        if [array exists A] {
            foreach name [lsort [array names A]] {
                regsub -all "\n" $A($name) "\n#" value
                puts $w "#Global $g : $name :: $value"
            }
        } else {
            regsub -all "\n" $A "\n#" value
            puts $w "#Global $g :: $value"
        }
    }
    puts $w "#}}}}}}}}}}}}}}}}}}}}"
}

proc nobraces {s} {  # For flattening list structures.
    regsub -all {[{}]} $s { }
}

proc FinishMakefile {} {

    lassign [clock format [clock seconds] -format "%Y %m %d"] Y M D

    file mkdir "$::Build/$::Platform/$::Level"
    set w [open "$::Build/$::Platform/$::Level/Makefile" "w"]
    #LogGlobalVars $w {[A-Z]*}
    puts $w "# THIS Makefile IS GENERATED by rmake.tcl"
    puts $w ""
    puts $w "N9 = $::N9Root"
    puts $w "LWASM = \$(N9)/../bin/lwasm"
    puts $w "LWLINK = \$(N9)/../bin/lwlink"
    puts $w "OS9_TOOL = \$(N9)/../bin/os9"
    puts $w "SIMPLE_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --no-warn=ifp1 --format=os9"
    puts $w "BASIC09_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax --no-warn=ifp1 --format=os9"
    puts $w "LINKED_LWASM_FLAGS = --6309 --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax,export --no-warn=ifp1 --format=os9"
    puts $w "USING = [nobraces [lmap i $::Using { list -I'$i/cmds'  -I'$i/modules/kernel'  -I'$i/modules'  -I'$i/' }]]  -I'$::DefsDir' "
    puts $w ""
    puts $w "YEAR=[expr $Y-2000]"
    puts $w "MONTH=[expr 1$M-100]"
    puts $w "DAY=[expr 1$D-100]"
    puts $w "VER=  -D'NOS9VER'=\$(YEAR) -D'NOS9MAJ'=\$(MONTH) -D'NOS9MIN'=\$(DAY) -D'$::Machine'=1 "
    puts $w ""

    puts $w "all: \\"
    foreach product $::Products {
        puts $w " $product \\"
    }
    puts $w "####"
    puts $w ""

    foreach product $::Products {
        puts $w "$product: \\"
        foreach dep $::ProductDepends($product) {
            puts $w " $dep \\"
        }
        foreach target $::ProductTargets($product) {
            puts $w " $target \\"
        }
        puts $w "####"
        puts $w $::MakeProduct($product)
        puts $w ""
    }

    foreach target [lsort [array names ::MakeTarget]] {
       lassign $::MakeTarget($target) kind src flags

        puts $w ""
        puts $w "$target: $src"

        regsub {^(.*)[.]mod$} $target {\1} c
        if [lsearch $::BASIC09_CMDS_LEVEL1 $c]>=0 {
            puts $w [tabbed { $(LWASM) $(BASIC09_LWASM_FLAGS) -o'$@' $< $(USING) $(VER) } $flags]
        } elseif [lsearch $::LINKED_CMDS_LEVEL1 $c]>=0 {
            puts $w [tabbed { $(LWASM) $(LINKED_LWASM_FLAGS) --format=obj -o'$@.o' $< $(USING) $(VER) } $flags]
            # Remove .mod ending, to create plain_name.
            set plain_name [string range $target 0 end-4]
            # Link into plain_name, so OS9 module gets correct module name.
            puts $w [tabbed { $(LWLINK) --format=os9 -L $(N9)/lib  -lnet -lcoco -lalib $@.o } " -o'$plain_name' " ]
            # But rename the file back to the target name with .mod on it.
            puts $w [tabbed "mv -fv $plain_name $target" ""]
        } else {
            puts $w [tabbed { $(LWASM) $(SIMPLE_LWASM_FLAGS) -o'$@' $< $(USING) $(VER) } $flags]
        }
    }
    puts $w ""
    puts $w "####"
    puts $w ""
    puts $w "clean:"
    puts $w [tabbed {find * -type f ! -name 'Makefile' ! -name '*,v' -print0 | xargs -0 rm -f } ""]
}

proc tabbed {line flags} {
    return "\t$line $flags"
}

proc Assemble {target source flags extra} {
    ~ set ::MakeTarget($target) [list lwasm $source "$flags $extra"]
}

proc CreateFlagsFromArgs {targetVar srcVar flagsVar extraVar spec} {
    upvar 1 $targetVar target $flagsVar flags $srcVar src $extraVar extra
    set words [split $spec ","]
    set w0 [lindex $words 0]

    if [regexp {^([A-Za-z0-9_]+)=([A-Za-z0-9_]+)$} $w0 0 1 2] {
        set target $1.mod
        set src $2
    } elseif [regexp {^([A-Za-z0-9_]+)$} $w0] {
        set target $w0.mod
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
        if [regexp {^include[+]=(.*)$} $w 0 1 2] {
              append extra " -I'\$(N9)/$1' "
        }
        # Future syntax for function-call syntax options.
        #if [regexp {^([A-Za-z0-9_]+)[(]([A-Za-z0-9_]+)[)]=(.*)$} $w 0 1 2 3] {
        #      append extra " Extra-$1 $2 $3 $src $target" 
        #}
    }
}

proc CreateProduct {name} {
    Log CreateProduct: $name (after $::Products)
    set ::Product $name
    set ::ProductDepends($name) $::Products  ;# before lappend
    lappend ::Products $name  ;# remember these in order
}

proc Create_track35_style_primary_boot {name body} {
    CreateProduct $name
    Log "T35: <$body>"
    regsub -all -line -lineanchor {[#](.*)$} $body {} body
    regsub -all {[@]([A-Za-z0-9_]+)} $body {[Get \1]} body
    Log "....: <$body>"
    set manifest {}
    foreach it [subst $body] {

        ~ CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindViaUsing  {modules modules/kernel} $src {.asm .as}] result ; set result]"
        if ![catch [list FindViaUsing  {modules modules/kernel} $src {.asm .as}] result] {
            ~ Assemble $target $result $flags $extra
            lappend manifest $target
        } else {
            error "CANNOT FIND `$src`: $result"
        }

    }

    set ::MakeProduct($name) "\tcat $manifest > $name"
    set ::ProductTargets($name) $manifest
}

proc Create_os9boot_style_secondary_boot {name body} {
    CreateProduct $name
    Log "O9b: <$body>"

    regsub -all -line -lineanchor {[#](.*)$} $body {} body
    regsub -all {[@]([A-Za-z0-9_]+)} $body {[Get \1]} body
    Log "....: <$body>"
    set manifest {}
    foreach it [subst $body] {
        ~ CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindViaUsing  {modules modules/kernel cmds} $src {.asm .as}] result ; set result]"
        if ![catch [list FindViaUsing  {modules modules/kernel cmds} $src {.asm .as}] result] {
            ~ Assemble $target $result $flags $extra
            lappend manifest $target
        } else {
            error "CANNOT FIND `$src`: $result"
        }
    }

    set ::MakeProduct($name) "\tcat $manifest > $name"
    set ::ProductTargets($name) $manifest
}

proc Create_hard_disk {name body} {
    CreateProduct $name
    Log "Create_hard_disk: $name ..."

    set params() {}
    foreach {k v} [subst $body] { set params(p$k) $v }

    if [info exists params(p-cmds)] {
        set cmds_body $params(p-cmds)
        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body

        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body

        foreach it [subst $cmds_body] {
            ~ CreateFlagsFromArgs target src flags extra $it
            Log " + [catch [list FindViaUsing  {. cmds} $src {.asm .as}] result ; set result]"
            if ![catch [list FindViaUsing  {. cmds} $src {.asm .as}] result] {
                ~ Assemble $target $result $flags $extra
                lappend manifest $target
            } else {
                Log "CANNOT FIND `$src`: $result"
            }
        }
    }

    set ::MakeProduct($name) "\t:
\t\$(OS9_TOOL) format -l'$params(p-sectors)' -n'$name' $name
\t\$(OS9_TOOL) gen -t'$params(p-track35)' -b'$params(p-os9boot)' $name
\t\$(OS9_TOOL) makdir $name,CMDS
"
    foreach t $manifest {
        regsub {^(.*)[.]mod$} $t {\1} c
        append ::MakeProduct($name) "\t\$(OS9_TOOL) copy -r $t $name,CMDS/$c\n"
        append ::MakeProduct($name) "\t\$(OS9_TOOL) attr -q -r -w -e -pr -pe  $name,CMDS/$c\n"
    }

    set ::ProductTargets($name) $manifest
}

proc FindViaUsing {middirs name suffix} {
    foreach d $::Using {
        foreach m $middirs {
            foreach s $suffix {
                if [file exists $d/$m/$name$s] {
                    return $d/$m/$name$s
                }
            }
        }
    }
    error "cannot find `$name` in Using=`$::Using` middirs=`$middirs` suffix=`$suffix`"
}

###########################################################################
###########################################################################

set PIPES { pipeman piper pipe }
set CLOCK_60HZ { clock_60=clock,PwrLnFrq=60 clock2_soft }

Platform tfr9 {
  Level level1 {
    Machine coco1
    set ::OsLevel 1
    set ::DefsDir "\$(N9)/defs"

    Using level1/coco1 level1 3rdparty/packages/basic09/

    Create_track35_style_primary_boot "tfr9-level1.t35" {
        rel krn krnp2 init
        boot_emu_h1=boot_emu,DISKNUM=1
    }
    Create_os9boot_style_secondary_boot "tfr9-level1.o9b" {
        ioman clock_60=clock,PwrLnFrq=60 clock2_soft
        scf sc6850  term_FF06=term_sc6850,HwBASE=0xFF06,Pauses=0
        rbf  emudsk_8=emudsk,MaxVhd=8
        dd_h1=emudskdesc,DNum=1,DD=1
        [lmap i [Range 8] { string_cat "h$i=emudskdesc,DNum=$i" }]
        pipeman piper pipe
        sysgo_dd=sysgo,dd=1
        shell_21
    }
    Create_hard_disk "tfr9-level1.dsk" {
        -sectors 99999
        -track35 "tfr9-level1.t35"
        -os9boot "tfr9-level1.o9b"
        -cmds {@DEFAULT_CMDS_LEVEL1 @BASIC09_CMDS_LEVEL1 @LINKED_CMDS_LEVEL1}
        -sys @DEFAULT_SYS_LEVEL1
        -startup "/dev/null"
    }
  }
  Level level2 {
    Machine coco3
    set ::OsLevel 2
    set ::DefsDir "\$(N9)/defs"

    # Using level2/coco3/defs level2/coco3 level2 3rdparty/packages/basic09/ level1/coco1 level1
    Using level2/coco3 level2 3rdparty/packages/basic09/ level1/coco1 level1

    Create_track35_style_primary_boot "tfr9-level2.t35" {
        rel_80=rel,Width=80
        boot_emu_h2=boot_emu,DISKNUM=2
        krn
    }
    Create_os9boot_style_secondary_boot "tfr9-level2.o9b" {
        init krnp2 ioman clock_60=clock,PwrLnFrq=60 clock2_soft
        scf sc6850  term_FF06=term_sc6850,HwBASE=0xFF06,Pauses=0
        rbf  emudsk_8=emudsk,MaxVhd=8
        dd_h2=emudskdesc,DNum=2,DD=1
        [lmap i [Range 8] { string_cat "h$i=emudskdesc,DNum=$i" }]
        pipeman piper pipe
        sysgo_dd=sysgo,dd=1
        shell_21
    }
    Create_hard_disk "tfr9-level2.dsk" {
        -sectors 99999
        -track35 "tfr9-level2.t35"
        -os9boot "tfr9-level2.o9b"
        -cmds {@DEFAULT_CMDS_LEVEL2 @BASIC09_CMDS_LEVEL1 @LINKED_CMDS_LEVEL1}
        -sys @DEFAULT_SYS_LEVEL2
        -startup "/dev/null"
    }
  }
}
