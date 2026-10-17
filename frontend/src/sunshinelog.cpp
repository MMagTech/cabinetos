// Sunshine's log, read as it grows (sunshinelog.h): whether a device is
// streaming, from the lines the pinned Sunshine writes (remoteplay.h names
// them). Moved out of remoteplay.cpp in the restructure (#398).

#include "sunshinelog.h"

#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <fstream>

#include "remoteplay.h"

namespace remoteplay {
namespace {

constexpr const char* kLogFile = "/var/lib/cabinetos/remoteplay/.config/sunshine/sunshine.log";

std::atomic<bool> gStreaming{false};

// THE LOG AS IT GROWS. Sunshine starts a new file each time it starts (the
// old one becomes sunshine.log.1), so a file that is a different one, or
// shorter than what was read, is read from its start.
struct Tail {
    ino_t inode = 0;
    off_t at = 0;
} gTail;

}  // namespace

void readLog() {
    struct stat st{};
    if (::stat(kLogFile, &st) != 0) {
        gStreaming = false;
        return;
    }
    if (st.st_ino != gTail.inode || st.st_size < gTail.at) {
        // A NEW FILE IS A NEW SUNSHINE, and a new Sunshine has nobody
        // streaming: the last one may have been stopped mid-stream, without
        // writing "CLIENT DISCONNECTED".
        if (gTail.inode != 0 && gStreaming) {
            gStreaming = false;
            std::fprintf(stderr, "[remoteplay] the stream ended (Sunshine started again)\n");
        }
        gTail = {st.st_ino, 0};
    }
    if (st.st_size == gTail.at) return;
    std::ifstream f(kLogFile);
    f.seekg(gTail.at);
    std::string line;
    bool any = false, on = gStreaming;
    while (std::getline(f, line)) {
        if (f.eof()) break;   // a line still being written is read next time
        gTail.at += static_cast<off_t>(line.size()) + 1;
        // A SESSION CLOSED AT SUNSHINE'S END (the console's screen going
        // out, #294: POST /api/apps/close) ends every stream with "Process
        // terminated" and no "CLIENT DISCONNECTED". Missed, the console
        // still counted the phone as there, so the phone joining again was
        // no change: no press counted, and it streamed the console's black
        // layer (A9, 2026-10-09 06:31).
        // AND "Executing Undo Cmd", the session's end step, which Sunshine
        // runs however a session ends. The console's idle closed one at
        // 19:38:57 the same day, Sunshine wrote the Undo line and neither of
        // the other two, and the console counted a phone for an hour after
        // it had gone: the TV's controllers stayed set aside (#304).
        if (line.find("CLIENT CONNECTED") != std::string::npos) on = any = true;
        else if (line.find("CLIENT DISCONNECTED") != std::string::npos ||
                 line.find("Process terminated") != std::string::npos ||
                 line.find("Executing Undo Cmd") != std::string::npos)
            on = false, any = true;
    }
    if (any && on != gStreaming) {
        gStreaming = on;
        std::fprintf(stderr, "[remoteplay] %s\n", on ? "a device is streaming" : "the stream ended");
    }
}

void startReadingLog(bool fresh) {
    // Fresh: what the log holds now was written before this Sunshine
    // could have taken a device, so it is skipped, whichever Sunshine's
    // file it is; a newer file is read from its start (readLog).
    if (fresh) {
        struct stat st{};
        gTail = ::stat(kLogFile, &st) == 0 ? Tail{st.st_ino, st.st_size} : Tail{};
        gStreaming = false;
    }
}

void stopReadingLog() {
    gStreaming = false;
    gTail = {};
}

bool streaming() { return gStreaming; }

}  // namespace remoteplay
