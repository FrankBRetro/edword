#!/bin/sh
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# mkdist.sh - DIST: EdWord for every processor and language, each file once,
# and all of it in EDWORD-<version>.zip to upload ("make dist" runs it)
#
#   DIST/EDWORD/        ready to copy to an Atari as it is: the 68000 program
#                       (it runs on any ST, STE, TT or Falcon), the resource
#                       files of every language, the English dictionary and
#                       thesaurus, the English manual, the licences
#   DIST/CPU/<cpu>/     EDWORD.PRG for a faster processor, to put in its place:
#                       030 (68030), 030F (68030 with an FPU), 040 (68040 or
#                       68060), CF (ColdFire, the FireBee)
#   DIST/LANG/<lang>/   to add for another language: its manual and spelling
#                       dictionary, and an EDWORD.INF that starts EdWord in it
#                       (DE, ES, IT, FR; CS, Czech, has no manual yet and
#                       needs a Czech system font)
#   DIST/TEST/          EDWTEST.PRG, the test build (see README.TXT)
#   DIST/README.TXT     the same in short
set -e
cd "$(dirname "$0")/.."
ver=$(sed -n 's/^#define ED_VERSION "\([^"]*\)".*/\1/p' gemui.h)
rm -rf DIST
mkdir -p DIST/EDWORD
cp EDWORD.PRG EDWORD.RSC EDWORDDE.RSC EDWORDES.RSC EDWORDIT.RSC EDWORDFR.RSC EDWORDCS.RSC DIST/EDWORD/
cp dict/EDWORD.DIC dict/EDWORD.THS dict/COPYING.DIC dict/COPYING.THS manual/MANUALEN.PDF DIST/EDWORD/
cp LICENSE DIST/EDWORD/LICENSE.TXT
for cpu in 030 030F 040 CF; do
    mkdir -p DIST/CPU/$cpu
    cp EDW$cpu.PRG DIST/CPU/$cpu/EDWORD.PRG
done
mkdir -p DIST/TEST
cp EDWTEST.PRG DIST/TEST/
n=1
for lang in DE ES IT FR CS; do
    d=DIST/LANG/$lang
    mkdir -p $d
    cp dict/EDWORD$lang.DIC $d/
    [ -f manual/MANUAL$lang.PDF ] && cp manual/MANUAL$lang.PDF $d/
    printf 'language=%d\r\nspelllang=%d\r\n' $n $n > $d/EDWORD.INF
    n=$((n + 1))
done
printf '%s\r\n' "EdWord $ver for Atari compatible computers" "" \
  "EDWORD      Copy this folder to your Atari and run EDWORD.PRG. It runs on" \
  "            any ST, STE, Mega ST, TT or Falcon, in the language TOS is" \
  "            set to (with English spelling)." \
  "CPU\\030     Faster versions: copy the EDWORD.PRG for your processor over" \
  "CPU\\030F    the one in EDWORD - 030 for a 68030, 030F for a 68030 with" \
  "CPU\\040     an FPU, 040 for a 68040 or 68060, CF for a FireBee." \
  "CPU\\CF" \
  "LANG\\DE     Another language: copy its files into EDWORD. EDWORD.INF" \
  "LANG\\ES     starts EdWord in that language (copy it only the first time:" \
  "LANG\\IT     it holds your settings afterwards)." \
  "LANG\\FR     (CS, Czech, needs a Czech system font with the" \
  "LANG\\CS     Kamenicky letters, and has no manual yet.)" \
  "TEST        EDWTEST.PRG: a test. Put it in EDWORD and run it: it goes" \
  "            through every menu command, types in each view, saves," \
  "            exports and loads (in EDWORD\\EDTEST), then quits. What it" \
  "            did is in EDWORD\\EDTEST.LOG; the last line says DONE." > DIST/README.TXT
chmod -R u+w DIST
zip=EDWORD-$ver.zip
rm -f EDWORD-*.zip
if command -v zip >/dev/null; then zip -qr -9 $zip DIST
else python3 -m zipfile -c $zip DIST; fi
echo "DIST: $(du -sk DIST | cut -f1) KB; $zip"
