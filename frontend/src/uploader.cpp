#include "uploader.h"

#include <algorithm>
#include <cstdio>

#include "cache.h"
#include "favorites.h"
#include "fileio.h"
#include "playtime.h"
#include "savemirror.h"

// ONE FILE TO RomM, as whoever `c` is signed in as: a save, a state (with its
// picture) or a screenshot. The uploader sends the signed-in person's this way,
// and removing a person (#194) sends theirs with their own login.
bool uploadOne(const romm::Client& c, int romId, bool isState, bool isScreenshot,
               const std::string& emulator, const std::string& fileName,
               const std::vector<uint8_t>& data, const std::string& shotName,
               const std::vector<uint8_t>& shot, std::string* err) {
    return isScreenshot ? c.uploadScreenshot(romId, fileName, data, err)
           : isState    ? c.uploadState(romId, emulator, fileName, data, err, shotName, shot)
                        : c.uploadSave(romId, emulator, fileName, data, err);
}

// Every play session `who` owes, a hundred at a time, RomM's most. Whatever
// RomM has now (new, or a copy of one it already had) or has refused for good
// stops being owed.
bool sendPlaySessionsAs(const romm::Client& c, const storage::User& who,
                        std::string* err) {
    std::vector<playtime::Session> list = playtime::owed(who);
    constexpr size_t kBatch = 100;
    for (size_t at = 0; at < list.size(); at += kBatch) {
        const size_t end = std::min(list.size(), at + kBatch);
        std::vector<romm::Client::PlaySession> out;
        for (size_t i = at; i < end; ++i)
            out.push_back({list[i].romId, playtime::iso(list[i].startMs),
                           playtime::iso(list[i].endMs), list[i].playedMs});
        std::vector<int> outcome;
        if (!c.postPlaySessions(out, &outcome, err)) return false;
        std::vector<playtime::Session> done;
        for (size_t i = at; i < end; ++i)
            if (outcome[i - at] != 0) done.push_back(list[i]);
        playtime::sent(who, done);
        std::fprintf(stderr, "[playtime] sent %zu session(s), %zu taken\n", end - at,
                     done.size());
        if (done.size() < end - at) {
            if (err) *err = "RomM did not say what became of some";
            return false;
        }
    }
    return true;
}

void Uploader::start(romm::Client* client) {
    client_ = client;
    worker_ = std::thread([this] { run(); });
}

void Uploader::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Uploader::push(Job job) {
    // Recorded BEFORE it is queued, so the window in which the console owes
    // the server something and does not know it is zero.
    cache::Owed o;
    o.romId = job.romId;
    o.isState = job.isState;
    o.isScreenshot = job.isScreenshot;
    o.emulator = job.emulator;
    o.fileName = job.fileName;
    o.localPath = job.localPath;
    o.shotName = job.shotName;
    o.shotPath = job.shotPath;
    cache::markPending(storage::currentUser(), o, static_cast<int64_t>(job.data.size()));
    enqueue(std::move(job));
}

void Uploader::resendOwed() {
    const std::vector<cache::Owed> list = cache::owed(storage::currentUser());
    for (const cache::Owed& o : list) {
        Job j;
        j.romId = o.romId;
        j.isState = o.isState;
        j.isScreenshot = o.isScreenshot;
        j.emulator = o.emulator;
        j.fileName = o.fileName;
        j.localPath = o.localPath;
        j.shotName = o.shotName;
        j.shotPath = o.shotPath;
        j.resend = true;
        enqueue(std::move(j));
    }
    if (!playtime::owed(storage::currentUser()).empty()) sendPlay();
    if (!favOwed(storage::currentUser()).empty()) sendFavorites();
}

void Uploader::sendFavorites() {
    Job j;
    j.isFavorite = true;
    j.fileName = "favourites";
    j.resend = true;
    enqueue(std::move(j));
}

void Uploader::sendPlay() {
    Job j;
    j.isPlay = true;
    j.fileName = "play sessions";
    j.resend = true;
    enqueue(std::move(j));
}

void Uploader::rotate(const Job& job) {
    std::vector<romm::Asset> all;
    std::string err;
    if (!client_->fetchStates(job.romId, &all, &err)) {
        std::fprintf(stderr, "[state] could not list states to keep three: %s\n",
                     err.c_str());
        return;
    }
    std::vector<romm::Asset> mine;
    for (const romm::Asset& a : all)
        if (a.emulator == job.emulator) mine.push_back(a);
    if (mine.size() <= kStatesKept) return;
    std::sort(mine.begin(), mine.end(), [](const romm::Asset& a, const romm::Asset& b) {
        return a.updatedAt != b.updatedAt ? a.updatedAt > b.updatedAt
                                          : a.fileName > b.fileName;
    });
    bool keptNew = false;
    for (size_t i = 0; i < kStatesKept; ++i)
        if (mine[i].fileName == job.fileName) keptNew = true;
    if (!keptNew) {
        std::fprintf(stderr, "[state] %s is not among the newest three on RomM; "
                             "deleting nothing\n", job.fileName.c_str());
        return;
    }
    std::vector<int> ids;
    for (size_t i = kStatesKept; i < mine.size(); ++i) ids.push_back(mine[i].id);
    if (!client_->deleteStates(ids, &err)) {
        std::fprintf(stderr, "[state] could not remove older states: %s\n", err.c_str());
        return;
    }
    // This console's copies of the ones that went, and their pictures.
    const size_t slash = job.localPath.rfind('/');
    const std::string dir =
        slash == std::string::npos ? std::string() : job.localPath.substr(0, slash);
    for (size_t i = kStatesKept; i < mine.size(); ++i) {
        const std::string& name = mine[i].fileName;
        std::fprintf(stderr, "[state] rotated out %s\n", name.c_str());
        if (dir.empty()) continue;
        cab::removeFile(dir + "/" + name);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) cab::removeFile(dir + "/" + name.substr(0, dot) + ".png");
    }
}

void Uploader::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string k = key(job);
        if (queued_.count(k)) {
            // Already on its way. A person's own save replaces the bytes
            // of a resend of the same file, which is only ever older.
            if (job.resend) return;
            for (Job& q : queue_)
                if (key(q) == k) { q = std::move(job); return; }
            // In flight right now: queue it behind, it is newer.
        }
        queued_.insert(k);
        queue_.push_back(std::move(job));
        ++pending_;
    }
    wake_.notify_one();
}

void Uploader::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            // Drain on the way out rather than dropping: a queued upload is
            // a save someone has already made, and quitting is not a reason
            // to discard it.
            if (queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        if (job.isFavorite) {
            std::string err;
            const bool ok = sendFavoritesAs(*client_, storage::currentUser(), &err);
            if (!ok) std::fprintf(stderr, "[favorite] not sent, kept locally: %s\n", err.c_str());
            finished(job, ok, /*leftSomething=*/!ok);
            continue;
        }
        if (job.isPlay) {
            std::string err;
            const bool ok = sendPlaySessions(&err);
            if (!ok) std::fprintf(stderr, "[playtime] not sent, kept locally: %s\n", err.c_str());
            finished(job, ok, /*leftSomething=*/!ok);
            continue;
        }
        // A resend reads its bytes now, off the disk.
        if (job.data.empty() && !job.localPath.empty()) {
            job.data = cab::readBytes(job.localPath);
            if (!job.shotPath.empty()) job.shot = cab::readBytes(job.shotPath);
        }
        bool ok = false;
        std::string err;
        if (job.data.empty()) {
            // The file it was owed from is gone (deleted by hand, or the
            // person removed). Nothing left to send, so nothing is owed.
            err = "its file is gone";
            cache::clearPending(storage::currentUser(), job.romId, job.fileName);
        } else {
            ok = uploadOne(*client_, job.romId, job.isState, job.isScreenshot,
                           job.emulator, job.fileName, job.data, job.shotName, job.shot,
                           &err);
            // Cleared only on success. A failed upload leaves the marker,
            // which is the point: the file is still on disk, it still has
            // not reached RomM, and the console still owes it.
            if (ok) cache::clearPending(storage::currentUser(), job.romId, job.fileName);
        }
        if (ok && job.isState) rotate(job);
        if (ok && !job.isState && !job.isScreenshot) refreshSaveCopy(job);
        if (job.isState && !job.resend) stateOutcome.store(ok ? 1 : 2);
        std::fprintf(stderr, "[%s] %s%s %s%s\n",
                     job.isScreenshot ? "screenshot" : job.isState ? "state" : "save",
                     job.resend ? "resent, " : "",
                     ok ? "uploaded" : "upload failed, kept locally:",
                     ok ? job.emulator.c_str() : err.c_str(),
                     ok && !job.shot.empty() ? " with its picture" : "");
        finished(job, ok, /*leftSomething=*/!ok && !job.data.empty());
    }
}

void Uploader::refreshSaveCopy(const Job& job) {
    const storage::User& u = storage::currentUser();
    savemirror::forget(u, job.romId);
    std::vector<romm::Asset> rows;
    std::string err;
    if (!client_->fetchSaves(job.romId, &rows, &err)) return;
    savemirror::putList(u, job.romId, rows);
    for (const romm::Asset& a : rows)
        if (a.fileName == job.fileName && a.emulator == job.emulator)
            savemirror::putBytes(u, job.romId, a, job.data);
}

void Uploader::finished(const Job& job, bool ok, bool leftSomething) {
    bool resendNow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(key(job));
        // THE SERVER ANSWERED, so anything a failure left behind can
        // go now rather than at the next timer.
        if (ok && owing_.load()) resendNow = true;
        if (leftSomething) owing_ = true;
        if (ok && queue_.empty()) owing_ = false;
    }
    --pending_;
    if (resendNow) resendOwed();
}

bool Uploader::sendPlaySessions(std::string* err) {
    return sendPlaySessionsAs(*client_, storage::currentUser(), err);
}
