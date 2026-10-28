# Makefile for building Gvts
# See ../doc/readme.html for installation and customization instructions.

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Your platform. See PLATS for possible values.
PLAT= guess

CC= gcc -std=gnu99
CFLAGS= -O2 -Wall -Wextra -DGVTS_COMPAT_5_3 $(SYSCFLAGS) $(MYCFLAGS)
LDFLAGS= $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS= -lm $(SYSLIBS) $(MYLIBS)

AR= ar rcu
RANLIB= ranlib
RM= rm -f
UNAME= uname

SYSCFLAGS=
SYSLDFLAGS=
SYSLIBS=

MYCFLAGS=
MYLDFLAGS=
MYLIBS=
MYOBJS=

# Special flags for compiler modules; -Os reduces code size.
CMCFLAGS= 

# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

PLATS= guess aix bsd c89 freebsd generic ios linux linux-readline macosx mingw posix solaris

GVTS_A=	libgvts.a
CORE_O=	Gapi.o Gcode.o Gctype.o Gdebug.o Gdo.o Gdump.o Gfunc.o Ggc.o Glex.o Gmem.o Gobject.o Gopcodes.o Gparser.o Gstate.o Gstring.o Gtable.o Gtm.o Gundump.o Gvm.o Gzio.o
LIB_O=	Gauxlib.o Gbaselib.o Gcorolib.o Gdblib.o Giolib.o Gmathlib.o Goadlib.o Goslib.o Gstrlib.o Gtablib.o Gutf8lib.o Ginit.o
BASE_O= $(CORE_O) $(LIB_O) $(MYOBJS)

GVTS_T=	gvts
GVTS_O=	gvts.o

GVTSC_T=	gvtsc
GVTSC_O=	gvtsc.o

ALL_O= $(BASE_O) $(GVTS_O) $(GVTSC_O)
ALL_T= $(GVTS_A) $(GVTS_T) $(GVTSC_T)
ALL_A= $(GVTS_A)

# Targets start here.
default: $(PLAT)

all:	$(ALL_T)

o:	$(ALL_O)

a:	$(ALL_A)

$(GVTS_A): $(BASE_O)
	$(AR) $@ $(BASE_O)
	$(RANLIB) $@

$(GVTS_T): $(GVTS_O) $(GVTS_A)
	$(CC) -o $@ $(LDFLAGS) $(GVTS_O) $(GVTS_A) $(LIBS)

$(GVTSC_T): $(GVTSC_O) $(GVTS_A)
	$(CC) -o $@ $(LDFLAGS) $(GVTSC_O) $(GVTS_A) $(LIBS)

test:
	./$(GVTS_T) -v

clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM l*.c

echo:
	@echo "PLAT= $(PLAT)"
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "LDFLAGS= $(LDFLAGS)"
	@echo "LIBS= $(LIBS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"
	@echo "UNAME= $(UNAME)"

# Convenience targets for popular platforms.
ALL= all

help:
	@echo "Do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"
	@echo "See doc/readme.html for complete instructions."

guess:
	@echo Guessing `$(UNAME)`
	@$(MAKE) `$(UNAME)`

AIX aix:
	$(MAKE) $(ALL) CC="xlc" CFLAGS="-O2 -DGVTS_USE_POSIX -DGVTS_USE_DLOPEN" SYSLIBS="-ldl" SYSLDFLAGS="-brtl -bexpall"

bsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_POSIX -DGVTS_USE_DLOPEN" SYSLIBS="-Wl,-E"

c89:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_C89" CC="gcc -std=c89"
	@echo ''
	@echo '*** C89 does not guarantee 64-bit integers for Gvts.'
	@echo '*** Make sure to compile all external Gvts libraries'
	@echo '*** with GVTS_USE_C89 to ensure consistency'
	@echo ''

FreeBSD NetBSD OpenBSD freebsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_LINUX -DGVTS_USE_READLINE -I/usr/include/edit" SYSLIBS="-Wl,-E -ledit" CC="cc"

generic: $(ALL)

ios:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_IOS"

Linux linux:	linux-noreadline

linux-noreadline:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_LINUX" SYSLIBS="-Wl,-E -ldl"

linux-readline:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_LINUX -DGVTS_USE_READLINE" SYSLIBS="-Wl,-E -ldl -lreadline"

Darwin macos macosx:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_MACOSX -DGVTS_USE_READLINE" SYSLIBS="-lreadline"

mingw:
	$(MAKE) "GVTS_A=gvts54.dll" "GVTS_T=gvts.exe" \
	"AR=$(CC) -shared -o" "RANLIB=strip --strip-unneeded" \
	"SYSCFLAGS=-DGVTS_BUILD_AS_DLL" "SYSLIBS=" "SYSLDFLAGS=-s" gvts.exe
	$(MAKE) "GVTSC_T=gvtsc.exe" gvtsc.exe

posix:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_POSIX"

SunOS solaris:
	$(MAKE) $(ALL) SYSCFLAGS="-DGVTS_USE_POSIX -DGVTS_USE_DLOPEN -D_REENTRANT" SYSLIBS="-ldl"

# Targets that do not create files (not all makes understand .PHONY).
.PHONY: all $(PLATS) help test clean default o a depend echo

# Compiler modules may use special flags.
Glex.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c Glex.c

Gparser.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c Gparser.c

Gcode.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c Gcode.c

# DO NOT DELETE

Gapi.o: Gapi.c Gprefix.h gvts.h gvtsconf.h Gapi.h Glimits.h Gstate.h \
 Gobject.h Gtm.h Gzio.h Gmem.h Gdebug.h Gdo.h Gfunc.h Ggc.h Gstring.h \
 Gtable.h Gundump.h Gvm.h
Gauxlib.o: Gauxlib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h
Gbaselib.o: Gbaselib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gcode.o: Gcode.c Gprefix.h gvts.h gvtsconf.h Gcode.h Glex.h Gobject.h \
 Glimits.h Gzio.h Gmem.h Gopcodes.h Gparser.h Gdebug.h Gstate.h Gtm.h \
 Gdo.h Ggc.h Gstring.h Gtable.h Gvm.h
Gcorolib.o: Gcorolib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gctype.o: Gctype.c Gprefix.h Gctype.h gvts.h gvtsconf.h Glimits.h
Gdblib.o: Gdblib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gdebug.o: Gdebug.c Gprefix.h gvts.h gvtsconf.h Gapi.h Glimits.h Gstate.h \
 Gobject.h Gtm.h Gzio.h Gmem.h Gcode.h Glex.h Gopcodes.h Gparser.h \
 Gdebug.h Gdo.h Gfunc.h Gstring.h Ggc.h Gtable.h Gvm.h
Gdo.o: Gdo.c Gprefix.h gvts.h gvtsconf.h Gapi.h Glimits.h Gstate.h \
 Gobject.h Gtm.h Gzio.h Gmem.h Gdebug.h Gdo.h Gfunc.h Ggc.h Gopcodes.h \
 Gparser.h Gstring.h Gtable.h Gundump.h Gvm.h
Gdump.o: Gdump.c Gprefix.h gvts.h gvtsconf.h Gobject.h Glimits.h Gstate.h \
 Gtm.h Gzio.h Gmem.h Gundump.h
Gfunc.o: Gfunc.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Gfunc.h Ggc.h
Ggc.o: Ggc.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Gfunc.h Ggc.h Gstring.h Gtable.h
Ginit.o: Ginit.c Gprefix.h gvts.h gvtsconf.h gvtslib.h Gauxlib.h
Giolib.o: Giolib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Glex.o: Glex.c Gprefix.h gvts.h gvtsconf.h Gctype.h Glimits.h Gdebug.h \
 Gstate.h Gobject.h Gtm.h Gzio.h Gmem.h Gdo.h Ggc.h Glex.h Gparser.h \
 Gstring.h Gtable.h
Gmathlib.o: Gmathlib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gmem.o: Gmem.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Ggc.h
Goadlib.o: Goadlib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gobject.o: Gobject.c Gprefix.h gvts.h gvtsconf.h Gctype.h Glimits.h \
 Gdebug.h Gstate.h Gobject.h Gtm.h Gzio.h Gmem.h Gdo.h Gstring.h Ggc.h \
 Gvm.h
Gopcodes.o: Gopcodes.c Gprefix.h Gopcodes.h Glimits.h gvts.h gvtsconf.h
Goslib.o: Goslib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gparser.o: Gparser.c Gprefix.h gvts.h gvtsconf.h Gcode.h Glex.h Gobject.h \
 Glimits.h Gzio.h Gmem.h Gopcodes.h Gparser.h Gdebug.h Gstate.h Gtm.h \
 Gdo.h Gfunc.h Gstring.h Ggc.h Gtable.h
Gstate.o: Gstate.c Gprefix.h gvts.h gvtsconf.h Gapi.h Glimits.h Gstate.h \
 Gobject.h Gtm.h Gzio.h Gmem.h Gdebug.h Gdo.h Gfunc.h Ggc.h Glex.h \
 Gstring.h Gtable.h
Gstring.o: Gstring.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h \
 Gobject.h Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Gstring.h Ggc.h
Gstrlib.o: Gstrlib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gtable.o: Gtable.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Ggc.h Gstring.h Gtable.h Gvm.h
Gtablib.o: Gtablib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gtm.o: Gtm.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Ggc.h Gstring.h Gtable.h Gvm.h
gvts.o: gvts.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
gvtsc.o: gvtsc.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h Gdebug.h Gstate.h \
 Gobject.h Glimits.h Gtm.h Gzio.h Gmem.h Gopcodes.h Gopnames.h Gundump.h
Gundump.o: Gundump.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h \
 Gobject.h Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Gfunc.h Gstring.h Ggc.h \
 Gundump.h
Gutf8lib.o: Gutf8lib.c Gprefix.h gvts.h gvtsconf.h Gauxlib.h gvtslib.h
Gvm.o: Gvm.c Gprefix.h gvts.h gvtsconf.h Gdebug.h Gstate.h Gobject.h \
 Glimits.h Gtm.h Gzio.h Gmem.h Gdo.h Gfunc.h Ggc.h Gopcodes.h Gstring.h \
 Gtable.h Gvm.h Gjumptab.h
Gzio.o: Gzio.c Gprefix.h gvts.h gvtsconf.h Glimits.h Gmem.h Gstate.h \
 Gobject.h Gtm.h Gzio.h

# (end of Makefile)
