# EdWord 3.0 - a GDOS word processor for the Atari ST

(also for macOS, the Amiga and Windows: see below)

Runs in ST medium and ST high resolution (and any mode 640 pixels wide or
more). Written in C for `m68k-atari-mint-gcc` + gemlib. There is no resource
file: menus and dialogs are built in code and lay themselves out, so they fit
in every language.

The full user documentation, with screenshots, is in `manual/MANUALEN.PDF`
(also `MANUALDE`, `MANUALES`, `MANUALIT` and `MANUALFR`).

## Installing
Copy `EDWORD.PRG`, its resource files (`EDWORD.RSC`, `EDWORDDE.RSC`,
`EDWORDES.RSC`, `EDWORDIT.RSC`, `EDWORDFR.RSC`), `dict/EDWORD.DIC`,
`dict/EDWORD.THS` and the manuals into one folder, with the spelling dictionaries for the other languages you want
(`EDWORDDE.DIC`, `EDWORDES.DIC`, `EDWORDIT.DIC`, `EDWORDFR.DIC`). Load GDOS first (SpeedoGDOS, FSMGDOS, NVDI, FontGDOS, AMCGDOS or
G+Plus); without it you only get the system font.

### Versions for faster processors
`EDWORD.PRG` runs on every Atari. For faster machines there are versions
compiled for their processor, used in its place (rename it to `EDWORD.PRG`
or keep its name; it finds its dictionaries the same way):

| Program | For |
|---|---|
| `EDWORD.PRG` | any ST, STE, Mega, TT, Falcon (68000 and up) |
| `EDW030.PRG` | 68030 (or 68020) without an FPU: Falcon, accelerated STs |
| `EDW030F.PRG` | 68030 with a 68881/68882 FPU: TT, Falcon with FPU |
| `EDW040.PRG` | 68040 or 68060 with its FPU: CT60/CT63, Milan, Hades |
| `EDWCF.PRG` | ColdFire V4e: FireBee |

Each one checks the processor (and FPU) as it starts; on one it can't run on
it says so in an alert and quits, instead of crashing. EdWord's work is in
whole numbers, so the gain is in the faster instructions rather than the
FPU.

## New in 3.0
- **One Font menu** with the fonts and the point sizes together; Style menu
  for plain/bold/italic/underline and colour.
- **Spelling as you type** (Tools menu or Preferences): unknown words get a
  wavy underline; hold the left button on one for suggestions, Ignore All and
  Add to dictionary.
- **Find and Replace** in a non-modal window: match case, whole words,
  backwards, Replace, Replace All; F3 finds again.
- **Line spacing** single, 1.5 and double; **indents** and **hanging
  indents** (a tab in the first line moves to the indent).
- **Three views** per window: page view with styling, split view with the
  formatting codes, and full-window *Quick Edit* of the codes in the system
  font. Codes now cover spacing, indents, lists, headings, contents, tables
  and column breaks.
- **Thesaurus** (Shift+F7) from Open English WordNet: 31,000 headwords,
  meanings by part of speech, synonyms, base-form lookup, replace in place.
- **Window menu**: page/split/quick views, zoom, toolbar, tile side by side,
  tile stacked, cascade, iconify, send to back, next window, window list.
- **A View menu** with the views, zoom, toolbar, rulers and guides, so the
  Window menu is short enough for TOS 1-4: their AES saves the screen
  under a drop-down menu in a buffer a quarter of the screen's size and
  doesn't check it, and the long Window menu hung or crashed TOS 4 in 256
  colours. On TOS 1-4 the Font and Window menus list as many fonts and
  windows as that buffer allows (More fonts.../More windows... for the rest).
- **Zoom** 50-200 % in the page view, from the toolbar or F7/F8, and **Fit
  Page Width** (View menu, the toolbar's zoom popup, Shift+F8 on the ST):
  the page fills the window's width and follows it when the window changes
  size.
- **Multi-level undo and redo** (100 steps; Undo key, Shift+Undo), covering
  text, formats, tables, page settings, pictures and drawings.
- **Bullet and numbered lists** (F9, F10).
- **Tables**: insert, Tab/Return navigation, wrapping cells, grid lines,
  table settings (rows, columns, column widths), insert/delete rows and
  columns, delete table.
- **Margins per document and per page** (this page, or this page onwards).
- **Multi-column pages**: 1-4 columns per page with a gap, column breaks.
- **Table of contents**: mark headings 1-3, insert and update the contents
  with dot leaders and page numbers.
- **Document summary**: pages, words, characters, paragraphs, lines, tables,
  pictures.
- **Version in the About box**, with the version and the sentence under it
  settable from make: `make ED_VERSION=3.1 ED_ABOUT="Built for our club"`.
- **Five languages** for the user interface and the manual: English, German,
  Spanish, Italian, French. The TOS language is used at first; Preferences
  changes it. Spelling and thesaurus stay British English.
- **Resource files (ST)**: the menu bar, the Print and Preferences dialogs,
  the font selector, the toolbar with its icons and the size, colour and
  zoom popups come from `EDWORD.RSC` (one file per language,
  `EDWORDDE.RSC` and so on), so they can be changed with a resource editor
  and are drawn and handled by the AES. Without the files, or built with
  `make RSC=0`, EdWord builds them itself. With an AES that has 3D objects (TOS 4,
  MultiTOS, MagiC, XaAES, MyAES) the dialogs, the font selector and the
  toolbar take the 3D look either way.
- **Pictures placed to the pixel**: positions and sizes are kept in twips
  (1/20 point), so a picture or drawing lands on the screen pixel you drop
  it on at any zoom; Shift+arrow keys move a selected one by one pixel.
- **Drawings**: boxes, rounded boxes, ellipses, lines, triangles and
  freehand strokes in a drawing window, with fill, line width and colour; the text flows around the
  box or around the shapes themselves.
- **Pictures**: GEM IMG (mono, colour, XIMG, and true colour: 16, 24 and
  32 planes), PNG (every kind, interlaced too; transparent parts on white)
  and IFF ILBM/PBM (1-8 planes, EHB, HAM6/HAM8, 24-bit), including Atari ST
  IFF files from Deluxe Paint ST (its vertical VDAT compression and RAST
  palette changes) and colour maps that use only the ST's 3 or 4 bits a
  colour. Move, resize and flow text around them. On screens of 15 bits or
  more (Falcon high colour, NVDI/fVDI graphics cards) and on the Mac they
  show in full colour; on 4, 16 and 256-colour screens (ST medium, TT,
  Falcon) they are dithered to the screen's colours when there are 2 MB or
  more free, and shown in black and white on smaller machines. Colour PDFs
  carry them in full colour.
  Colours are kept in the RTF.
- **Charts** of a table's numbers: pie, bar, 3D pie and 3D bar, above,
  below, left or right of the table. Click the chart (or use Insert ▸
  Chart…; on the Mac also right-click the table) for a menu of the kind and
  place; on the Amiga a click opens a small dialog. The rows are the
  categories, the columns of numbers the series; a first row of words or
  years names the series and a first column of words the categories.
  Charts follow the numbers as you type, print, and go into the PDF.
- **Spelling in five languages**: German, Spanish, Italian and French
  dictionaries besides English (Preferences ▸ Dictionary). The checker
  accepts German compound words made of two known words and French and
  Italian elided forms (l'homme, dell'anno), and suggests words with the
  language's accented letters.
- **Pictures on the clipboard**: on the ST, Copy or Cut on a selected
  picture or drawing writes `SCRAP.IMG` (24-plane true colour when the
  picture has its colours, as Papyrus reads it), and Paste inserts a
  `SCRAP.IMG` that is newer than the clipboard's text. On the Mac pictures
  go to and come from the clipboard as images, and on Windows as bitmaps
  (CF_DIB).
- Everything new is saved in RTF (standard RTF where there is one, EdWord's
  own groups otherwise) and appears in the print preview and the PDF.
- **File ▸ Import… and File ▸ Export…** each open a window that stays
  open while you work (a floating panel on the Mac). Choose the format —
  RTF, Text, Markdown, 1st Word Plus, Protext, or PDF for export; Automatic
  for import — and its options, then *Import…* or *Export…* asks for the
  file. Options that don't apply to the chosen format are greyed out.
  - Export writes a copy of the document used last, which the window names;
    the document keeps its own name. Text has line ends and character set;
    PDF (ST and Amiga) has the resolution.
  - Import has the character set and line handling for plain text, and
    opens the file in a new window or puts it into the current document at
    the cursor.
  - Open still reads every format, recognising each by its contents.
- **Protext** import and export (Arnor's word processor for the CPC, PCW,
  ST, Amiga and PC): bold, italic and underline; hard and soft returns;
  justified paragraphs (soft spaces); centred lines (`>CE`); the left
  margin from the ruler; line spacing (`>LS`); headers and footers with
  the page number (`>HE`/`>FO` with `%`); and page breaks (`>PA`). Other
  stored commands and comments are skipped. A CPC file's AMSDOS header is
  recognised. Export uses the extension `.PRO`.
- **Markdown** import and export (`.md`): headings, bold/italic, lists,
  quotes, code blocks, pipe tables, rules and links come in as EdWord
  formatting; export writes the same back. Open and Import detect it.
- **1st Word Plus** import and export (`.DOC`): bold, italic and underline,
  the ruler (paragraph width and justification), headers, footers, page
  breaks, stretch spaces and soft line ends.
- **Printing** from the print preview's new Print button (and File ▸ Print):
  page range and copies. On the ST it prints through the GDOS printer driver
  (device 21), about eight times faster than before, with a Cancel button in
  the progress window (or Esc/Undo), the busy bee only over EdWord's
  windows, and in colour on a GDOS colour printer driver (8 pens or more):
  the Print dialog chooses Colour or Black and white, and colour pictures
  print in colour too, dithered to six inks and black (pure inks in pens of
  their own with 16 pens or more); on the Mac through the standard print dialog; on the Amiga
  through `printer.device` with the Preferences printer; on Windows through
  the Windows Print dialog.
- **Page navigation**: Previous page, Next page and Go to page… in the Edit
  menu (Mac: View menu), with keys on every platform.
- **Repaginate** (Edit menu, F2; Mac: View menu, ⌥⌘R) lays every page out
  again and brings the table of contents' page numbers up to date.
- **Page breaks**: Insert ▸ Page Break, Control+Return (Ctrl+Enter on
  Windows, ⌘Return on the Mac) and a new toolbar button; Control+Shift+Return
  breaks to the next column.
- **Page numbers** (Insert ▸ Page Numbers…): top or bottom of the page, left,
  centre or right, as `1`, `Page 1`, `Page 1 of 3` or `- 1 -` (in the
  interface language), on the first page or not; Remove takes it away. It
  fills in the header or footer, which Header and Footer can still edit.
- **Printing a page range** (ST and Amiga): all pages, the current page, from
  … to …, or a list such as `1-3, 5, 8-`, and the number of copies. Windows
  and the Mac use their own Print dialogs, which have pages and copies.
- **Paragraphs kept whole across pages** (ST): no widows or orphans (a lone
  first or last line of a paragraph on a page), headings kept with the text
  under them, and short paragraphs moved whole to the next page. Preferences
  ▸ More… chooses: Anywhere, No widows/orphans or Keep paragraphs together
  (the default).
- **Blinking caret** (ST): the text cursor flashes on and off (XOR), and stays
  on while you type or click.
- **Busy bee only over EdWord** (ST): during long work the pointer is the bee
  over EdWord's windows and the arrow anywhere else.
- **New from Template…** (ST, File menu): a UK letter (A4, your address and
  the date on the right), a US block-style letter (US Letter) or a newsletter
  (big title, issue and date, contents, then two columns from page 2 with
  page numbers). Your name and address are remembered for next time.
- **Untouched Untitled window closes** (ST) when you open a document or make
  one from a template, as long as nothing was typed in it.
- **Documents folder** (ST, Preferences ▸ More…): where the file selector
  starts; `C:\DATA\DOCPROC\EDWORD` at first, made if it isn't there (and
  only on a drive the machine has).
- **Unicode text and Markdown**: import and export as UTF-8 or UTF-16 (with a
  byte order mark, read in either byte order); characters the ST can't show
  become the nearest ones it can.
- **Rulers and guides**: rulers along the top and left of the page view and
  dotted margin/column guides on the page, each switched on and off from the
  toolbar or the menu (remembered in Preferences). Double-click a ruler for
  Indents (top) or Page layout (left).
- **ST: dialogs and progress bars in windows.** Every dialog and gauge is a
  movable window; Undo/Esc cancels, Alternate + the underlined letter picks a
  button. Only one dialog at a time; asking for another brings the open one
  to the front. If the AES has no free window, the old modal dialog is used.
- **Progress bars (ST, Amiga and Windows)** for opening, importing, saving and
  exporting documents, loading pictures and loading a dictionary. The bar
  only appears when the job takes more than a third of a second.
- **ST: window positions are remembered** for the session: a document,
  dialog, tool window or the preview reopens where it was last closed.
- **ST: 3D dialogs.** Under an AES with 3D objects (TOS 4, MagiC, XaAES,
  N.AES, MyAES) dialogs use the 3D flags, and MagiC's object extensions
  (`WHITEBAK` check boxes and round radio buttons, underlined shortcut
  letters) where `appl_getinfo(13)` reports them.
- **ST: font selector** shows any number of fonts (scroll bar, Home, type a
  letter to jump). It is an AES object tree: its buttons, arrows, scroll bar
  and check boxes are the AES's own, with its look and its feedback.
- **ST: popup menus** (the toolbar's font, size, colour and zoom lists, the
  chart menu) are the AES's own `menu_popup` when the AES has one (AES 4,
  MagiC, XaAES, MyAES, N.AES): its look, keyboard navigation and scrolling.
  Older AESes get EdWord's own popup.
- **ST: faster scrolling**: when the window is on top and nothing covers
  it, scrolling moves what is on screen with `vro_cpyfm` and draws only the
  strip that comes into view (sliders, arrows, cursor keys, dragging a
  selection). If the `FONTSELECT` environment variable names a running
  font selector, EdWord uses it through the font protocol (`FONT_SELECT` /
  `FONT_CHANGED`).
- **ST: PDF export under memory protection** (MiNT/XaAES, NVDI) no longer
  crashes: bitmap font data is read in supervisor mode.
- **Up to 32 documents** open at once (the AES window limit is respected);
  the Window menu lists as many as fit and More windows… lists the rest.

### Menus and keys follow each platform
**Atari ST** (Atari's GEM conventions): the first menu is *EdWord*.
| Key | Command | Key | Command |
|---|---|---|---|
| ^N / ^O | New / Open | ^U | Close window |
| ^S / ^⇧S | Save / Save as | ^W | Next window |
| ^P / ^⇧P | Print / Print preview | ^Q | Quit |
| ^F | Find and Replace | ^G or F3 | Find again |
| ^↑ / ^↓ | Previous / Next page | ^J | Go to page |
| ^B / ^I / ^⇧U | Bold / Italic / Underline | ^⇧F | Font selector |
| ^L / ^E / ^R / ^⇧J | Left / Centre / Right / Justify | ^⇧R | Rulers |

Changed from earlier versions: Close was ^W, Next window ^Tab, Find ^G,
Justify ^J, Underline ^U, Print preview ^P.

**Amiga** (Amiga User Interface Style Guide): the first menu is *Project*
with Right Amiga N, O, S, A (Save As), P (Print), W (Close), ? (About) and
Q; Edit has Z, Y, X, C, V, F (Find) and G (Find again). Cursor keys: Shift
goes to the line's ends or a windowful up/down, Alt by words and by pages,
Ctrl to the document's ends, and **Shift + Alt extends the selection**
(it was Shift before).

**Windows** (the usual Windows keys): File, Edit, Insert, Font, Style,
Paragraph, Tools, Window and Help, with Ctrl N, O, S, Shift+S (Save As),
P, W (Close), Z, Y, X, C, V, A (Select All), F (Find), F3 (Find again),
G (Go to page), B, I, U, L, E, R, J, T (Font selector), K (Spelling),
Ctrl + / Ctrl − (zoom) and Alt+F4 (Exit). Cursor keys: Home/End, Page Up/
Down, Ctrl by words and to the document's ends, Ctrl + Page Up/Down by
pages, and **Shift extends the selection**. The right mouse button opens
a menu: spelling suggestions on a misspelt word, Cut/Copy/Paste/Select
All on text, Properties/Delete on a picture or drawing.

**macOS** (Apple's Human Interface Guidelines): EdWord, File (Import… ⇧⌘I,
Export… ⇧⌘E), Edit, Insert, Format (Font, Colour and Text submenus),
View, Tools, Window and Help (which opens the manual). View has Rulers ⌘R,
Guides, zoom, Previous/Next page ⌥⌘↑/⌥⌘↓ and Go to Page ⌥⌘G.

## Start-up log and safe start
Built with `make EDLOG=1`, EdWord writes `EDWORD.LOG` next to the program each
time it starts. A normal `make` leaves the logging out. The log covers:
- the AES version and the VDI workstation it opened;
- which GDOS was found, with the FSMC cookie for Speedo;
- every font-loading step and each font's name, id and outline flag;
- the first metric calculations for each font;
- the first text drawn;
- every step of a PDF export: the gauge, each band of each page, each
  line drawn, and for each new character the VDI outline call (before and
  after) or the bitmap font it came from, with the Line-A font list. Exporting
  allows another 4,000 lines of log, so start EdWord, export straight away,
  and send `EDWORD.LOG`. If EdWord crashes, the last line shows the step it
  was on.

Each line is written and the file closed again, so if EdWord stops the last
line shows how far it got.

Hold **Control** while EdWord starts to skip loading GDOS fonts entirely; it
then runs with the system font only. Hold **Alternate** to size outline
fonts with `vst_point` instead of `vst_arbpt`.

When EdWord measures a font for the first time, the log lists every VDI call
(`vst_font`, `vst_arbpt`, `vst_effects`, the width queries, `vqt_fontinfo`)
before and after it is made.

## SpeedoGDOS
With GDOS present, EdWord opens its screen workstation as device `Getrez()+2`,
as GDOS expects, rather than device 1. EdWord detects SpeedoGDOS/FSMGDOS (`vq_vgdos() == '_FSM'`). It then:
- reserves the scratch buffer (`vst_scratch`) before loading fonts;
- reports font errors to EdWord rather than writing them on the screen (`vst_error`);
- selects the Atari character map (`vst_charmap`), so accented letters print correctly;
- sizes outline fonts with `vst_arbpt`, so any point size works (Size ▸ Other…);
- measures outline fonts with `vqt_advance`, SpeedoGDOS's own width call;
- makes the `vqt_fontinfo` call itself, with full-size reply buffers. gemlib's binding has room
  for only the standard 2 + 10 words, and SpeedoGDOS returns more, which crashed EdWord 2.2;
- runs with a 64 KB stack;
- draws text word by word at its laid-out position, so outline-font rounding never builds up along a line.

## EdWord for macOS (`mac/`)
A Cocoa version built on the same core, so documents, layout, RTF, PDF,
spelling and the thesaurus behave exactly as on the ST.
```
cd mac && make               # EdWord.app (Apple silicon and Intel)
make dmg                     # EdWord.dmg to hand out
```
Needs macOS 11 or later, the Xcode command line tools and python3. The
dictionary and thesaurus are copied into the app bundle. Fonts are the Mac's
own (any installed font, any size); printing uses the standard Mac print
dialog, which also offers Save as PDF.

## EdWord for the Amiga (`amiga/`)
Two programs from the same source, on any 68000 or better:

- **EdWord** for AmigaOS 2.04 (V37) or newer: GadTools menus and gadgets,
  ASL requesters, diskfont fonts.
- **EdWord13** for Workbench 1.2 and 1.3 (Kickstart 33/34). It has all of
  EdWord's features; what 1.3 lacks (GadTools, asl.library, tag calls,
  public screens, EasyRequest) is done by `amiga/ami13.c`, with its own
  gadgets, menus, requesters and a file requester (Volumes, Parent, a
  pattern such as `#?.(rtf|txt)`). It needs 1 MB of memory or more, plus
  room for a dictionary if spelling is used.

**The screen:** by default EdWord opens its own 16-colour high-resolution
screen, not interlaced (640 × 256 on PAL, 640 × 200 on NTSC). *Preferences*
> *Screen* chooses instead: *High res interlaced*; *Like the Workbench* (the
Workbench's own mode and size); *Other mode...* (the screen mode requester:
any mode, size and 4, 8 or 16 colours; asl.library 38, Workbench 2.1, so not
on 1.3); or *Workbench screen*, where the documents open as windows on the
Workbench. The new screen is used the next time EdWord starts. On the
Workbench screen EdWord's colours are the nearest the Workbench has (on 3.0
and newer it asks for its own pens), and closing the last document quits
EdWord.

**Icons:** `amiga/EdWord.info` and `amiga/EdWord13.info` are the programs'
icons; `amiga/EdWordDoc.info` is a document icon that opens with EdWord
(copy it next to a document as *name*`.info`). They are in the format every
Workbench reads, from 1.2 on. `make icons` makes them again
(`tools/mkinfo.py`).

**Installing:** copy `amiga/EdWord` (or `EdWord13`) and its `.info` with
`dict/EDWORD.DIC` and `dict/EDWORD.THS` into one drawer. Preferences are
kept in `EdWord.prefs` in the same drawer; add `dict/EDWORD??.DIC` for
spelling in the other languages. Any Amiga bitmap font in `FONTS:` can be
used; sizes the font doesn't have are scaled. Pictures are IFF ILBM, PNG and
GEM IMG.

**Keys:** the menu shortcuts are Right Amiga + letter (see *Menus and keys*
above); F3–F10 as on the ST; Ctrl + Return inserts a page break; Esc closes
a dialog.

**Building** (with Bartman's m68k-amiga-elf GCC from the vscode-amiga-debug
extension, which brings the NDK headers and elf2hunk):
```
export PATH=<toolchain>/bin/linux/opt/bin:<toolchain>/bin/linux:$PATH
cd amiga && make             # EdWord
make AMI13=1                 # EdWord13, for Workbench 1.2/1.3
make icons                   # the .info files
make EDLOG=1                 # ... writing timings to EdWord.log
```
No C library is needed: `amiga/sys` holds the start-up code and the small
libc EdWord uses. Large local variables are kept static, as GCC 15 for the
68000 mis-addresses stack frames over 16 KB.

## EdWord for Windows (`win/`)
One 32-bit program, `win/EdWord.exe`, for **Windows 95 to Windows 11**
(also NT 4, 98, Me, 2000, XP, Vista, 7, 8 and 10; and Wine). It needs no
run-time library (not even msvcrt.dll): only the Windows API that every
version has, so the same file runs on all of them. 4 MB of memory is
enough, plus room for a dictionary.

- **Windows:** a frame window with the menus holds the documents (MDI):
  maximise one, tile or cascade them, minimise them. Find and Replace,
  Spelling, the Thesaurus, Import, Export, the font selector, print
  preview and the drawing window float above it.
- **Fonts:** every TrueType font installed. EdWord starts with Courier
  New, Arial and Times New Roman, then the rest in alphabetical order.
  Text is drawn at the positions the layout gives it, so the screen, the
  printout and the PDF match.
- **Printing** uses the Windows Print dialog (any printer, the pages,
  the copies) and prints at the printer's own resolution; *Export PDF*
  makes a PDF without any printer.
- **Clipboard:** Copy puts the text on the clipboard as Rich Text Format
  (other word processors paste it with its fonts and styles) and as plain
  text; a selected picture or drawing goes as a bitmap. Paste takes RTF,
  text or a bitmap (a picture) from other programs.
- **Files** can be dropped on the window from Explorer, or named on the
  command line (`EdWord.exe letter.rtf`).
- **Settings** are kept in `EdWord.ini` in `Application Data\EdWord` (on
  Windows 95, next to the program), with personal dictionaries and the
  auto-save copies of untitled documents.

**Installing:** copy `win/EdWord.exe`, `dict/EDWORD.DIC` and
`dict/EDWORD.THS` into one folder (for example `C:\Program Files\EdWord`),
with `dict/EDWORD??.DIC` for spelling in the other languages, and make a
shortcut to `EdWord.exe`. The user interface follows the Windows language
(English, German, Spanish, Italian or French) until another is chosen in
*Preferences*.

**Building** with clang and lld as a cross compiler (any platform) and the
mingw-w64 headers and import libraries:
```
cd win && make               # EdWord.exe
make W32INC=<mingw>/include W32LIB=<mingw>/lib     # if they are elsewhere
make EDLOG=1                 # ... writing timings to EdWord.log
make icon                    # win/res/edword.ico (from mac/icon.py; Pillow)
```
`win/sys` holds the start-up code and the small C library EdWord uses, on
the Windows API (memory from HeapAlloc, files on CreateFile).

## Building
```
make                         # EDWORD.PRG
make EDLOG=1                 # ... with the start-up log (EDWLOG.PRG in the package)
make ED_VERSION=3.1 ED_ABOUT="Built for our club"
                             # version number and About-box sentence
make EDDEMO=1                # ... that runs DEMO.SCR (manual screenshots)
make CPU=030                 # EDW030.PRG; also CPU=030F, CPU=040, CPU=CF
make cpus                    # all four (objects in build/<cpu>)
make RSC=0                   # menus and dialogs built in, not from EDWORD*.RSC
                             #   (make clean when switching)
make rsc                     # EDWORD*.RSC and edrsc.h from lang/strings.txt
                             #   (tools/mkrsc.py; needs Python 3)
make MKDIC.TTP               # dictionary builder for the ST
make hosttest                # PC: tests the portable core (layout, codes,
./hosttest test/sample.rtf /tmp dict/EDWORD.DIC dict/EDWORD.THS
                             #   RTF, text, tables, undo, PDF, thesaurus...)
```
The 68k versions link with the usual 68000 C library, so its start-up runs
on any machine and `cpucheck.c` (compiled for the 68000 in every build) gets
to check the processor; `EDWCF.PRG` needs MiNTLib and gemlib built for
ColdFire (`m5475`, MiNTLib's `WITH_V4E_LIB=yes`).

`lang.c` and `lang.h` are made from `lang/strings.txt` by
`tools/mklang.py` (Python 3), which also checks that alerts, buttons and
the menu bar fit. To add a language, add its lines to `strings.txt` and its
code to `LANGS` in `mklang.py`.

To use another word list, run `make mkdic` and then
`./mkdic words.txt dict/EDWORD.DIC`. The other languages' word lists are
made from hunspell dictionaries and wordfreq frequency lists with
`tools/mkhun.py` (see `dict/COPYING.DIC`). `test/mkpics.py` makes the test
pictures hosttest reads. The thesaurus is rebuilt from the Open
English WordNet YAML sources with `tools/mkthes.py`.

The manuals are made by `manual/build.sh`: it builds the demo version, runs
`manual/demo.scr` in Hatari under EmuTOS in each language to take the
screenshots, and typesets `manual/src/*.txt` with ReportLab
(`manual/mkmanual.py`).

## Files
| File | Purpose |
|---|---|
| `gem.c` | start-up, preferences, menus, commands, keys, event loop |
| `cpucheck.c` | the processor check of the faster versions (68000 code) |
| `gemwin.c` | document windows: views, zoom, toolbar, page and codes panes, editing, tiling |
| `gemdlg.c` | the self-laying-out dialog builder, popups, alerts and all dialogs |
| `gemtwin.c` | Find and Replace, Spelling and Thesaurus windows; the spelling popup |
| `gemobj.c` | pictures and drawings: selecting, moving, resizing, the drawing window |
| `gemtool.c` | font selector and print preview windows |
| `gemclip.c` | GEM clipboard, Insert > File, auto-save |
| `gemrend.c` | GDOS/Speedo fonts, metrics, page drawing, PDF glyphs |
| `gemicon.c` | toolbar icons |
| `demo.c` | the screenshot script (EDDEMO builds only) |
| `doc.c` | text model, paragraph formats, tables, page formats, objects |
| `layout.c` | lines, pages, columns, tables, text flow around objects, hit testing |
| `edit.c` | selection, typing, styles, clipboard, table editing |
| `undo.c` | multi-level undo and redo |
| `docops.c` | table of contents, find and replace, statistics |
| `codes.c` | formatting codes view: build, parse, wrap |
| `rtf.c`, `text.c`, `pdf.c` | file formats |
| `md.c`, `fwp.c` | Markdown and 1st Word Plus |
| `protext.c` | Protext; also finding a file's format, and reading or writing any format |
| `prt.c` | printing: renders pages in bands at printer resolution |
| `pagedraw.c`, `raster.c` | PDF page rendering, shapes |
| `img.c` | pictures: GEM IMG, PNG (with its own inflate), IFF; colours, dithering, IMG writing |
| `chart.c` | charts of a table's numbers (whole-number maths) |
| `spell.c`, `thes.c` | spelling checker, thesaurus |
| `charset.c` | ST / Windows-1252 / Unicode, letter case, colours |
| `lang/strings.txt` | every user interface string in five languages |
| `tools/` | `mkdic.c`, `mkthes.py`, `mklang.py` |
| `manual/` | manual sources, screenshots, builder, demo script and files |
| `mac/` | macOS version (Cocoa) |
| `amiga/` | Amiga versions (Intuition, GadTools; `ami13.c` for Workbench 1.3) |
| `win/` | Windows version (Win32 API and GDI, Windows 95 to 11) |
| `dict/` | `EDWORD.DIC`, `EDWORD.THS` and their licences |
| `LICENSE` | the MIT licence |

## Limits
- 32 documents at once (fewer if the AES runs out of windows); 255 fonts;
  255 paragraph formats, 48 tables, 32 pictures and
  drawings per document; 12 columns per table, 16 shapes per drawing, 4 text
  columns per page.
- At 1x a PDF is at screen resolution (about 70 dpi in high res); use 2x or 3x
  for sharper pages. Bitmap fonts in the PDF use the nearest size GDOS has
  loaded, as on screen.
- Zooming with bitmap fonts uses the nearest size that fits; outline fonts
  zoom exactly.
- Headers and footers in RTF files from other programs are not imported.
- 1st Word Plus pictures and footnotes are not imported; Markdown keeps
  only what Markdown can express (no fonts, sizes, colours or page layout).
- Protext uses the bytes of some accented letters (such as ä, é, ü) for
  its own codes, so EdWord writes those letters without their accents.
  Protext has no total page count, so a header's "of {pages}" is dropped.
  The Protext format was built from Arnor's published description and the
  manual, not tested against files from Protext itself.
- Charts read up to 24 rows and 6 columns of numbers. Their size is
  automatic. Black-and-white screens and PDFs tell the series apart by
  patterns.
- Pictures keep their colours only on screens of 15 bits or more on the ST
  (and on the Amiga with plenty of memory); a colour picture saved on such
  a machine still opens, dithered, everywhere else. EmuTOS doesn't answer
  `vq_scrninfo`, so there only the Falcon's own high-colour mode is used.
- The Amiga clipboard takes text only (no pictures yet).
- The manuals describe 3.0 before the changes listed from *Markdown* onward
  and will be updated once the app is finished.

## Licence
EdWord is free software under the MIT licence: Copyright (c) 2026 Francis C
Bullen. See `LICENSE` for the full text. Every source file carries a short
copyright line with `SPDX-License-Identifier: MIT`.

The word lists and the thesaurus in `dict/` are made from other projects and
keep their own licences (SCOWL; the German, Spanish, French and Italian
hunspell dictionaries under GPL, LGPL or MPL; Open English WordNet under
CC BY 4.0). They are separate data files EdWord reads at run time, listed in
`LICENSE` and given in full in `dict/COPYING.DIC` and `dict/COPYING.THS`.
