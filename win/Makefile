# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# EdWord for Windows (Windows 95 to Windows 11, 32-bit, any 386 or better)
#
# Built with clang and lld as a cross compiler for i686-w64-mingw32, using
# the mingw-w64 headers and import libraries (only the Windows API is used:
# win/sys has the little C library EdWord needs, so no msvcrt.dll or other
# run-time is needed, and the program runs on Windows 95 as it is).
#
#   make                         EdWord.exe
#   make ED_VERSION=3.1          set the version shown in the About box
#   make EDLOG=1                 EdWord.log of how long things take
#   make icon                    win/res/edword.ico again (needs Pillow)
#
# W32INC and W32LIB are mingw-w64's headers and import libraries (Debian's
# mingw-w64 package puts them under /usr/i686-w64-mingw32).

CLANG   = clang
RC      = llvm-rc
PYTHON  = python3
W32INC  = /usr/i686-w64-mingw32/include
W32LIB  = /usr/i686-w64-mingw32/lib
CLANGINC := $(shell $(CLANG) -print-resource-dir)/include
ED_VERSION ?= 3.0

TARGET  = --target=i686-w64-mingw32
CFLAGS  = $(TARGET) -nostdinc -isystem $(CLANGINC) -Iinclude -isystem $(W32INC) -I$(B) -I. -I.. \
          -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -D_WIN32_IE=0x0300 -DNOMINMAX \
          -O2 -march=i386 -mtune=i686 -fno-builtin -fno-stack-protector -fno-ident -ffunction-sections \
          -Wall -Wno-pointer-sign -Wno-unused-function -Wno-missing-braces -Wno-char-subscripts \
          -Wno-incompatible-pointer-types-discards-qualifiers -Wno-parentheses -Wno-format-truncation \
          -DED_VERSION='"$(ED_VERSION)"'
LDFLAGS = $(TARGET) -nostdlib -fuse-ld=lld -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 \
          -Wl,-e,_WinMainCRTStartup -Wl,--gc-sections -s -L$(W32LIB)
LIBS    = -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lcomctl32 -lshell32
ifdef EDLOG
CFLAGS += -DEDLOG
endif
ifdef ED_ABOUT
CFLAGS += -DED_ABOUT='"$(ED_ABOUT)"'
endif

B       = build
CORE    = doc layout edit undo docops rtf pdf pagedraw raster img thes charset text codes spell md fwp protext chart prt
UI      = winmain winwin wingfx winrend windlg wintwin wintool winobj winclip
OBJS    = $(addprefix $(B)/,$(addsuffix .o,$(UI) $(CORE))) $(B)/lang.o $(B)/libc.o $(B)/chkstk.o $(B)/edword.res

all: EdWord.exe

$(B)/cflags: FORCE
	@mkdir -p $(B)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

EdWord.exe: $(OBJS)
	$(CLANG) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(B)/lang.c $(B)/lang.h: ../lang/strings.txt ../tools/mklang.py
	@mkdir -p $(B)
	$(PYTHON) ../tools/mklang.py ../lang/strings.txt $(B)/lang.h $(B)/lang.c --cp1252

$(B)/verinfo.h: $(B)/cflags
	@mkdir -p $(B)
	@v='$(ED_VERSION)'; set -- $$(echo "$$v.0.0.0" | tr '.' ' '); \
	 printf '#define ED_VER_N %s,%s,%s,%s\n#define ED_VER_S "%s"\n' $$1 $$2 $$3 0 "$$v" > $@

$(B)/edword.res: res/edword.rc res/edword.ico res/edword.manifest $(B)/verinfo.h
	$(RC) -I$(B) -Ires -FO $@ res/edword.rc

$(B)/libc.o: sys/libc.c $(B)/cflags
	$(CLANG) $(CFLAGS) -ffreestanding -c -o $@ $<
$(B)/chkstk.o: sys/chkstk.s
	@mkdir -p $(B)
	$(CLANG) $(TARGET) -c -o $@ $<
$(B)/lang.o: $(B)/lang.c
	$(CLANG) $(CFLAGS) -c -o $@ $<
$(B)/%.o: %.c winui.h ../wp.h $(B)/lang.h $(B)/cflags
	$(CLANG) $(CFLAGS) -c -o $@ $<
$(B)/%.o: ../%.c ../wp.h $(B)/cflags
	@mkdir -p $(B)
	$(CLANG) $(CFLAGS) -c -o $@ $<
FORCE:

icon: ../tools/mkico.py ../mac/icon.py
	$(PYTHON) ../tools/mkico.py res/edword.ico

clean:
	rm -rf build EdWord.exe
