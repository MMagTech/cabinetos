#!/usr/bin/env python3
"""
Does a document name a file of this repository that is not there? (#398, step 4)

  ci/check-doc-paths.py

Reads every tracked `.md` file, at the top of the repository it runs in,
and prints each link and each path in backticks that names a file or folder
that does not exist, as file:line, and exits 1 if there is any; 0 when there
is none. .github/workflows/checks.yml runs it on every pull request; on GitHub
each line is also marked on the pull request. REPORT ONLY UNTIL A DATE: while
today (UTC) is not past REPORT_ONLY_UNTIL (YYYY-MM-DD, set by checks.yml),
they are warnings and it exits 0.

WHY. A doc that names a file which moved reads exactly like one that is right
(docs/lessons/README.md, rule 8), and the restructure of #398 moved most of
the frontend. The whole tree is read, not only what a pull request changes,
so a pull request that moves or deletes a file fails until the docs that name
it are put right.

WHAT IS READ:
  * a link, `[text](target)` or `[name]: target`, that is not a web address
    or an anchor: the target, from the doc's own folder (from the top of the
    repository when it starts with `/`), must be there;
  * a path in backticks that starts with one of this repository's top-level
    folders (`frontend/src/app.h`, `docs/wiki/`), with a line number after a
    colon allowed, must be there. Any other path in backticks is not read: a
    bare name (`romm.h`) or a path from elsewhere (`bios/dc/`, a file on the
    console, an emulator's source) cannot be told from one of ours.
Code blocks are not read, and neither are docs/history/ (history as it was)
and frontend/third_party/ (vendored).

A PATH IS ALSO THERE when .gitignore covers it (a build output, such as
`cores/build/`), when it is the end of a path that is there (written from a
folder the sentence names, as docs/ARCHITECTURE.md's `frontend/` row writes
`tools/fake-nmcli/`), or when it is in CABINET below: a file of Cabinet's own
repository, which shares this one's folder names.
"""

import datetime
import os
import posixpath
import re
import subprocess
import sys
import urllib.parse

# Files in Cabinet's repository (github.com/MMagTech/cabinet), named by
# docs that compare with it. Each was checked there on 2026-10-17.
CABINET = {
    "docs/core-manifest.json",
    "docs/core-quality-pass",
    "docs/core-quality-pass-2026-08-17.md",
    "docs/licenses.md",
    "docs/native-in-game-saves.md",
    "docs/scope-native-offline.md",
    "docs/settled.md",
    "tools/build-core.sh",
    "tools/build-dolphin-mac.sh",
    "tools/build-pcsx2-mac.sh",
    "tools/build-ppsspp.sh",
    "tools/generate_cores_map.py",
    "tools/make_icon.swift",
    "tools/patch-dolphin-mac.py",
    "tools/patch-pcsx2-mac.py",
}

NOT_READ = ("docs/history/", "frontend/third_party/")

FENCE = re.compile(r"^\s*(```|~~~)")
# A code span opens and closes with the same number of backticks and never
# runs past a blank line, so one stray backtick spoils only its paragraph.
CODE = re.compile(r"(?<!`)(`+)(?!`)(.+?)(?<!`)\1(?!`)", re.S)
PARAGRAPH = re.compile(r"(?:[^\n]+\n?)+")
LINK = re.compile(r"\]\(\s*<?([^)\s>]+)>?(?:\s+[\"'(][^)]*)?\)")
REFERENCE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?([^\s>]+)>?", re.M)
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
LINE_NUMBER = re.compile(r":\d+(-\d+)?$")
NOT_A_PATH = re.compile(r"[\s*?<>{}\[\]$|=()'\",;:\\]")


def git(*args, stdin=None):
    return subprocess.run(["git", *args], input=stdin, capture_output=True,
                          text=True, check=True).stdout


def blank(text, start, end):
    """The text with [start, end) made spaces, newlines kept, so offsets and
    line numbers do not move."""
    return text[:start] + re.sub(r"[^\n]", " ", text[start:end]) + text[end:]


def main():
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    files = [f for f in git("ls-files", "-z").split("\0") if f]
    tracked = set(files)
    folders = set()
    for f in files:
        while "/" in f:
            f = f.rsplit("/", 1)[0]
            folders.add(f)
    top = {f.split("/", 1)[0] for f in files if "/" in f}

    def there(path):
        path = path.rstrip("/")
        return path in tracked or path in folders

    # (doc, line, what it names, the path it was read as)
    named = []
    docs = sorted(f for f in files if f.endswith(".md") and not f.startswith(NOT_READ))
    for doc in docs:
        with open(doc, encoding="utf-8") as fh:
            text = fh.read()
        here = posixpath.dirname(doc)

        # Code blocks out.
        lines = text.split("\n")
        inside = False
        for i, line in enumerate(lines):
            if FENCE.match(line):
                inside = not inside
                lines[i] = ""
            elif inside:
                lines[i] = ""
        text = "\n".join(lines)

        def line_of(at):
            return text.count("\n", 0, at) + 1

        spans = [m for p in PARAGRAPH.finditer(text)
                 for m in CODE.finditer(text, p.start(), p.end())]
        for m in spans:
            span = LINE_NUMBER.sub("", m.group(2).strip())
            if span.startswith("./"):
                span = span[2:]
            if NOT_A_PATH.search(span) or "/" not in span:
                continue
            if span.split("/", 1)[0] not in top or "/../" in "/" + span + "/":
                continue
            named.append((doc, line_of(m.start()), "`" + span + "`", span.rstrip("/")))
        for m in spans:
            text = blank(text, m.start(), m.end())

        for m in list(LINK.finditer(text)) + list(REFERENCE.finditer(text)):
            target = m.group(1)
            if SCHEME.match(target) or target.startswith("#"):
                continue
            path = urllib.parse.unquote(re.split(r"[#?]", target, maxsplit=1)[0])
            if not path:
                continue
            if path.startswith("/"):
                path = posixpath.normpath(path.lstrip("/"))
            else:
                path = posixpath.normpath(posixpath.join(here, path))
            named.append((doc, line_of(m.start(1)), "a link to " + target, path))

    missing = [n for n in named if not there(n[3]) and n[3] not in CABINET]
    if missing:
        asked = sorted({p for *_, p in missing} | {p + "/" for *_, p in missing})
        ignored = subprocess.run(["git", "check-ignore", "--no-index", "--stdin"],
                                 input="\n".join(asked) + "\n", capture_output=True,
                                 text=True).stdout.split("\n")
        ignored = {p.rstrip("/") for p in ignored if p}
        missing = [n for n in missing if n[3] not in ignored]
    missing = [n for n in missing
               if not any(f.endswith("/" + n[3]) for f in files)
               and not any(d.endswith("/" + n[3]) for d in folders)]

    until = os.environ.get("REPORT_ONLY_UNTIL", "")
    today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    level = "warning" if until and today <= until else "error"
    on_github = bool(os.environ.get("GITHUB_ACTIONS"))
    for doc, line, what, _ in missing:
        print(f"{doc}:{line}: {what}, which is not in the repository")
        if on_github:
            print(f"::{level} file={doc},line={line}::{what}, which is not in the "
                  "repository. Name the file where it is now, or see ci/check-doc-paths.py.")
    if not missing:
        print(f"Every file the docs name is there ({len(named)} read in {len(docs)} docs).")
        return 0
    print(f"{len(missing)} name(s) in the docs of files that are not there.")
    if level == "warning":
        print(f"Report only until {until} (UTC): passing. From the next day this fails.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
