#!/usr/bin/env python3
"""Hash object files so that two builds of the same source compare equal.

A plain sha256 of a .o is the obvious thing and it is wrong for any core built
with -flto, which picodrive is. GCC gives every LTO invocation a random id and
writes it into the section names it generates:

    .gnu.lto_.profile.3bda9114828bb356        first build
    .gnu.lto_.profile.ca2804fcf73ae283        second build

So every object differs between two builds of IDENTICAL source, by a few hundred
bytes of section-name string and nothing else. Found by running
cores/backend-diff.sh with the same setting on both sides, which reported 102 of
103 objects differing — including zlib's, which no CPU backend can reach.

**That control run is the point.** Without it the picodrive comparison would have
read as "the recompiler changes everything", which is the answer that would have
cost this core its shared emulator tag on RomM, for a reason that was never real.

The id is 16 hex characters, so replacing it with 16 zeroes keeps every offset in
the file exactly where it was and leaves the rest of the object byte for byte as
the compiler emitted it. Objects with no LTO sections are hashed untouched.

Reads paths on stdin, one per line, relative to the root given as argv[1].
Writes `<sha256>  <path>`, the same shape as sha256sum.
"""

import hashlib
import re
import sys

# The id as it appears in a section name. Anchored on the .gnu.lto_ prefix
# rather than on "16 hex characters", which would match plenty of real data.
LTO_SECTION = re.compile(rb"\.gnu\.lto_[.\w]*?\.([0-9a-f]{16})")


def hash_object(path: str) -> str:
    with open(path, "rb") as fh:
        blob = fh.read()

    ids = set(LTO_SECTION.findall(blob))
    for lto_id in ids:
        # Length-preserving, so nothing in the file moves.
        blob = blob.replace(lto_id, b"0" * 16)

    return hashlib.sha256(blob).hexdigest()


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    for line in sys.stdin:
        rel = line.strip()
        if not rel:
            continue
        try:
            digest = hash_object(f"{root}/{rel}" if not rel.startswith("/") else rel)
        except OSError as exc:
            print(f"cannot read {rel}: {exc}", file=sys.stderr)
            return 1
        print(f"{digest}  {rel}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
