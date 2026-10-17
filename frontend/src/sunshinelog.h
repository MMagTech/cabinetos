// Sunshine's log, read as it grows, which is the one place the console can
// learn that a device is streaming (remoteplay::streaming). remoteplay.cpp's
// watcher calls these. Moved out of remoteplay.cpp in the restructure (#398).

#pragma once

namespace remoteplay {

// What the log has gained since the last read. On the watcher's thread.
void readLog();

// THE TAIL IS NOT ATOMIC: these two run only while no watcher runs, the first
// before its thread starts (fresh: what the log holds now is skipped), the
// second after it is joined.
void startReadingLog(bool fresh);
void stopReadingLog();

}  // namespace remoteplay
