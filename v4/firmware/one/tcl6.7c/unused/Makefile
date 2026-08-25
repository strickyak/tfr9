#
# This Makefile is for use when distributing Tcl to the outside world.
# It is normally set up by running the "config" script.  Before modifying
# this file by hand, you should read through the "config" script to see
# what it does.
#
# Some changes you may wish to make here:
#
# 1. To compile for non-UNIX systems (so that only the non-UNIX-specific
# commands are available), change the OBJS line below so it doesn't
# include ${UNIX_OBJS}.  Also, add the switch "-DTCL_GENERIC_ONLY" to
# CFLAGS.  Lastly, you'll have to provide your own replacement for the
# "panic" procedure (see panic.c for what the current one does).

# 2. ANSI-C procedure prototypes are turned on by default if supported
# by the compiler.  To turn them off, uncomment the following line:

# NP =		-DNO_PROTOTYPE

# 3. If you want to put Tcl-related information in non-standard places,
# change the following definitions below to reflect where you want
# things (all must be specified as full rooted path names):
#
#    INSTALL_DIR	Top-level directory in which to install;  contains
#			each of the other directories below.
#    TCL_LIBRARY	Directory in which to install the library of Tcl
#			scripts.  Note: if the TCL_LIBRARY environment
#			variable is specified at run-time then Tcl looks
#			there rather than in the place specified here.
#    LIB_DIR		Directory in which to install the archive libtcl.a
#    INCLUDE_DIR	Directory in which to install include files.
#    MAN3_DIR		Directory in which to install manual entries for
#			library procedures such as Tcl_Eval.
#    MANN_DIR		Directory in which to install manual entries for
#			miscellaneous things such as the Tcl overview
#			manual entry.
#    RANLIB		If you're using a System-V-based UNIX that doesn't
#			have ranlib, change this definition to "echo" or
#			something else harmless.
#    SHELL		Some versions of make (e.g. SGI's) use this variable
#			to determine which shell to use for executing
#			commands.

INSTALL_DIR =	/usr/local
TCL_LIBRARY =	$(INSTALL_DIR)/lib/tcl
LIB_DIR =	$(INSTALL_DIR)/lib
INCLUDE_DIR =	$(INSTALL_DIR)/include
MAN3_DIR =	$(INSTALL_DIR)/man/man3
MANN_DIR =	$(INSTALL_DIR)/man/mann
RANLIB =	ranlib
SHELL =		/bin/sh

CC =		cc
CFLAGS =	-O -I. -DTCL_LIBRARY=\"${TCL_LIBRARY}\" ${NP}

GENERIC_OBJS =	regexp.o tclAssem.o tclBasic.o tclCkalloc.o \
	tclCmdAH.o tclCmdIL.o tclCmdMZ.o tclExpr.o tclGet.o \
	tclHash.o tclHistory.o tclParse.o tclProc.o tclUtil.o \
	tclVar.o

UNIX_OBJS = panic.o tclEnv.o tclGlob.o tclUnixAZ.o tclUnixStr.o \
	tclUnixUtil.o 

COMPAT_OBJS =

OBJS = ${GENERIC_OBJS} ${UNIX_OBJS} ${COMPAT_OBJS}

all: libtcl.a

libtcl.a: ${OBJS}
	rm -f libtcl.a
	ar cr libtcl.a ${OBJS}
	$(RANLIB) libtcl.a

install: libtcl.a
	-if [ ! -d $(LIB_DIR) ] ; then mkdir -p $(LIB_DIR); fi
	-if [ ! -d $(TCL_LIBRARY) ] ; then mkdir -p $(TCL_LIBRARY); fi
	-if [ ! -d $(INCLUDE_DIR) ] ; then mkdir -p $(INCLUDE_DIR); fi
	-if [ ! -d $(MAN3_DIR) ] ; then mkdir -p $(MAN3_DIR); fi
	-if [ ! -d $(MANN_DIR) ] ; then mkdir -p $(MANN_DIR); fi
	rm -rf $(TCL_LIBRARY)/*
	for i in library/*.tcl library/tclIndex; \
	    do \
	    cp $$i $(TCL_LIBRARY);  \
	    done
	rm -f $(LIB_DIR)/libtcl.a
	cp libtcl.a $(LIB_DIR)
	$(RANLIB) $(LIB_DIR)/libtcl.a
	rm -f $(INCLUDE_DIR)/tcl.h $(INCLUDE_DIR)/tclHash.h
	cp tcl.h $(INCLUDE_DIR)
	cp tclHash.h $(INCLUDE_DIR)
	cd doc; for i in *.3; \
	    do \
	    rm -f $(MAN3_DIR)/$$i; \
	    sed -e '/man\.macros/r man.macros' -e '/man\.macros/d' \
		    $$i > $(MAN3_DIR)/$$i; \
	    done; cd ..
	cd doc; for i in *.n; \
	    do \
	    rm -f $(MANN_DIR)/$$i; \
	    sed -e '/man\.macros/r man.macros' -e '/man\.macros/d' \
		    $$i > $(MANN_DIR)/$$i; \
	    done; cd ..

tclTest: tclTest.o libtcl.a
	${CC} ${CFLAGS} tclTest.o libtcl.a -o tclTest

test: tclTest
	( echo cd tests ; echo source all ) | ./tclTest

configured:
	@echo "The configuration script \"./config\" hasn't been run"
	@echo "successfully yet.  Please run it as described in the "
	@echo "README file, then run make again."
	exit 1

clean:
	rm -f ${OBJS} libtcl.a tclTest.o tclTest

# The following target is used during configuration to compile
# a test program to see if certain facilities are available on
# the system.

configtest:
	${CC} ${CFLAGS} test.c

${OBJS}: tcl.h tclHash.h tclInt.h configured
${UNIX_OBJS}: tclUnix.h
