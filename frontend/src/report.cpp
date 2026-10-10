// The diagnostic report: run the tool, serve the one file. See report.h.

#include "report.h"

#include "proc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace report {

namespace {

// The tool, or a copy of it for trying a change without an image
// (tools/ui-loop.sh --env CABINETOS_REPORT=...), as CABINETOS_WII_BRIDGE.
std::string tool() {
    const char* env = std::getenv("CABINETOS_REPORT");
    return env && *env ? env : "/usr/libexec/cabinetos-report";
}

// The server's state. One report is served at a time: opening the screen
// again replaces it.
std::mutex g_mu;
std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_serving{false};
int g_fd = -1;

// No 0/O, no 1/I/L: it may be typed from the screen into a computer. The
// same alphabet as File access's password. 8 of 31 symbols is about 40 bits,
// on the home network, for the minutes the screen is open.
std::string token() {
    static const char kAlphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    std::string out;
    FILE* f = std::fopen("/dev/urandom", "rb");
    for (int i = 0; i < 8; ++i) {
        unsigned char b = 0;
        if (!f || std::fread(&b, 1, 1, f) != 1) b = static_cast<unsigned char>(std::rand());
        out += kAlphabet[b % (sizeof(kAlphabet) - 1)];
    }
    if (f) std::fclose(f);
    return out;
}

std::string baseName(const std::string& path) {
    const size_t at = path.rfind('/');
    return at == std::string::npos ? path : path.substr(at + 1);
}

// EVERY WAIT IS A QUARTER OF A SECOND AT A TIME, checking for stop(): the
// screen's close joins this thread on the frame thread, and a phone that
// connects and then says nothing must not freeze the console. Ten seconds in
// all for anything one device does.
bool ready(int fd, short events, int64_t* budgetMs) {
    while (!g_stop && *budgetMs > 0) {
        pollfd p{fd, events, 0};
        const int r = ::poll(&p, 1, 250);
        *budgetMs -= 250;
        if (r > 0) return true;
        if (r < 0 && errno != EINTR) return false;
    }
    return false;
}

bool sendAll(int fd, const char* data, size_t n, int64_t* budgetMs) {
    while (n > 0) {
        if (!ready(fd, POLLOUT, budgetMs)) return false;
        const ssize_t w = ::send(fd, data, n, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
        data += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

void answer(int fd, const std::string& path, const std::string& want) {
    int64_t budget = 10000;
    // The request line only: one file, nothing to negotiate.
    std::string req;
    char buf[1024];
    while (req.find("\r\n") == std::string::npos && req.size() < 4096) {
        if (!ready(fd, POLLIN, &budget)) return;
        const ssize_t r = ::recv(fd, buf, sizeof buf, MSG_DONTWAIT);
        if (r < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (r <= 0) return;
        req.append(buf, static_cast<size_t>(r));
    }
    const bool head = req.rfind("HEAD ", 0) == 0;
    const bool get = req.rfind("GET ", 0) == 0;
    const size_t sp = req.find(' ');
    const size_t sp2 = sp == std::string::npos ? sp : req.find(' ', sp + 1);
    const std::string target = sp2 == std::string::npos ? "" : req.substr(sp + 1, sp2 - sp - 1);
    if ((!get && !head) || target != want) {
        static const char k404[] =
            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(fd, k404, sizeof k404 - 1, &budget);
        return;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    struct stat st{};
    if (!f || ::fstat(fileno(f), &st) != 0) {
        if (f) std::fclose(f);
        static const char k410[] =
            "HTTP/1.1 410 Gone\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(fd, k410, sizeof k410 - 1, &budget);
        return;
    }
    // A DOWNLOAD, NOT A PAGE. Served as plain text alone, an iPhone shows the
    // report in the browser instead of saving it, and there is no obvious way
    // from there to attach it to an issue.
    char hdr[512];
    std::snprintf(hdr, sizeof hdr,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain; charset=utf-8\r\n"
                  "Content-Disposition: attachment; filename=\"%s\"\r\n"
                  "Content-Length: %lld\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  baseName(path).c_str(), static_cast<long long>(st.st_size));
    bool ok = sendAll(fd, hdr, std::strlen(hdr), &budget);
    if (ok && get) {
        char chunk[64 * 1024];
        size_t n;
        while (ok && (n = std::fread(chunk, 1, sizeof chunk, f)) > 0) ok = sendAll(fd, chunk, n, &budget);
        if (ok) std::fprintf(stderr, "[report] downloaded\n");
    }
    std::fclose(f);
}

}  // namespace

Made make(const std::string& storageRoot) {
    Made m;
    const proc::Result r = proc::run({tool(), "--from-settings", "--root", storageRoot}, 120);
    if (r.timedOut) {
        m.why = "took too long";
    } else if (!r.ok()) {
        m.why = proc::trimmed(r.err);
        if (m.why.empty()) m.why = r.status < 0 ? "the report tool is missing" : "the report tool failed";
    } else {
        const std::vector<std::string> lines = proc::lines(r.out);
        if (!lines.empty()) m.path = proc::trimmed(lines.back());
        m.ok = !m.path.empty() && ::access(m.path.c_str(), R_OK) == 0;
        if (!m.ok) m.why = "the report tool wrote nothing";
    }
    if (m.ok) {
        struct stat st{};
        ::stat(m.path.c_str(), &st);
        std::fprintf(stderr, "[report] written %s (%lld bytes)\n", m.path.c_str(),
                     static_cast<long long>(st.st_size));
    } else {
        std::fprintf(stderr, "[report] not written: %s\n", m.why.c_str());
    }
    return m;
}

bool serve(const std::string& path, const std::string& address, std::string* url,
           std::string* why) {
    stop();
    std::lock_guard<std::mutex> lock(g_mu);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = 0;   // any free port: the firewall's home zone allows 1025 and up
    if (::inet_pton(AF_INET, address.c_str(), &sa.sin_addr) != 1) {
        if (why) *why = "no address on the home network";
        return false;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (why) *why = std::strerror(errno);
        return false;
    }
    socklen_t len = sizeof sa;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0 || ::listen(fd, 4) != 0 ||
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len) != 0) {
        if (why) *why = std::strerror(errno);
        ::close(fd);
        return false;
    }
    const int port = ntohs(sa.sin_port);
    const std::string want = "/" + token();
    if (url) *url = "http://" + address + ":" + std::to_string(port) + want;
    g_fd = fd;
    g_stop = false;
    g_serving = true;
    std::fprintf(stderr, "[report] serving on port %d while the screen is open\n", port);
    g_thread = std::thread([fd, path, want]() {
        while (!g_stop) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 250) <= 0) continue;
            const int c = ::accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) continue;
            answer(c, path, want);
            ::close(c);
        }
    });
    return true;
}

void stop() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_serving) return;
    g_stop = true;
    if (g_thread.joinable()) g_thread.join();
    if (g_fd >= 0) ::close(g_fd);
    g_fd = -1;
    g_serving = false;
    std::fprintf(stderr, "[report] stopped serving\n");
}

bool serving() { return g_serving; }

}  // namespace report
