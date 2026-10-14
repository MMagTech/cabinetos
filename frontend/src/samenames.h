// The diagnostic report's "Same-name games" (#383, PROJECT.md question 48).
//
// Every name two or more games share on one system this console shows, and
// for each copy the file name, RomM's regions, languages, revision and tags
// as they arrived, and what a platform's grid heading says when it is
// selected. One report then tells a tag RomM did not read (in the file name,
// empty in its field) from one the console showed wrong (fields right,
// heading wrong) from a wrong match (two different files under one name,
// fixed in RomM).
//
// `cabinetos-frontend --same-names`, run by /usr/libexec/cabinetos-report
// only when a report is made. It asks the server as the person signed in and
// touches nothing on the console.

#pragma once

namespace samenames {

// Prints the list on stdout. 0 when it could ask the server, 1 when not, with
// the reason printed, since the report shows it either way.
int probe(const char* serverAddress);

// `--same-names-test`: the label's wording and when a twin counts, including
// every case that must NOT show one. Needs no server. 0 when all pass.
int test();

}  // namespace samenames
