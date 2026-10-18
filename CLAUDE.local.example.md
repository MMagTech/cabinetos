# Your machines (example)

Copy this file to `~/.claude/cabinetos.local.md` and fill it in. CLAUDE.md
loads it from there, so every session on your computer sees it, in every
worktree, and git never does. The first session after you create it asks you
once to allow the import.

It lives in your home folder, not in the repository, for two reasons: each
session works in its own copy of the repository, and a line added to
`.gitignore` counts as a change to the console image (build.yml's `paths`), so
it would make `main` build an update with nothing in it.

## The test console

| | |
|---|---|
| Address | `<console address>`, SSH port 2222 (Developer access on) |
| Key | `<path to your SSH key>` |
| Password | shown on the television when Developer access is on |
| Channel | `testing` or `latest` |

## Other test machines

| | |
|---|---|
| `<name>` | `<address>`: what it is for, and what it cannot answer |

## Your RomM server

`<address:port>`. Point `tools/romm-readonly-relay.py` and
`tools/offline-relay.py` at it (their `UP` line) before testing.

## Hand-made state on your machines, not in git

Folders and scripts you made by hand on the test console, and what each is
for.
