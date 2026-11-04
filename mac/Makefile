# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# EdWord for macOS - build with the Xcode command line tools:
#
#   cd mac && make                 EdWord.app (Apple silicon and Intel)
#   make ED_VERSION=3.1 ED_ABOUT="Built for our club"
#   make dmg                       EdWord.dmg to hand out
#   make clean
#
# Needs macOS 11 or later and python3 (for the interface strings).
# EdWord.icns was made by icon.py (python3 icon.py EdWord.iconset; iconutil -c icns EdWord.iconset).

CC       = clang
ARCHS   ?= -arch arm64 -arch x86_64
ED_VERSION ?= 3.0
CFLAGS   = -O2 -Wall -mmacosx-version-min=11.0 $(ARCHS) -Ibuild -I.. -DED_VERSION='"$(ED_VERSION)"'
ifdef ED_ABOUT
CFLAGS  += -DED_ABOUT='"$(ED_ABOUT)"'
endif
OBJCFLAGS = $(CFLAGS) -fobjc-arc -fmodules
LDFLAGS  = $(ARCHS) -mmacosx-version-min=11.0 -framework Cocoa

CORE   = doc layout edit undo docops rtf pdf pagedraw raster img thes charset text codes spell md fwp protext chart prt
MAC    = main ewdoc ewpage ewdraw ewforms ewpanels ewprint
OBJS   = $(CORE:%=build/%.o) $(MAC:%=build/%.o) build/lang.o
APP    = EdWord.app

all: $(APP)

build/lang.c build/lang.h: ../lang/strings.txt ../tools/mklang.py
	@mkdir -p build
	python3 ../tools/mklang.py ../lang/strings.txt build/lang.h build/lang.c --utf8

build/%.o: ../%.c ../wp.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/lang.o: build/lang.c
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.m ewmac.h ../wp.h build/lang.h
	$(CC) $(OBJCFLAGS) -c $< -o $@

# the version and About text come from the command line
build/main.o: FORCE

$(APP): $(OBJS) Info.plist EdWord.icns
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	$(CC) $(LDFLAGS) -o $(APP)/Contents/MacOS/EdWord $(OBJS)
	sed 's/@VERSION@/$(ED_VERSION)/g' Info.plist > $(APP)/Contents/Info.plist
	printf 'APPL????' > $(APP)/Contents/PkgInfo
	cp ../dict/EDWORD.DIC ../dict/EDWORD??.DIC ../dict/EDWORD.THS ../dict/COPYING.DIC ../dict/COPYING.THS $(APP)/Contents/Resources/
	-cp ../manual/MANUAL??.PDF $(APP)/Contents/Resources/
	for l in en de es it fr; do mkdir -p $(APP)/Contents/Resources/$$l.lproj; done
	cp EdWord.icns $(APP)/Contents/Resources/
	codesign --force --deep --sign - $(APP)

dmg: $(APP)
	rm -f EdWord.dmg
	hdiutil create -volname EdWord -srcfolder $(APP) -ov -format UDZO EdWord.dmg

clean:
	rm -rf build $(APP) EdWord.dmg

FORCE:
.PHONY: all dmg clean FORCE
