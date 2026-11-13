EdWord 3.0.14 for PC GEM (FreeGEM / OpenGEM)
============================================

EdWord, the GDOS word processor for the Atari ST, running under PC GEM on
DOS. It is the Atari version's own front end and core, built as a 32-bit
DJGPP program, so it has the Atari version's features and menus.

You need
--------
- a 386 or later PC (or DOSBox-X), with a 640-pixel-wide GEM screen
- FreeGEM or OpenGEM
- a DPMI host: CWSDPMI.EXE (included) beside EDWORD.APP or on the PATH
  (or Windows' DOS box, or any other DPMI server)
- for outline fonts at any size: XGDOS for PC GEM
  (https://github.com/FrankBRetro/xgdos). Without it EdWord has GEM's own
  bitmap fonts.

Installing
----------
Copy the EDWORD folder anywhere on the PC (e.g. C:\EDWORD) and open
EDWORD.APP from the GEM desktop. Everything EdWord needs is in the
folder: the resource files (EDWORD.RSC for English, EDWORDDE, ES, FR, IT
and CS.RSC), the dictionaries (*.DIC) and the thesaurus (EDWORD.THS).
Preferences picks the language and the dictionary.

The manual for the Atari version (MANUALEN.PDF and the other languages,
in the Atari release) describes the PC version as well.

Known limits on PC GEM
----------------------
- Underline doesn't show on text in XGDOS's outline fonts (it does in GEM's
  bitmap fonts); being looked at.
- PDF export: text in GEM's bitmap fonts is left out of the PDF (outline
  fonts are fine).
- Colour themes colour the toolbars but not their text without XGDOS.
- Printing through GEM's printer drivers is untested.

Licence
-------
EdWord's source code is MIT licensed, Copyright (c) 2026 Francis C Bullen
(LICENSE.TXT); it is at https://github.com/FrankBRetro/edword (folder
pcgem for the PC GEM parts).

This PC GEM program is linked with John Elliott's GEM bindings for DJGPP,
which are based on Digital Research's GEM Programmer's Toolkit and are
under the GNU General Public License version 2; so EDWORD.APP as a whole
may be passed on only under the GPL version 2. The bindings' source:
https://www.seasip.info/Gem/Software/djgppgem.zip

The dictionaries and the thesaurus keep their own licences (COPYING.DIC,
COPYING.THS). CWSDPMI.EXE is Copyright (C) 1995-2010 Charles W Sandmann,
redistributed unmodified with its documentation (CWSDPMI.DOC). You have
the right to receive its source code and updates: they are at
http://www.delorie.com/djgpp/ (csdpmi7s.zip).
