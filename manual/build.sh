#!/bin/sh
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
# build.sh - make the manual's screenshots in Hatari and the five PDF manuals
#
#   HATARI=/path/to/hatari ETOS=/path/to/emutos-256k-1.4 sh manual/build.sh
#
# Needs: the m68k-atari-mint cross compiler, Hatari, the EmuTOS 256k ROMs
# (one per language: etos256uk.img, etos256de.img ...), a host C compiler,
# Python 3 with Pillow and ReportLab, and the DejaVu fonts.
# Run from the EdWord source folder. Screenshots go to manual/shots/LANG,
# the manuals to manual/MANUAL??.PDF.
set -e
HATARI=${HATARI:-hatari}
ETOS=${ETOS:-.}
WORK=${WORK:-/tmp/edword-manual}
HOSTSRC="doc.c layout.c edit.c undo.c docops.c rtf.c pdf.c pagedraw.c raster.c img.c thes.c charset.c text.c codes.c spell.c md.c fwp.c protext.c chart.c prt.c"

mkdir -p $WORK
make clean >/dev/null
make EDDEMO=1
cc -O2 -I. -o $WORK/mkdemo manual/mkdemo.c $HOSTSRC

for L in en de es it fr; do
    R=$L; [ $L = en ] && R=uk
    HD=$WORK/hd_$L
    rm -rf $HD; mkdir -p $HD
    cp EDWORD.PRG dict/EDWORD.DIC dict/EDWORD.THS $HD/
    cp manual/demo.scr $HD/DEMO.SCR
    $WORK/mkdemo $HD/DEMO.RTF
    python3 manual/mkimg.py $HD/LIGHT.IMG
    printf 'Notes from the committee meeting\r\n\r\nThe meeting agreed to buy new oars\r\nand to mend the slipway before the\r\nopen day in April.\r\n' > $HD/NOTES.TXT
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $HATARI --tos $ETOS/etos256$R.img --machine st \
        --memsize 4 --monitor mono --harddrive $HD --gemdos-drive C --auto 'C:\EDWORD.PRG' \
        --fast-forward on --sound off --confirm-quit off --statusbar off --log-level error &
    P=$!
    i=0
    while [ $i -lt 300 ] && [ ! -f $HD/SHOTS/DONE ]; do sleep 1; i=$((i+1)); done
    kill $P 2>/dev/null || true
    rm -rf manual/shots/$L
    python3 manual/shot2png.py $HD/SHOTS manual/shots/$L
    U=$(echo $L | tr a-z A-Z)
    python3 manual/mkmanual.py $L manual/MANUAL$U.PDF
done
make clean >/dev/null
make
