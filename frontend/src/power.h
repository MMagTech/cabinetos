// The Power menu's half that talks to the machine — docs/PROJECT.md, open
// question 10b.
//
// THE BUTTON IS BORROWED, NOT TAKEN. logind handles the power button by
// default (power off, on this image). The console asks logind for a
// `handle-power-key` inhibitor lock, which tells logind "somebody else is
// handling this key", and then reacts to the key itself by opening the Power
// menu — so a child pressing it mid-game gets a paused game and a menu with
// Resume focused, not a machine switching off.
//
// THE LOCK IS A FILE DESCRIPTOR AND IT DIES WITH THE PROCESS. That is the
// safety property: a frontend that crashes or hangs hard hands the button
// straight back to logind, so our bug can never make the button useless.
//
// NOTHING HERE IS A-NINE-SPECIFIC. The lock, the three actions and their
// permissions are logind's and polkit's stock behaviour for the active local
// session, which the console always is — `implicit active: yes` for all of
// them, read off the image, with no rule of ours. What does vary by machine is
// whether it can sleep at all, and canRest() asks rather than assumes.

#pragma once

namespace power {

// Take the power key (and the Sleep key some keyboards have) from logind.
// False, with a line on stderr, when logind will not hand it over — the
// button then keeps doing what logind does, which is the safe failure.
bool takeButtons();

// Whether this machine can suspend. Rest is only offered when it can.
bool canRest();

enum class Action { Rest, Restart, PowerOff };
const char* name(Action a);

// Asks logind, on a worker so the frame loop is not held. Says on stderr what
// logind answered.
void act(Action a);

// True for a few seconds after the machine comes back from a sleep. The press
// that woke it reaches us as an ordinary power-key press, and without this the
// console would wake straight into its own Power menu. Call once a frame.
bool justWoke();

}  // namespace power
