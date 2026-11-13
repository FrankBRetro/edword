#!/bin/sh
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# build.sh - make the manual's screenshots in Hatari and the five PDF manuals
#
#   HATARI=/path/to/hatari TOS4=/path/to/tos404.img sh manual/build.sh
#   (LANGS="en de" for some of the languages; SKIP256=1 for only the
#   high-colour screenshot)
#
# Needs: the m68k-atari-mint cross compiler, Hatari, a TOS 4.04 ROM image, a
# host C compiler, Python 3 with Pillow and ReportLab, and the DejaVu fonts.
# Run from the EdWord source folder. The screenshots are taken on an emulated
# Falcon in 640x400 with 256 colours (TOS 4's 3D dialogs, EdWord's colour
# toolbar; the one of two windows' own themes in high colour), without GDOS, so only the system font is shown; EdWord is set to
# each language in turn (TOS's own file selector and alerts stay English).
# Screenshots go to manual/shots/LANG, the manuals to manual/MANUAL??.PDF.
set -e
HATARI=${HATARI:-hatari}
TOS4=${TOS4:-tos404.img}
WORK=${WORK:-/tmp/edword-manual}
HOSTSRC="doc.c layout.c edit.c undo.c docops.c rtf.c pdf.c pagedraw.c raster.c img.c thes.c charset.c text.c codes.c spell.c md.c fwp.c protext.c chart.c prt.c"

mkdir -p $WORK
make EDDEMO=1 OBJDIR=$WORK/obj TARGET=$WORK/EDDEMO.PRG
cc -O2 -I. -o $WORK/mkdemo manual/mkdemo.c $HOSTSRC

for L in ${LANGS:-en de es it fr}; do
    case $L in en) n=0;; de) n=1;; es) n=2;; it) n=3;; fr) n=4;; esac
    HD=$WORK/hd_$L
    # first on 256 colours (Falcon video mode 0x010B), then the per-window
    # themes on high colour (0x010C); SKIP256=1 takes only the second
    for MODE in 0B 0C; do
    [ $MODE = 0B ] && [ -n "$SKIP256" ] && continue
    rm -rf $HD; mkdir -p $HD/EDWORD
    printf '%s\r\n' '#a000000' '#b000000' '#c7770007000600070055200505552220770557075055507703111103' \
        '#d' "#E 18 10 00 06 01 $MODE 00 00 00 00 " '#Q 41 40 43 40 43 40 ' \
        '#M 00 01 0B FF C HARD DISK@ @ ' '#T 00 03 02 FF   TRASH@ @ ' > $HD/NEWDESK.INF
    cp $WORK/EDDEMO.PRG $HD/EDWORD/EDWORD.PRG
    cp EDWORD*.RSC dict/EDWORD.DIC dict/EDWORD.THS $HD/EDWORD/
    if [ $MODE = 0B ]; then cp manual/demo.scr $HD/EDWORD/DEMO.SCR; else cp manual/demo_tc.scr $HD/EDWORD/DEMO.SCR; fi
    printf 'language=%d\r\nspelllang=0\r\ntheme=0\r\n' $n > $HD/EDWORD/EDWORD.INF
    $WORK/mkdemo $HD/EDWORD/DEMO.RTF
    python3 manual/mkimg.py $HD/EDWORD/LIGHT.PNG
    printf 'Notes from the committee meeting\r\n\r\nThe meeting agreed to buy new oars\r\nand to mend the slipway before the\r\nopen day in April.\r\n' > $HD/EDWORD/NOTES.TXT
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $HATARI --tos $TOS4 --machine falcon \
        --memsize 14 --monitor rgb --harddrive $HD --gemdos-drive C --auto 'C:\EDWORD\EDWORD.PRG' \
        --fast-forward on --sound off --confirm-quit off --statusbar off --log-level error &
    P=$!
    i=0
    while [ $i -lt 400 ] && [ ! -f $HD/EDWORD/SHOTS/DONE ]; do sleep 1; i=$((i+1)); done
    kill $P 2>/dev/null || true
    [ $MODE = 0B ] && rm -rf manual/shots/$L
    python3 manual/shot2png.py $HD/EDWORD/SHOTS manual/shots/$L
    done
    U=$(echo $L | tr a-z A-Z)
    python3 manual/mkmanual.py $L manual/MANUAL$U.PDF
done
