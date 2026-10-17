// What the RomM client's own files share, and nothing outside the client may
// use: the two timeouts every request is sent with, the write sinks and the
// path encoder (romm_http.cpp), and the JSON field readers (romm_library.cpp).
// The client's face is romm.h. Split out of romm.cpp in the restructure
// (#398), where all of these sat in one anonymous namespace.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <json-c/json.h>

namespace romm {

constexpr long kConnectTimeoutSec = 5;
constexpr long kTimeoutSec = 30;

// Defined in romm_http.cpp, with why each is as it is.
size_t sink(char* p, size_t sz, size_t n, void* user);
size_t byteSink(char* p, size_t sz, size_t n, void* user);
std::string encodeUrl(const std::string& in);

// Defined in romm_library.cpp, with why each is as it is.
std::string jstr(json_object* o, const char* key);
int64_t jint(json_object* o, const char* key);
std::vector<std::string> jstrs(json_object* o, const char* key);

}  // namespace romm
