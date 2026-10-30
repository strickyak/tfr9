CC = gcc
CFLAGS = -O2 -I.. -DTCL_LIBRARY=\"\" -U TCL_GENERIC_ONLY -DTCL_UNION_WAIT=0

GENERIC_OBJS = regexp.o tclAssem.o tclBasic.o tclCkalloc.o \
	tclCmdAH.o tclCmdIL.o tclCmdMZ.o tclExpr.o tclGet.o \
	tclHash.o tclHistory.o tclParse.o tclProc.o tclUtil.o \
	tclVar.o

UNIX_OBJS = panic.o tclEnv.o tclGlob.o tclUnixAZ.o tclUnixStr.o \
	tclUnixUtil.o 

OBJS = $(GENERIC_OBJS) $(UNIX_OBJS)

VPATH = ..

sixseven: tclTest.o $(OBJS)
	$(CC) $(CFLAGS) -o sixseven tclTest.o $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o sixseven
