# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# EdWord for PC GEM (FreeGEM / OpenGEM on DOS), 32-bit with DJGPP
#
#   source ../../PC-GEM/scripts/djgppenv.sh     (or set DJCC and GEMKIT yourself)
#   make -C pcgem                               -> pcgem/EDWORD.APP
#
# The Atari front end (../gem*.c) and the core are compiled unchanged:
# pcgem/include holds GEMLib-style gem.h, vdi.h and osbind.h, implemented by
# pgaes.c / pgvdi.c (on John Elliott's DJGPP GEM bindings), pgrsc.c (reads the
# Atari EDWORD*.RSC files) and pgtos.c (TOS calls on DOS).
#
# Run it with CWSDPMI.EXE beside it or on the PATH (386 or later).

DJCC   ?= i586-pc-msdosdjgpp-gcc
GEMKIT ?= $(HOME)/work/vibe/PC-GEM/tools/djgppgem
GK      = $(GEMKIT)/CONTRIB/GEM
# The kit's headers, with their struct packing fixed for current gcc: GPK
# expands to the packed attribute plus ';', which gcc ignores after a
# typedef's name, so "} NAME GPK" becomes "} __attribute__((packed)) NAME;"
# (without it TEDINFO, ICONBLK etc. would no longer match LIBGEM.A)
GKINC   = obj/kitinc

CFLAGS  = -O2 -Wall -std=gnu89 -Wno-address-of-packed-member -DPCGEM -Iinclude -I..
ifdef EDLOG
CFLAGS += -DEDLOG
endif
ifdef ED_VERSION
CFLAGS += -DED_VERSION='"$(ED_VERSION)"'
endif
KITFLAGS = -O2 -Wall -Wno-address-of-packed-member -fgnu89-inline -I$(GKINC)   # the kit uses GNU89 "extern inline"
ifdef EDLOG
KITFLAGS += -DEDLOG
endif

CORE    = doc layout edit undo docops rtf pdf pagedraw raster img thes \
          charset text codes spell md fwp protext chart prt
FRONT   = gem gemwin gemdlg gemrsc gemrend gemtheme gemtool gemtwin gemobj gemclip lang
PORT    = pgrsc pgtos pglinear
KITPORT = pgaes pgvdi

OBJS = $(addprefix obj/,$(addsuffix .o,$(CORE) $(FRONT) $(PORT) $(KITPORT) pgcpu))

EDWORD.APP: $(OBJS)
	$(DJCC) -o edword.exe $(OBJS) $(GK)/LIB/LIBGEM.A
	cp edword.exe $@

obj/%.o: ../%.c ../wp.h
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
obj/pgrsc.o obj/pgtos.o obj/pglinear.o: obj/%.o: %.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<
# these two talk to the bindings, so they see the kit's headers, not ours
obj/pgaes.o obj/pgvdi.o: obj/%.o: %.c pgkit.h $(GKINC)/.fixed
	@mkdir -p obj
	$(DJCC) $(KITFLAGS) -c -o $@ $<
$(GKINC)/.fixed: $(wildcard $(GK)/INCLUDE/*.H)
	@test -f $(GK)/INCLUDE/DJGPPGEM.H || { echo "DJGPP GEM kit not found: set GEMKIT"; exit 1; }
	rm -rf $(GKINC); mkdir -p $(GKINC)
	for f in $(GK)/INCLUDE/*.H; do \
	  lc=$$(basename "$$f" | tr A-Z a-z); \
	  sed -E 's/^\}[[:space:]]*([A-Za-z_0-9]+)[[:space:]]*(__attribute__\(\(packed\)\))?[[:space:]]*GPK/} __attribute__((packed)) \1;/' "$$f" > $(GKINC)/$$lc; \
	done
	touch $@

obj/pgcpu.o: pgcpu.c
	@mkdir -p obj
	$(DJCC) $(CFLAGS) -c -o $@ $<

# the resource-file loader checked on the build machine against the real files
rsctest: pgrsc.c test/rsctest.c
	cc -std=gnu89 -DPG_HOSTCHECK -Wall -Wno-address-of-packed-member -Iinclude -I.. -o rsctest test/rsctest.c pgrsc.c
	for r in ../EDWORD*.RSC; do ./rsctest $$r || exit 1; done

# the PC GEM release zip: EDWORD-<version>-PCGEM.zip with an EDWORD folder
# ready to copy to the PC (build without EDLOG for it)
CWSDPMI ?= $(HOME)/work/vibe/PC-GEM/tools/cwsdpmi/bin
VER  = $(shell sed -n 's/^\#define ED_VERSION "\(.*\)".*/\1/p' ../gemui.h)
DIST = dist/EDWORD
dist: EDWORD.APP
	@test -f $(CWSDPMI)/CWSDPMI.EXE || { echo "CWSDPMI.EXE not found: set CWSDPMI"; exit 1; }
	rm -rf dist EDWORD-$(VER)-PCGEM.zip
	mkdir -p $(DIST)
	cp EDWORD.APP README.TXT $(DIST)/
	cp ../EDWORD*.RSC ../dict/*.DIC ../dict/EDWORD.THS ../dict/COPYING.THS $(DIST)/
	cp ../LICENSE $(DIST)/LICENSE.TXT
	cp $(CWSDPMI)/CWSDPMI.EXE $(DIST)/
	cp $(CWSDPMI)/cwsdpmi.doc $(DIST)/CWSDPMI.DOC
	cd dist && zip -r -X ../EDWORD-$(VER)-PCGEM.zip EDWORD
	@echo "pcgem/EDWORD-$(VER)-PCGEM.zip ready"

clean:
	rm -rf obj dist edword.exe EDWORD.APP rsctest

.PHONY: clean rsctest dist
