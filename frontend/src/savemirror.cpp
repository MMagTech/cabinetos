#include "savemirror.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <set>

#include <json-c/json.h>

#include "filesave.h"

namespace savemirror {
namespace {

std::mutex gLock;
uint64_t gSeq = 0;
std::map<int, uint64_t> gForgotAt;   // rom id -> gSeq when last forgotten

std::string dirFor(const storage::User& u, int romId) {
    return storage::userDir(u) + "/server-saves/" + std::to_string(romId);
}

std::string bytesName(const romm::Asset& a) {
    return std::to_string(a.id) + "-" + storage::safeSegment(a.updatedAt) + ".bin";
}

}  // namespace

void putList(const storage::User& u, int romId, const std::vector<romm::Asset>& rows) {
    if (!u.valid() || romId <= 0) return;
    const std::string dir = dirFor(u, romId);
    storage::makeDirs(dir);
    json_object* arr = json_object_new_array();
    std::set<std::string> keep;
    for (const romm::Asset& a : rows) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "id", json_object_new_int(a.id));
        json_object_object_add(o, "file_name", json_object_new_string(a.fileName.c_str()));
        json_object_object_add(o, "file_size_bytes", json_object_new_int64(a.sizeBytes));
        json_object_object_add(o, "emulator", json_object_new_string(a.emulator.c_str()));
        json_object_object_add(o, "updated_at", json_object_new_string(a.updatedAt.c_str()));
        json_object_array_add(arr, o);
        keep.insert(bytesName(a));
    }
    const std::string path = dir + "/saves.json";
    const std::string tmp = path + ".part";
    if (json_object_to_file_ext(tmp.c_str(), arr, JSON_C_TO_STRING_PRETTY) == 0)
        ::rename(tmp.c_str(), path.c_str());
    json_object_put(arr);
    // Bytes of a row that is gone, or of an older version of one.
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0 &&
                !keep.count(name))
                ::unlink((dir + "/" + name).c_str());
        }
        ::closedir(d);
    }
}

bool getList(const storage::User& u, int romId, std::vector<romm::Asset>* rows) {
    rows->clear();
    if (!u.valid()) return false;
    json_object* arr = json_object_from_file((dirFor(u, romId) + "/saves.json").c_str());
    if (!arr) return false;
    const bool ok = json_object_is_type(arr, json_type_array);
    if (ok) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* o = json_object_array_get_idx(arr, i);
            json_object* v = nullptr;
            romm::Asset a;
            if (json_object_object_get_ex(o, "id", &v)) a.id = json_object_get_int(v);
            if (json_object_object_get_ex(o, "file_name", &v)) a.fileName = json_object_get_string(v);
            if (json_object_object_get_ex(o, "file_size_bytes", &v)) a.sizeBytes = json_object_get_int64(v);
            if (json_object_object_get_ex(o, "emulator", &v)) a.emulator = json_object_get_string(v);
            if (json_object_object_get_ex(o, "updated_at", &v)) a.updatedAt = json_object_get_string(v);
            a.romId = romId;
            if (a.id != 0) rows->push_back(std::move(a));
        }
    }
    json_object_put(arr);
    return ok;
}

void putBytes(const storage::User& u, int romId, const romm::Asset& row,
              const std::vector<uint8_t>& bytes) {
    if (!u.valid() || bytes.empty()) return;
    const std::string dir = dirFor(u, romId);
    storage::makeDirs(dir);
    const std::string path = dir + "/" + bytesName(row);
    const std::string tmp = path + ".part";
    if (cab::writeBytes(tmp, bytes)) ::rename(tmp.c_str(), path.c_str());
    else ::unlink(tmp.c_str());
}

std::vector<uint8_t> getBytes(const storage::User& u, int romId, const romm::Asset& row) {
    if (!u.valid()) return {};
    return cab::readBytes(dirFor(u, romId) + "/" + bytesName(row));
}

bool hasBytes(const storage::User& u, int romId, const romm::Asset& row) {
    struct stat st;
    return u.valid() &&
           ::stat((dirFor(u, romId) + "/" + bytesName(row)).c_str(), &st) == 0 && st.st_size > 0;
}

uint64_t mark() {
    std::lock_guard<std::mutex> lk(gLock);
    return gSeq;
}

bool forgottenSince(int romId, uint64_t m) {
    std::lock_guard<std::mutex> lk(gLock);
    auto it = gForgotAt.find(romId);
    return it != gForgotAt.end() && it->second > m;
}

void forget(const storage::User& u, int romId) {
    {
        std::lock_guard<std::mutex> lk(gLock);
        gForgotAt[romId] = ++gSeq;
    }
    if (!u.valid()) return;
    const std::string dir = dirFor(u, romId);
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            ::unlink((dir + "/" + e->d_name).c_str());
        }
        ::closedir(d);
    }
    ::rmdir(dir.c_str());
}

}  // namespace savemirror
