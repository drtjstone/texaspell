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

> **Status: written and tested on Linux, not yet tested on a Mac.** The
> checking engine and command-line tool pass their tests. The macOS spell
> service has not been built or run yet. See [First run on a
> Mac](#first-run-on-a-mac).

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

## Other languages

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
| `packages.txt` | Packages whose commands are always accepted. TeXAspell adds a package here the first time it sees you load it. |
| `commands.txt` | Your own commands (`\mymacro`), environments (`keypoint`) and option names (`myoption=`). **Learn Spelling** on a flagged command, environment or option adds it here. |
| `auto-commands.txt` | Commands and environments TeXAspell has seen defined in your documents. Safe to edit or delete. |
| `words.txt` | Your own words. **Learn Spelling** adds them here. |

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
  often a single line. TeXAspell can't always tell what surrounds that text,
  so it uses two rules of thumb:
  - If the text contains `\end{align}` (or another maths or verbatim
    environment) before any `\begin`, it assumes the text starts inside that
    environment.
  - Packages and `\newcommand`s it has seen once are remembered in the
    settings files. That way, a chapter checked without its preamble still
    knows your commands.

  A line in the middle of a long `align` with neither a `\begin` nor an `\end`
  may have short variable names checked as words. For a whole-document check,
  use **Edit › Spelling and Grammar › Check Document Now**.
- **TeX accents.** Words written with TeX accents (`caf\'e`, `na\"ive`) are
  skipped, not checked. Words typed with real accented characters (`café`)
  are checked normally.
- **Generous command list.** A command counts as known if it appears anywhere
  in the files of a loaded package. This occasionally accepts an internal
  command you would never type, but it reliably catches typos.
- **Option names** are read from the package source in the same way. A few
  packages generate some option names in ways that can't be read directly.
  TeXAspell knows about the ones in hyperref and listings. If a valid option
  is flagged, use **Learn Spelling**. Options are only checked once their
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
