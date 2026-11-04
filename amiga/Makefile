# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# EdWord for the Amiga (AmigaOS 2.04 or newer, any 68000 or better)
#
# Built with Bartman's m68k-amiga-elf GCC (the toolchain of the
# vscode-amiga-debug extension) and its elf2hunk; the Amiga NDK headers
# come with it. No C library is needed: amiga/sys has the little EdWord
# uses (start-up, memory, strings, files, printf).
#
#   make                         EdWord (the program, Workbench 2.04 and newer)
#   make AMI13=1                 EdWord13, for Workbench 1.2 and 1.3
#   make icons                   EdWord.info, EdWord13.info, EdWordDoc.info
#   make ED_VERSION=3.1          set the version shown in the About box
#
# PATH must hold m68k-amiga-elf-gcc and elf2hunk, e.g.
#   export PATH=$HOME/.vscode/extensions/bartmanabyss.amiga-debug-*/bin/linux/opt/bin:$HOME/.vscode/extensions/bartmanabyss.amiga-debug-*/bin/linux:$PATH

CC      = m68k-amiga-elf-gcc
AS      = m68k-amiga-elf-as
ELF2HUNK = elf2hunk
PYTHON  = python3
CFLAGS  = -m68000 -O2 -std=gnu99 -nostdlib -fomit-frame-pointer -fno-tree-loop-distribute-patterns \
          -fno-reorder-functions -fno-reorder-blocks-and-partition -fno-exceptions -fno-asynchronous-unwind-tables \
          -Iinclude -I$(B) -I.. -Wall -Wno-pointer-sign -Wno-volatile-register-var -Wno-unused-function -Wno-stringop-truncation -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -Wno-format-overflow -Wno-format-truncation
ASFLAGS = -mcpu=68000 --register-prefix-optional
ifdef EDLOG
CFLAGS += -DEDLOG
endif
ifdef ED_VERSION
CFLAGS += -DED_VERSION='"$(ED_VERSION)"'
endif
ifdef ED_ABOUT
CFLAGS += -DED_ABOUT='"$(ED_ABOUT)"'
endif

ifdef AMI13
CFLAGS += -DAMI13
B       = build13
PROG    = EdWord13
EXTRA   = ami13
else
B       = build
PROG    = EdWord
EXTRA   =
endif

CORE    = doc layout edit undo docops rtf pdf pagedraw raster img thes charset text codes spell md fwp protext chart prt
UI      = ami amiwin amigfx amirend amidlg amitwin amitool amiobj amiclip $(EXTRA)
OBJS    = $(B)/start.o $(addprefix $(B)/,$(addsuffix .o,$(UI) $(CORE))) $(B)/lang.o $(B)/libc.o $(B)/arith.o

all: $(PROG)

# everything is rebuilt when the options change (EDLOG=1, ED_VERSION...)
$(B)/cflags: FORCE
	@mkdir -p $(B)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

$(PROG): $(B)/$(PROG).elf
	$(ELF2HUNK) $< $@

$(B)/$(PROG).elf: $(OBJS)
	$(CC) -m68000 -nostdlib -Wl,--emit-relocs,-Ttext=0 $(OBJS) -o $@

$(B)/lang.c $(B)/lang.h: ../lang/strings.txt ../tools/mklang.py
	@mkdir -p $(B)
	$(PYTHON) ../tools/mklang.py ../lang/strings.txt $(B)/lang.h $(B)/lang.c --latin1

$(B)/start.o: sys/start.s
	@mkdir -p $(B)
	$(AS) $(ASFLAGS) -o $@ $<
$(B)/arith.o: sys/arith.s
	$(AS) $(ASFLAGS) -o $@ $<
$(B)/libc.o: sys/libc.c $(B)/cflags
	$(CC) $(CFLAGS) -ffreestanding -c -o $@ $<
$(B)/lang.o: $(B)/lang.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: %.c amiui.h ami13.h ../wp.h $(B)/lang.h $(B)/cflags
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: ../%.c ../wp.h $(B)/cflags
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/ami.o: FORCE
FORCE:

icons: ../tools/mkinfo.py
	$(PYTHON) ../tools/mkinfo.py .

clean:
	rm -rf build build13 EdWord EdWord13
