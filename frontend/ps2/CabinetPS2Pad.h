// The controller's half of the host layer (CabinetPS2Pad.cpp), as
// CabinetPS2Host.cpp calls it. Internal to frontend/ps2.

#pragma once

namespace CabinetPS2
{
	// Hands PCSX2 the pads the frontend last set. On PCSX2's CPU thread only:
	// the first call in Host::PumpMessagesOnCPUThread, before the stop and
	// pause handling.
	void ApplyPads();
} // namespace CabinetPS2
