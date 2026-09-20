#include "proc.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <ctime>

namespace proc {
namespace {

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

}  // namespace

Result run(const std::vector<std::string>& args, int timeoutSeconds) {
    Result r;
    if (args.empty()) return r;

    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0) return r;
    if (pipe(errPipe) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return r;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        return r;
    }
    if (pid == 0) {
        // The child: a few dups and an exec, and nothing that can throw.
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);   // execvp only returns on failure
    }

    close(outPipe[1]);
    close(errPipe[1]);

    // Read both pipes until they close or the deadline passes. The deadline
    // matters more than it looks: a join against a network that simply does not
    // answer will sit there, and a console that stops responding is worse than
    // one that says it could not join.
    const int64_t deadline = nowMs() + static_cast<int64_t>(timeoutSeconds) * 1000;
    struct pollfd fds[2] = {
        {outPipe[0], POLLIN, 0},
        {errPipe[0], POLLIN, 0},
    };
    bool open0 = true, open1 = true;
    while (open0 || open1) {
        const int64_t left = deadline - nowMs();
        if (left <= 0) { r.timedOut = true; break; }
        fds[0].events = open0 ? POLLIN : 0;
        fds[1].events = open1 ? POLLIN : 0;
        const int n = poll(fds, 2, static_cast<int>(std::min<int64_t>(left, 1000)));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[4096];
            const ssize_t got = read(fds[i].fd, buf, sizeof buf);
            if (got > 0) {
                (i == 0 ? r.out : r.err).append(buf, static_cast<size_t>(got));
            } else {
                (i == 0 ? open0 : open1) = false;
            }
        }
    }
    close(outPipe[0]);
    close(errPipe[0]);

    if (r.timedOut) {
        kill(pid, SIGTERM);
        // A moment to go politely, then stop being polite. A wedged child left
        // behind becomes a zombie this process never reaps.
        for (int i = 0; i < 20; ++i) {
            int st = 0;
            if (waitpid(pid, &st, WNOHANG) == pid) return r;
            usleep(100000);
        }
        kill(pid, SIGKILL);
        int st = 0;
        waitpid(pid, &st, 0);
        return r;
    }

    int st = 0;
    if (waitpid(pid, &st, 0) == pid && WIFEXITED(st)) r.status = WEXITSTATUS(st);
    return r;
}

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    auto space = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

}  // namespace proc
