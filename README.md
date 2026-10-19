# TeXAspell

A British English spell checker for macOS that understands LaTeX. It plugs
into the system spell checker, so it works in TeXShop (and any other app that
uses the standard macOS spelling panel), and it:

- checks your prose against **aspell**'s British English dictionary
  (`en_GB-ise` by default, so *organise* is right and *organize* is flagged);
- checks **LaTeX command names**: `\sectoin`, `\txtbf` and `\alpah` are
  flagged, and right-clicking offers `\section`, `\textbf` and `\alpha`;
- checks **environment names**: `\begin{itemzie}` is flagged and `itemize`
  is suggested;
- checks **option names** in `\hypersetup`, `\geometry`, `\captionsetup`,
  `\lstset`, `\sisetup` and `\setlist`: `pdftitel` is flagged and `pdftitle`
  suggested. The text of `pdftitle`, `pdfauthor`, `pdfsubject` and
  `pdfkeywords` is spell-checked as prose;
- leaves alone what isn't prose: maths, labels, citation keys, file names,
  URLs, verbatim and code listings, comments, `key=value` options, column
  specifications, TikZ code, and so on.

Command names are checked against what your TeX installation actually
defines. TeXAspell reads the LaTeX kernel, your document class and every
package you `\usepackage` (and the packages they load), so `\toprule` is
accepted once you use `booktabs` and `\SI` once you use `siunitx`. Commands you
define yourself with `\newcommand`, `\def`, `\DeclareMathOperator` and so on
are accepted too.

> **Revision 0.2:** the command-line checker, native macOS callbacks and
> regression suite have been built and tested on macOS. End-to-end TeXShop
> registration and typing behaviour still need an integration check. See
> [CHANGES.md](CHANGES.md) for the fixes and migration notes.

## Requirements

- macOS 11 (Big Sur) or later.
- Apple's Command Line Tools: `xcode-select --install`. You don't need the
  full Xcode or an Apple developer account.
- [Homebrew](https://brew.sh), then `brew install aspell`.
- A TeX distribution such as MacTeX (which includes TeXShop). TeXAspell finds
  it through `/Library/TeX/texbin/kpsewhich`.

Check that aspell has the British dictionary:

```sh
aspell dicts | grep en_GB
```

You should see `en_GB-ise` among the results.

## Build and install

```sh
git clone https://github.com/drtjstone/texaspell.git
cd texaspell
make            # builds the command-line checker
make test       # runs the tests; all should say "ok"
make install    # builds and installs the spell service
```

`make install` puts `TeXAspell.service` in `~/Library/Services` and the
`texaspell` command-line tool in `~/.local/bin`.

## Using it in TeXShop

1. Open a `.tex` file in TeXShop.
2. Choose **Edit › Spelling and Grammar › Show Spelling and Grammar**.
3. In the language pop-up menu at the bottom of the panel, choose the British
   English entry marked **TeXAspell**.
4. Turn on **Edit › Spelling and Grammar › Check Spelling While Typing**.

Misspelt words and unknown commands get the usual red underline.
Right-click for suggestions. **Learn Spelling** adds a word to your own list.
On a command such as `\mymacro`, it adds the command instead.

If TeXShop has a setting of its own for skipping TeX commands when
spell-checking, turn it off. Otherwise TeXAspell may never see the commands.

## Languages in your document

TeXAspell follows the languages your document declares:

- **Main language.** It's taken from babel's options, for example
  `\usepackage[german,british]{babel}`. Following babel's own rule, that's
  `main=...` if you give it, otherwise the last language listed. Languages
  given as `\documentclass[british]{article}` options also count, as does
  polyglossia's `\setmainlanguage[variant=british]{english}`. With no babel
  or polyglossia, the main language is British English, or whichever
  language you chose in the Spelling panel.
- **Passages in other languages.** `\begin{otherlanguage}{german}`,
  `\foreignlanguage{french}{...}` and `\selectlanguage{...}` are checked
  with that language's dictionary, and so are polyglossia's
  `\textgerman{...}` and `\begin{german}`. Suggestions come from the same
  dictionary.
- **No dictionary.** Text in a language with no aspell dictionary installed
  (or a language TeXAspell doesn't recognise) is skipped rather than flagged.
  To check French passages, for example, `aspell dicts` must list `fr_FR`.
  You don't need to add French to `LANGUAGES` below.

Two details:

- babel's `english` follows your default (British unless you choose
  otherwise), because many British documents use it. Use `british` or
  `american` to be explicit.
- While you type, macOS may send only a fragment without its preamble.
  A language declaration applies only to the current request; the service
  does not assume the fragment belongs to the last document it checked.
  For a fragment with no declaration, the language selected in the spelling
  panel is the default. Language switches inside the supplied text are
  still respected, including restoration after an environment ends.

## Choosing a default language in the Spelling panel

British English is the default, but the service can offer other languages
as well. Each one appears as its own TeXAspell entry in the Spelling panel's
language menu, and you switch between them there whenever you like. The LaTeX
checking works the same in every language.

Choose which languages to offer when you install:

```sh
brew install aspell        # includes many languages; check with: aspell dicts
make install LANGUAGES="en_GB en_US de_DE fr_FR"
```

Each name must match an aspell dictionary (`aspell dicts` lists them). Any
language without a dictionary is skipped with a warning. To change the
list, run `make install` again with a new `LANGUAGES`. Your own word lists
are shared by all languages.

The command-line tool takes `--lang`, for example
`texaspell --lang de_DE -s brief.tex`.

## Settings

TeXAspell keeps plain-text files in
`~/Library/Application Support/TeXAspell/`:

| File | What it is |
|---|---|
| `config.txt` | British dictionary (`en_GB-ise`, `en_GB-ize` or `en_GB`), dictionaries for other languages, whether to skip ALL-CAPS acronyms, and other options. |
| `packages.txt` | Packages whose commands are always accepted. Add packages whose commands should also be accepted in fragments without a preamble. This list is no longer populated automatically. |
| `commands.txt` | Your own commands (`\mymacro`), environments (`keypoint`) and option names (`myoption=`). **Learn Spelling** on a flagged command, environment or option adds it here. |
| `auto-commands.txt` | Legacy file. No longer read or populated automatically. Move deliberately trusted entries to `commands.txt`. |
| `words.txt` | Your own words. **Learn Spelling** adds them here. |
| `recent-documents.txt` | The packages and definitions of recently checked full documents, used for partial text (see *Partial text* below). Written automatically; safe to delete. |

After editing these files, restart the service with `pkill TeXAspell`. macOS
starts it again the next time an app needs it.

## Command-line checker

The same engine is available in Terminal. You can also run it from a TeXShop
macro:

```sh
texaspell -s chapter1.tex
```

```text
chapter1.tex:23:1: command: \sectoin -> \section
chapter1.tex:27:6: environment: itemzie -> itemize
chapter1.tex:19:4: spelling: recieve -> receive, receiver, relieve
```

`texaspell --help` lists the options.

## How it works, and its limits

- **Partial text.** For check-as-you-type, macOS often sends the spell
  checker only the paragraph or lines being edited. In LaTeX source, that is
  often a single line. TeXAspell cannot always tell what surrounds that text:
  - If the text contains `\end{align}` (or another maths or verbatim
    environment) before any `\begin`, it assumes the text starts inside that
    environment.
  - Text containing `\documentclass` is checked as a whole document: only
    its own packages and definitions (and those of its included preambles)
    count. TeXAspell then remembers what that document loaded and defined.
  - Text without `\documentclass` (a line or paragraph being typed) also
    accepts the packages and definitions of the last few documents checked
    in full (8 by default, kept in `recent-documents.txt`). So once a
    document has been checked in full, by opening it or with **Check
    Document Now**, typing `\ac{NDA}` or a `\newcommand` of its own is no
    longer flagged. The service cannot tell which document a fragment comes
    from, so a fragment of one document may accept a macro that only another
    recent document defines. Set `remember_documents = 0` in `config.txt` to
    turn this off; then use **Learn Spelling**, `commands.txt` or
    `packages.txt` instead.
  - A line of setup options on its own, such as
    `colorlinks=true, linkcolor=blue,` from inside a multi-line
    `\hypersetup{...}`, is recognised when its names are options of a setup
    package in context (hyperref, geometry, caption, listings, siunitx,
    enumitem). Its names are not spell-checked as words. A known name
    followed by ordinary text (`width = the value`) is still prose.

  A line in the middle of a long `align` with neither a `\begin` nor an `\end`
  may have short variable names checked as words. For a whole-document check,
  use **Edit › Spelling and Grammar › Check Document Now**.
- **TeX accents.** Words written with TeX accents (`caf\'e`, `na\"ive`) are
  skipped, not checked. Words typed with real accented characters (`café`)
  are checked normally.
- **Generous command list.** A command counts as known if a loaded file
  defines it or at least two loaded files mention it. The configured default
  packages are accepted even if the current document does not load them.
  This is a spelling aid, not a replacement for compiling the document.
- **Option names** are read from the package source in the same way. A few
  packages generate some option names in ways that can't be read directly.
  TeXAspell knows about the ones in hyperref and listings. Keys and
  suggestions are limited to the setup package and its dependencies; this
  is not complete key-family validation. If a valid option is flagged,
  use **Learn Spelling**. Options are only checked once their
  package is loaded, and only inside the six setup commands listed above.
  `\usepackage[...]` options aren't checked.
- **Single letters** and words in ALL CAPITALS (with an optional final *s*,
  as in *MPs*) are ignored. You can change both in `config.txt`.

## First run on a Mac

The macOS mechanism TeXAspell relies on (`NSSpellServer`) is still
documented by Apple. However, the last tool that used it, cocoAspell, is
unmaintained, and users reported it failing on recent macOS versions. So,
after `make install`:

1. Check that `make test` passed. If it did, the engine works on your Mac.
2. Open TextEdit and look for the TeXAspell entry in the Spelling panel's
   language menu. If it's missing, log out and back in, then look again.
3. If it's still missing, or nothing gets underlined, open **Console**, search
   for `TeXAspell`, and note any messages. The service logs its dictionary and
   settings folder when it starts. Also run `texaspell --info`.

## Tests

`make test` builds the command-line checker and runs:

- **Correct-command documents.** `tests/fixtures/valid-commands.tex` uses
  around 600 correct commands, environments and options from the LaTeX
  kernel and about 50 popular packages (amsmath, mathtools, siunitx, tikz,
  pgfplots, hyperref, cleveref, natbib, booktabs, listings, algpseudocode,
  glossaries, fancyhdr and others). `valid-beamer.tex` does the same for
  beamer. TeXAspell must flag nothing in either file.
- **Typos.** `tests/typos.txt` lists over 200 realistic mistakes in command,
  maths, environment and option names (`\sectoin`, `\txtbf`, `$\alpah$`,
  `\begin{itemzie}`, `\hypersetup{pdftitel=..}`, ...). Each must be flagged,
  with the intended name among the first five suggestions. Run
  `VERBOSE=1 sh tests/typos.sh` to see which corrections are not the first
  suggestion.
- **Sample documents.** These cover prose, maths, verbatim, fragments of
  documents, option lists and mixed languages, each compared with its
  expected output.
- **Behaviour checks.** These cover learning and forgetting words, configured
  packages, `-ise` spellings, acronyms, and choosing a language.

- **Stateful regressions.** `tests/regressions.c` checks repeated requests,
  prose containing equals signs, option-package separation, comments,
  definition lifetime, input edits and cycles, language restoration and
  learning-category ambiguity. CLI read errors are checked too.
- **Native callback regressions (macOS).** `tests/macos-regressions.m`
  checks system ignored words, both callback interfaces, UTF-16 ranges,
  offsets, command replacement backslashes and Learn/Forget callbacks.
  These tests do not install or register a spelling service.

Tests that need something you haven't installed say `skip` and why. For
example, the large documents need their packages, and `languages.tex` needs
the German and French aspell dictionaries. Anything else that isn't `ok` is
a real failure.

To add a typo test, add a line to `tests/typos.txt`. To check another
package, add commands that use it to `valid-commands.tex`.

## Troubleshooting

**`\textbf`, `\documentclass` or `\maketitle` are underlined.** That's
almost certainly not TeXAspell. TeXAspell knows every kernel command once it
has found your TeX installation. Apple's checker, by contrast, sees
`documentclass` as a misspelt word. Check that the language chosen in
**Edit › Spelling and Grammar › Show Spelling and Grammar** is the one
marked **TeXAspell**, not plain British English. Then, in Terminal:

```sh
texaspell --info               # "LaTeX kernel: found", thousands of commands known
texaspell -s yourfile.tex      # what TeXAspell itself reports for the file
```

If `texaspell` reports the file clean but TeXShop still underlines LaTeX
commands, TeXShop isn't using the service. Open **Console**, search for
`TeXAspell`, and check that the service started. It logs the same
information as `--info`.

**Commands from a preamble in a separate file are flagged.** TeXAspell reads
files you `\input`, `\include` or `\subfile` for the packages they load and
the commands they define. The command-line tool looks in the document's
folder and your TeX tree. The spell service isn't told where your document
is, so it finds such files only in your TeX tree (for example
`~/Library/texmf/tex/latex`) or in folders you list in `config.txt`:

```text
input_dirs = ~/Documents/LaTeX:~/Work/Preambles
```

Preambles are reread on each request in which their input is visible, so
new files, edits and deletions take effect without a service restart. A
canonical-path cycle guard and file-count limits bound recursive input.
If a later fragment has no input line, configure any needed packages in
`packages.txt` and deliberate custom commands in `commands.txt`.

Existing `packages.txt` files may contain entries automatically accumulated
by older versions. They are preserved; review them as an explicit allowlist.
Existing `auto-commands.txt` files are preserved but no longer loaded.

## Uninstall

```sh
make uninstall
rm -rf ~/Library/Application\ Support/TeXAspell
```

## Layout

```text
src/core/    the engine (portable C): LaTeX scanner, command database, aspell wrapper
src/cli/     the texaspell command-line tool
src/macos/   the NSSpellServer spell service
macos/       the service's Info.plist
tests/       test documents and the test script
```
