// The console's own voice: the short sounds the interface makes when somebody
// moves around it.
//
// MMagTech, 2026-09-21: *"i also think a navigation sound of some sort would be
// nice and later we have the option in settings to turn it off."* Both halves
// are built here — the sounds, and the switch. The switch has no screen to live
// on yet, because Settings does not exist, so today it is a flag and a field;
// when Settings arrives it sets `sound::setEnabled` and nothing else changes.
//
// THE SOUNDS ARE SYNTHESISED, NOT RECORDED, and that is a deliberate choice
// with three reasons behind it:
//
//   NOTHING TO SHIP AND NOTHING TO LICENCE. This project keeps docs/LICENCES.md
//   because it cares where its parts came from. A recorded UI click is somebody
//   else's work with somebody else's terms attached, and four short sounds are
//   not worth a new class of obligation in the image.
//
//   THEY BECOME NUMBERS, like everything else in the design system. A click
//   that is "880 Hz, 45 ms, exponential decay" can be argued with in the same
//   way a focus scale can. A .wav can only be replaced.
//
//   THEY COST NOTHING. Each cue is a few thousand samples, built once at
//   startup and pushed at the device when it is asked for. No files, no
//   decoder, no disk.
//
// A UI SOUND IS NOT THE GAME'S SOUND. This opens its OWN stream on the default
// device rather than sharing the core's: the core's stream runs at whatever
// sample rate the emulated machine wants, it is opened and closed with the
// game, and the pause menu has to be able to click while a game is running.
// Two streams on one device is what SDL3's audio model is for.

#pragma once

namespace sound {

// What happened, not what it sounds like. The caller says "focus moved" and
// this file decides what that is — which is the only way the set stays
// consistent when somebody retunes it.
enum class Cue {
    Move,      // focus moved to another thing
    Activate,  // something was chosen
    Back,      // a screen was left
    Edge,      // focus tried to move and there was nothing there
};

// Opens the device and builds the cues. Returns false when there is no audio
// device, which is not an error: a console with no sound card is still a
// console, and every play() after that does nothing.
bool init();
void shutdown();

// THE SWITCH SETTINGS WILL OWN. Off means silent, and nothing else about the
// interface changes — there is no visual compensation for a muted console,
// because a person who turns the sounds off has said what they want.
void setEnabled(bool on);
bool enabled();

// 0..1. The default is deliberately low: this is punctuation under whatever
// else is happening, and a navigation click that competes with a game's music
// is one nobody keeps switched on.
void setVolume(float v);

// Plays a cue, or does nothing if there is no device or the switch is off.
// Safe to call from the frame loop as often as input arrives — a cue that is
// already sounding is simply overlapped rather than queued, so running along a
// shelf ticks once per card instead of building a backlog of clicks.
void play(Cue c);

}  // namespace sound
