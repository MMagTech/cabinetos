// Removing a person from the console (#194): what they still owe RomM sent
// with their own login, then their downloads, folder and login removed, in
// that order. Called from Settings' Remove and from --remove-account. Moved
// out of main.cpp in the restructure (#398).

#pragma once

#include <string>

#include "accounts.h"

int sendOwedBeforeRemoving(const accounts::Account& who, const std::string& addr);
bool removePersonNow(const accounts::Account& who, std::string* err);
