# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# EdWord - build with the m68k-atari-mint cross toolchain and gemlib
#   https://tho-otto.de/crossmint.php  or  Vincent Riviere's packages
#
#   make                      every version and DIST (see make dist below)
#   make EDWORD.PRG           just the 68000 version
#   make EDLOG=1              ... that writes a start-up log, EDWORD.LOG
#   make EDDEMO=1             ... that runs DEMO.SCR (screenshots for the manual)
#   make EDTEST=1             EDWTEST.PRG, the test build: in English, it runs
#                             every menu command, edits in each view, saves,
#                             exports and loads, and logs it all to EDTEST.LOG
#   make ED_VERSION=3.1 ED_ABOUT="Built for the Atari club"
#                             set the version and the sentence in the About box
#
# Versions for faster processors (objects in build/<cpu>):
#   make CPU=030              EDW030.PRG   68030 (or 68020) without an FPU
#   make CPU=030F             EDW030F.PRG  68030 with a 68881/68882 FPU
#   make CPU=040              EDW040.PRG   68040 or 68060 (with its FPU)
#   make CPU=CF               EDWCF.PRG    ColdFire V4e (FireBee)
#   make cpus                 all four
#   make dist                 all five, then DIST: DIST/EDWORD ready to copy to an
#                             Atari (68000 program, resource files, dictionaries,
#                             manual), DIST/CPU/<cpu> the faster programs, DIST/LANG/
#                             <lang> each language's dictionary and manual; and all
#                             of it in EDWORD-<version>.zip
# The ColdFire one needs MiNTLib and gemlib built for m5475 (the "V4E"
# libraries); the others use the usual 68000 ones. Each refuses to start, with
# an alert, on a processor it can't run on (cpucheck.c).

CC      = m68k-atari-mint-gcc
STRIP   = m68k-atari-mint-strip
PYTHON  = python3

CPUFLAGS = -m68000
LDCPU    = $(CPUFLAGS)
TARGET   = EDWORD.PRG
OBJDIR   = .
ifeq ($(CPU),030)
CPUFLAGS = -m68030 -msoft-float -DED_CPU='"68030"' -DED_CPUMIN=20
TARGET   = EDW030.PRG
endif
ifeq ($(CPU),030F)
CPUFLAGS = -m68030 -m68881 -DED_CPU='"68030+FPU"' -DED_CPUMIN=20 -DED_FPU
TARGET   = EDW030F.PRG
endif
ifeq ($(CPU),040)
CPUFLAGS = -m68040 -DED_CPU='"68040/060"' -DED_CPUMIN=40 -DED_FPU
TARGET   = EDW040.PRG
endif
ifeq ($(CPU),CF)
CPUFLAGS = -mcpu=5475 -DED_CPU='"ColdFire"'
TARGET   = EDWCF.PRG
endif
ifdef CPU
OBJDIR   = build/$(CPU)
endif
ifdef EDTEST
TARGET   = EDWTEST.PRG
OBJDIR   = build/test
endif
# The processor check and the strings of its alert run on any ST. The 68k
# versions link with the 68000 libraries too, so that the C library's start-up
# runs everywhere and the check gets its turn (EdWord calls nothing in them
# that passes floating point numbers).
ifeq ($(CPU),CF)
SAFEFLAGS := $(CPUFLAGS)
else
SAFEFLAGS := -m68000 -msoft-float $(filter -D%,$(CPUFLAGS))
LDCPU    := -m68000 -msoft-float
endif

CFLAGS  = -O2 $(CPUFLAGS) -fomit-frame-pointer -Wall -std=gnu89

ifdef EDDIAG
CFLAGS += -DEDDIAG
endif
ifdef EDLOG
CFLAGS += -DEDLOG
endif
ifdef EDDEMO
CFLAGS += -DEDDEMO
endif
ifdef EDTEST
CFLAGS += -DEDDEMO -DEDTEST
endif
ifdef ED_VERSION
CFLAGS += -DED_VERSION='"$(ED_VERSION)"'
endif
ifdef ED_ABOUT
CFLAGS += -DED_ABOUT='"$(ED_ABOUT)"'
endif
LDLIBS  = -lgem

CORE    = doc.o layout.o edit.o undo.o docops.o rtf.o pdf.o pagedraw.o raster.o img.o thes.o \
          charset.o text.o codes.o spell.o md.o fwp.o protext.o chart.o prt.o
GEMOBJ  = gem.o gemwin.o gemdlg.o gemrsc.o gemrend.o gemtheme.o gemtool.o gemtwin.o gemobj.o gemclip.o \
          demo.o lang.o cpucheck.o
OBJS    = $(addprefix $(OBJDIR)/,$(GEMOBJ) $(CORE))

# "make" alone builds every processor's version and DIST; a build for one
# processor, or with EDLOG, EDDEMO, EDDIAG or EDTEST, just its program
ifeq ($(CPU)$(EDLOG)$(EDDEMO)$(EDDIAG)$(EDTEST),)
all: dist
else
all: $(TARGET)
endif

$(TARGET): $(OBJS)
	$(CC) $(LDCPU) -o $@ $(OBJS) $(LDLIBS)
	$(STRIP) $@

cpus:
	$(MAKE) CPU=030
	$(MAKE) CPU=030F
	$(MAKE) CPU=040
	$(MAKE) CPU=CF

# the user interface strings in five languages
lang.c lang.h: lang/strings.txt tools/mklang.py
	$(PYTHON) tools/mklang.py lang/strings.txt lang.h lang.c

$(addprefix $(OBJDIR)/,$(GEMOBJ)): lang.h gemui.h edrsc.h

# the resource files, one per language, and their object numbers
RSCS    = EDWORD.RSC EDWORDDE.RSC EDWORDES.RSC EDWORDIT.RSC EDWORDFR.RSC EDWORDCS.RSC
rsc edrsc.h $(RSCS): lang/strings.txt tools/mkrsc.py tools/mklang.py tools/icons.h
	$(PYTHON) tools/mkrsc.py lang/strings.txt .
# the version and About text change with the command line
$(OBJDIR)/gemdlg.o: FORCE
FORCE:

$(OBJDIR)/lang.o $(OBJDIR)/cpucheck.o: CPUFLAGS = $(SAFEFLAGS)
$(OBJDIR)/cpucheck.o: lang.h

$(OBJDIR)/%.o: %.c wp.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# the dictionary builder, for the ST (or use mkdic on a PC, see below)
MKDIC.TTP: tools/mkdic.c charset.c wp.h
	$(CC) $(CFLAGS) -o $@ tools/mkdic.c charset.c

# ---- host side (PC) ----------------------------------------------------
HOSTCC  = cc
HOSTSRC = doc.c layout.c edit.c undo.c docops.c rtf.c pdf.c pagedraw.c raster.c img.c thes.c \
          charset.c text.c codes.c spell.c md.c fwp.c protext.c chart.c prt.c

hosttest: $(HOSTSRC) test/hosttest.c wp.h
	$(HOSTCC) -g -Wall -fsanitize=address,undefined -I. -o hosttest test/hosttest.c $(HOSTSRC)

mkdic: tools/mkdic.c charset.c wp.h
	$(HOSTCC) -O2 -Wall -o mkdic tools/mkdic.c charset.c

# rebuild dict/EDWORD.DIC from any word list, one word per line:
#   make mkdic && ./mkdic words.txt dict/EDWORD.DIC
# rebuild dict/EDWORD.THS from Open English WordNet (YAML sources):
#   python3 tools/mkthes.py english-wordnet/src/yaml words.txt dict/EDWORD.THS

clean:
	rm -f *.o EDWORD.PRG MKDIC.TTP hosttest mkdic
	rm -rf build EDW030.PRG EDW030F.PRG EDW040.PRG EDWCF.PRG EDWTEST.PRG DIST EDWORD-*.zip

dist: EDWORD.PRG $(RSCS)
	$(MAKE) cpus
	$(MAKE) EDTEST=1
	sh tools/mkdist.sh

.PHONY: all clean cpus dist FORCE
