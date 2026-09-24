// The console's own settings, the ones a person changes in Settings and
// expects to find the same after a restart.
//
// ONE FILE, `<root>/config/settings.json`, beside first-run.json and the
// others, and one flat object of strings in it:
//
//     { "interface_sounds": "quiet" }
//
// WORDS, NOT NUMBERS, on purpose. A person who opens this file over file
// access should be able to read it, and a level stored as a word survives the
// numbers behind it being retuned: "quiet" stays quiet when what quiet means
// changes.
//
// THESE ARE THE CONSOLE'S, NOT A PERSON'S. Everything here applies to whoever
// is signed in. Anything that should follow an account (a background colour,
// RetroAchievements) belongs under that account, not in this file.
//
// A MISSING OR UNREADABLE FILE IS NOT AN ERROR. Every caller has a default,
// and a console that has never had a setting changed has no file at all.

#pragma once

#include <string>

namespace prefs {

// The stored word for `key`, or `fallback` if there is none.
std::string get(const std::string& key, const std::string& fallback);

// Writes it down now, atomically, so pulling the plug a moment later keeps it.
void set(const std::string& key, const std::string& value);

}  // namespace prefs
