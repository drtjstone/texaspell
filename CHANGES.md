# TeXAspell 0.3

Fixes for the problems found in a review of 0.2. Each has a regression test
in `tests/regressions.c`.

- **Partial text knows its document again.** In 0.2, isolating each request
  meant that text typed without its preamble lost the document's packages
  and `\newcommand`s, so `\ac`, `\autocite`, `\SI` and the document's own
  macros were flagged while typing. TeXAspell now remembers the packages and
  definitions of the last few documents checked in full
  (`remember_documents = 8` in `config.txt`, stored in
  `recent-documents.txt`, kept across restarts) and accepts them in text
  without `\documentclass`. A full document is still checked against its own
  context only.
- **A `%` in a URL or inline code no longer stops checking.** `\href`'s URL
  and the code in `\lstinline`/`\mintinline` are now read verbatim, so
  `a%20b` or `printf("%d")` no longer hides the rest of the text as a
  comment.
- **Lone lines of setup options.** A line such as
  `colorlinks=true, linkcolor=blue,` typed inside a multi-line
  `\hypersetup{...}` is recognised as options when the names belong to a
  setup package in context, rather than only after a `pdf...` key.
- **Learn Spelling and suggestions after another check.** The category of
  a flagged name (word, command, environment, option) is remembered for the
  last 8 checks, not just the latest, so an intervening check-as-you-type
  request no longer turns an environment into a prose word.
- **Faster.** Each package's commands are read once per session and reused,
  so full-document checks take about half the time of 0.2.
- **`make install`** again replaces the command-line tool rather than
  overwriting it in place (which macOS answers by killing it) and re-signs
  it.

# TeXAspell 0.2 — reviewed revision

This revision keeps TeXAspell as a separate macOS spelling service using GNU
Aspell. It addresses the problems reproduced in the review of the supplied
0.1 source. It does not install itself or alter cocoAspell.

## Behaviour changes

- Ordinary `Result = teh mispellt` text is spell-checked. The option-fragment
  heuristic is restricted to PDF keys, with a small set of recognised
  continuation keys after PDF context has been seen.
- Both native checking callbacks consult the system learned-word and
  document ignore lists. Ignoring the first error does not hide subsequent
  errors. LaTeX names use case-sensitive dictionary lookups.
- Ordinary LaTeX environments restore the surrounding language on exit.
- Language declarations apply to the current request. A fragment without a
  declaration uses the selected language, not a different document's last
  preamble. The dominant orthography language is considered first.
- Included preambles are reread when their input line is present in a new
  request. Missing files are retried; changed and deleted definitions take
  effect. Canonical paths and existing limits bound recursive inputs.
- Definitions and additional packages inferred from a document apply to
  that request only. A separate copy of the configured base vocabulary
  prevents them from silently leaking into subsequent documents.
- Suggestions and Learn Spelling use categories from the latest request.
  A spelling encountered in conflicting categories defaults to prose,
  because the native callback does not supply its original token location.
- Setup-option checks and suggestions use the relevant package and its
  dependencies instead of the union of all loaded packages. For example,
  geometry's `margin` no longer makes it valid in `\hypersetup`.
- Comments are understood between commands and arguments and while matching
  braces/brackets. Literal percent signs in `\url`, `\path` and `\nolinkurl`
  remain supported.
- CLI read failures produce diagnostics and error status 2 rather than a
  clean result. Library file reads also reject I/O errors.
- Installation destination paths are quoted for home folders containing
  spaces. Service copying excludes extended attributes, so File Provider
  metadata from a Documents checkout is not carried into the installation.

## Existing settings and fragments

`words.txt` and `commands.txt` retain explicitly learned entries. Configured
packages and default classes remain accepted as the user's base vocabulary.

`auto-commands.txt` is preserved but is no longer automatically read or
populated. Review it and move only deliberately trusted names to
`commands.txt`. Existing `packages.txt` files may contain entries accumulated
by the old version; these are preserved and now function as an explicit
allowlist. No migration deletes user settings.

macOS can send a paragraph without the document's preamble or identity.
This service cannot reconstruct missing context reliably. A valid custom
command in such a fragment may need Learn Spelling or an entry in
`commands.txt`; a package may need an entry in `packages.txt`. Full-document
checks and visible `\input` lines provide more context. A previous check is
not treated as proof that a later fragment belongs to the same document.

Option extraction still reads package source rather than executing TeX.
The package's dependency closure is narrower than the old global key list,
but does not provide complete key-family or semantic validation. Compiling
the document remains the authoritative check of LaTeX validity.

Native suggestions and learning still lack document/token identity. When a
new checking request intervenes before a callback, only that latest context
is available; ambiguous names default to prose. Fully contextual behaviour
would require integration with an editor that can supply document identity
and surrounding text.

## Build, test and install

```sh
make
make test
make service
```

The expanded `make test` runs the original fixtures and 227 typo cases,
stateful engine regressions, a CLI read-error check and, on macOS, direct
native callback regressions. The native tests do not register a service.

The archive includes source plus a service and CLI built locally on the Mac
used for this revision. Those binaries depend on that Mac's Homebrew Aspell;
they are not a self-contained universal distribution. Rebuild from source
on another Mac. The plist's minimum OS value is not a claim that this local
binary has been tested on all earlier macOS releases.

When ready to replace an existing TeXAspell installation:

```sh
make install
```

Use the same `LANGUAGES="..."` selection as your existing installation if it
has more than British English. The default is `en_GB`. Select the TeXAspell
entry in the spelling panel afterwards.

End-to-end registration and check-as-you-type behaviour inside TeXShop have
not been tested as part of this revision. Verify that integration before
relying on the service for everyday use.
