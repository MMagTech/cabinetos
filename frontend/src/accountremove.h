// Removing a person from the console (#194): what they still owe RomM sent
// with their own login, then their downloads, folder and login removed, in
// that order. Called from Settings' Remove and from --remove-account. Moved
// out of main.cpp in the restructure (#398).

#pragma once

#include <atomic>
#include <string>

#include "accounts.h"

// `stop`, when given, is checked between files: set at exit (#406), the send
// ends after the file on the wire and what is left stays owed.
int sendOwedBeforeRemoving(const accounts::Account& who, const std::string& addr,
                           const std::atomic<bool>* stop = nullptr);
bool removePersonNow(const accounts::Account& who, std::string* err);
