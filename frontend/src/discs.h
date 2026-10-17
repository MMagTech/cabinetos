// The discs of a game in play (#162): the next launch's disc, the disc rig's
// two knobs, and the swaps a state or Start over makes at once. Each global
// has one definition, in discs.cpp. Moved out of main.cpp in the restructure
// (#398); Insert during play stays in the frame loop until batch (f).

#pragma once

#include "gamesession.h"

extern int gNextStartDisc;
extern float gDiscWaitSeconds;
extern int gDiscBootOverride;

int discIndexOf(const GameSession& sess, int n);
void syncDiscFromCore(GameSession& sess);
void discSwapNow(GameSession& sess, int n, const char* why);
