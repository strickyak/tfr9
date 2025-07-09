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

#.  # Queries the names of entries in a global multi-dimensional table
#.  # (i.e. array) with commas separating dimensions.
#.  # Returns sorted list of names of the dimension that comes
#.  # after prefix head.
#.  proc gnames {gtable head} {
#.      upvar #0 $gtable T
#.      set R(0) 0
#.      unset R(0)
#.  
#.      set fix {}
#.      set search $head,*
#.      if {$head == ""} {
#.          set fix F,
#.          set head F
#.          set search *
#.      }
#.      set pattern "^$head,(.*)$"
#.  
#.      foreach path [array names T $search] {
#.          set path "$fix$path"
#.          regsub $pattern $path {\1} path
#.          regsub {^([^,]*),.*$} $path {\1} path
#.          set R($path) 1
#.      }
#.      lsort [array names R]
#.  }
#.  
#.  if 0 {
#.  # set x(one) 1
#.  set x(one,two) 1
#.  set x(one,two,three) 1
#.  set x(one,two,tres) 1
#.  set x(one,dos,three) 1
#.  set x(one,dos,tres) 1
#.  # set x(uno) 1
#.  set x(uno,two) 1
#.  set x(uno,two,three) 1
#.  set x(uno,two,tres) 1
#.  set x(uno,dos,three) 1
#.  set x(uno,dos,tres) 1
#.  set x(uno,dos,tres,4) 1
#.  set x(uno,dos,tres,444) 1
#.  puts [lsort [array names x]]
#.  puts 0:[gnames x {}]
#.  puts 1:[gnames x one]
#.  puts 2:[gnames x uno]
#.  foreach i [gnames x uno] { puts 3:$i:[gnames x uno,$i] }
#.  foreach i [gnames x uno,dos] { puts 4:$i:[gnames x uno,dos,$i] }
#.  exit 0
#.  }

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

#set N9Root [ cd ../../nitros9 ; pwd ]
set N9Root [ exec /bin/sh -c "cd ../../nitros9 ; pwd" ]
set Build ./build
set Using __UNSET__

proc Platform {name body} {
    set ::Platforms($name) 1
    set ::Platform $name
    file mkdir $::Build/$name

    InitMakefile
    eval $body
    FinishMakefile
}
proc Level {name body} {
    set ::Level $name
    set ::Levels($::Platform,$name) 1
    file mkdir $::Build/$::Platform/$name
    eval $body
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

proc FinishMakefile {} {

    lassign [clock format [clock seconds] -format "%Y %m %d"] Y M D

    file mkdir "$::Build/$::Platform/$::Level"
    set w [open "$::Build/$::Platform/$::Level/Makefile" "w"]
    LogGlobalVars $w {[A-Z]*}
    puts $w "N9 = $::N9Root"
    puts $w "LWASM = \$(N9)/../bin/lwasm"
    puts $w "LWLINK = \$(N9)/../bin/lwlink"
    puts $w "OS9_TOOL = \$(N9)/../bin/os9"
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
    foreach product $::Products {
        puts $w " $product \\"
    }
    puts $w "####"
    puts $w ""

    foreach product $::Products {
        puts $w "$product: \\"
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
            puts $w [tabbed { $(LWASM) $(BASIC09_LWASM_FLAGS) -o'$@' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
        } elseif [lsearch $::LINKED_CMDS_LEVEL1 $c]>=0 {
            # lwlink --format=os9 -L /home/strick/modoc/coco-shelf/nitros9/lib -lnet -lcoco -lalib grep.o -ogrep
            puts $w [tabbed { $(LWASM) $(LINKED_LWASM_FLAGS) --format=obj -o'$@.o' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
            puts $w [tabbed { $(LWLINK) --format=os9 -L $(N9)/lib  -lnet -lcoco -lalib $@.o -o'$@' } "" ]
        } else {
            # # # puts $w \[string cat "\t" $(LWASM) $(SIMPLE_LWASM_FLAGS) -o'$@' $< -I'.' -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $flags \$(VER) \]
            puts $w [tabbed { $(LWASM) $(SIMPLE_LWASM_FLAGS) -o'$@' $< -I'$(N9)/level1/coco1/cmds' -I'$(N9)/level1/cmds' -I'$(N9)/level1/modules' -I'$(N9)/defs/' $(VER) } $flags]
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

proc Assemble {target source flags} {
    set flags "-D'$::Machine'=1 $flags"
    set ::MakeTarget($target) [list lwasm $source $flags]
}

proc CreateFlagsFromArgs {targetVar srcVar flagsVar extraVar spec} {
Log CreateFlagsFromArgs << $targetVar , $srcVar , $flagsVar , $extraVar, $spec
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
        #if [regexp {^([A-Za-z0-9_]+)[(]([A-Za-z0-9_]+)[)]=(.*)$} $w 0 1 2 3] {
        #      append extra " Extra-$1 $2 $3 $src $target" 
        #}
    }
Log CreateFlagsFromArgs $spec >> src = $src , target = $target , flags = $flags , extra = $extra
}

proc CreateProduct {name} {
    Log CreateProduct: $name (after $::Products)
    set ::Product $name
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

        CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindViaUsing  {modules modules/kernel} $src {.asm .as}] result ; set result]"
        if ![catch [list FindViaUsing  {modules modules/kernel} $src {.asm .as}] result] {
            Assemble $target $result $flags
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
        CreateFlagsFromArgs target src flags extra $it
        Log " + [catch [list FindViaUsing  {modules modules/kernel cmds} $src {.asm .as}] result ; set result]"
        if ![catch [list FindViaUsing  {modules modules/kernel cmds} $src {.asm .as}] result] {
            Assemble $target $result $flags
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
#Log 000 [array get params]

    if [info exists params(p-cmds)] {
        set cmds_body $params(p-cmds)
#Log 001 $cmds_body
        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body

#Log 111 $cmds_body
        regsub -all -line -lineanchor {[#](.*)$} $cmds_body {} cmds_body
        regsub -all {[@]([A-Za-z0-9_]+)} $cmds_body {[Get \1]} cmds_body
#Log 222  $cmds_body
#Log 223  [subst $cmds_body]

        foreach it [subst $cmds_body] {
#Log 333 $it
            CreateFlagsFromArgs target src flags extra $it
            Log " + [catch [list FindViaUsing  {. cmds} $src {.asm .as}] result ; set result]"
            if ![catch [list FindViaUsing  {. cmds} $src {.asm .as}] result] {
                Assemble $target $result $flags
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

    Using level1/coco1 level1 3rdparty/packages/basic09/

    Create_track35_style_primary_boot "tfr9-level1.t35" {
        rel krn krnp2 init
        boot_emu_h1=boot_emu,DISKNUM=1
    }
    Create_os9boot_style_secondary_boot "tfr9-level1.o9b" {
        ioman @CLOCK_60HZ @PIPES
        scf sc6850  term=term_sc6850,HwBASE=0xFF06
        rbf  emudsk_8=emudsk,MaxVhd=8
        dd_h1=emudskdesc,DNum=1,DD=1
        [lmap i [Range 8] { string cat "h$i=emudskdesc,DNum=$i" }]
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
}
