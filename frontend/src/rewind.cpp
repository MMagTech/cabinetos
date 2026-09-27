#include "rewind.h"

#include <zlib.h>

namespace cab {

Rewind::Rewind() { worker_ = std::thread([this] { run(); }); }

Rewind::~Rewind() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Rewind::reset() {
    std::lock_guard<std::mutex> lk(m_);
    ++generation_;
    snaps_.clear();
    bytes_ = 0;
}

bool Rewind::offer(std::vector<uint8_t>& raw) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (busy_) return false;
        busy_ = true;
        pending_.swap(raw);
        pendingGen_ = generation_;
    }
    wake_.notify_one();
    return true;
}

bool Rewind::takeNewest(std::vector<uint8_t>& raw) {
    Snap s;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (snaps_.empty()) return false;
        s = std::move(snaps_.back());
        snaps_.pop_back();
        bytes_ -= s.packed.size();
    }
    raw.resize(s.rawSize);
    uLongf len = static_cast<uLongf>(s.rawSize);
    if (uncompress(raw.data(), &len, s.packed.data(), static_cast<uLong>(s.packed.size())) !=
            Z_OK ||
        len != s.rawSize)
        return false;
    return true;
}

size_t Rewind::count() {
    std::lock_guard<std::mutex> lk(m_);
    return snaps_.size();
}

size_t Rewind::bytes() {
    std::lock_guard<std::mutex> lk(m_);
    return bytes_;
}

void Rewind::run() {
    std::vector<uint8_t> raw, packed;
    for (;;) {
        unsigned gen = 0;
        {
            std::unique_lock<std::mutex> lk(m_);
            wake_.wait(lk, [this] { return stopping_ || busy_; });
            if (stopping_) return;
            raw.swap(pending_);
            gen = pendingGen_;
        }
        // Level 1: fast, and states are mostly zeros and repeats, so even the
        // quickest setting takes most of the size away.
        uLongf len = compressBound(static_cast<uLong>(raw.size()));
        packed.resize(len);
        const bool ok = compress2(packed.data(), &len, raw.data(),
                                  static_cast<uLong>(raw.size()), 1) == Z_OK;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (ok && gen == generation_) {
                snaps_.push_back({std::vector<uint8_t>(packed.begin(), packed.begin() + len),
                                  raw.size()});
                bytes_ += len;
                while (!snaps_.empty() && (snaps_.size() > kKeep || bytes_ > kBudget)) {
                    bytes_ -= snaps_.front().packed.size();
                    snaps_.pop_front();
                }
            }
            busy_ = false;
        }
    }
}

}  // namespace cab
