#include "selftests.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "accounts.h"
#include "firstrun.h"
#include "romm.h"
#include "server.h"
#include "storage.h"

// Does completing setup actually STICK?
//
// THIS TESTS THE ONLY CODE IN FIRST RUN THAT HAS NEVER RUN. Every walkthrough
// of the flow so far has used `--setup`, which forces it on an already
// configured machine and deliberately writes nothing — so `setServerAddress`,
// `markCompleted` and the token save have never once been executed by the
// product. Three separate faults found on 2026-09-20 were in code that looked
// correct and had simply never been exercised: SDL text input that was never
// enabled, a key press that never reported a commit, and a frame that never
// reached the window.
//
// AND THE FAILURE THIS GUARDS IS THE WORST ONE THE FEATURE HAS. If the marker
// does not persist, a console completes setup and boots straight back into
// setup, for ever, with no way past it — on a machine somebody has just
// installed. It would look exactly like a console that cannot be set up at all.
//
// It runs against a scratch root and touches nothing real, so it is safe on any
// machine and belongs in CI.
int firstRunWriteTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // Somewhere that is nobody's console.
    char root[] = "/tmp/cabinetos-firstrun-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[first-run] could not make a scratch root\n");
        return 1;
    }
    // AND NO REAL DRIVE. Sign out (6, below) empties roms/ and cache/ on every
    // location, and a games drive plugged into this machine is one: the drive
    // search points into the scratch root, where nothing is mounted, before
    // anything asks where the games are (#398, FIRSTRUN-14).
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[first-run] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);
    check(storage::locations() == std::vector<std::string>{root},
          "the scratch root is the only place for games");

    // 1. A machine with nothing on it has not been set up.
    //
    // NOTE WHAT THIS CANNOT ISOLATE: `serverAddress()` reads $CABINETOS_ROMM and
    // /etc/cabinetos/session.env before it reads our file, by design — root's
    // answer outranks the session's. On a console that is configured by hand,
    // those are set, so "is it configured" can legitimately be true here. That
    // is the adoption rule working, not a failure, and the test says which case
    // it is rather than pretending the environment is clean.
    const bool envConfigured = !firstrun::serverAddress().empty();
    const firstrun::Completion before = firstrun::completion();
    if (envConfigured) {
        std::printf("  note  this machine has a server address from %s, so the\n"
                    "        'not set up' case cannot be checked here\n",
                    firstrun::serverAddressSource().c_str());
    } else {
        check(!before.done, "a machine with nothing on it is not set up");
    }

    // 2. The address is written, and comes back.
    const std::string want = "cabinetos-write-test.invalid:6005";
    std::string werr;
    check(firstrun::setServerAddress(want, &werr),
          "setServerAddress reports success");
    if (!werr.empty()) std::printf("        (%s)\n", werr.c_str());

    // Read the FILE, not the resolver, because the resolver correctly prefers
    // root's answer and would hide whether ours landed at all.
    {
        const std::string path = storage::configDir() + "/server.json";
        FILE* f = std::fopen(path.c_str(), "rb");
        std::string body;
        if (f) {
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
            std::fclose(f);
        }
        check(body.find(want) != std::string::npos,
              "config/server.json holds the address that was written");
    }

    // 3. The marker is written, and says the flow was WALKED.
    check(firstrun::markCompleted(/*adopted=*/false, &werr),
          "markCompleted reports success");
    const firstrun::Completion after = firstrun::completion();
    check(after.done, "the console now reports itself set up");
    check(after.why == firstrun::Why::Completed,
          "and records that the flow was walked, not adopted");
    check(!after.when.empty(), "with a timestamp");

    // 4. THE ONE THAT MATTERS ON A REBOOT: it survives being read fresh. The
    // marker is a file, so this is the same question as "does the next boot see
    // it" — and a `completion()` that answered from memory would pass every
    // check above while a rebooted console went round the loop again.
    {
        const std::string path = storage::configDir() + "/first-run.json";
        struct stat st;
        check(::stat(path.c_str(), &st) == 0 && st.st_size > 0,
              "config/first-run.json is on disk and not empty");
    }

    // 5. Adopted is a different answer and has to stay different, because it is
    // how a machine set up by hand is told from one that walked the flow.
    check(firstrun::markCompleted(/*adopted=*/true, &werr), "markCompleted(adopted)");
    check(firstrun::completion().why == firstrun::Why::AdoptedExisting,
          "an adopted machine records itself as adopted");

    // 6. SIGN OUT (issue #61): everything tied to the server goes, and
    // nothing else. A console's worth of files in the scratch root, and a
    // scratch HOME for the tokens and the PIN, so no real one is touched.
    {
        const std::string r = root, home = r + "/home";
        setenv("HOME", home.c_str(), 1);
        auto put = [](const std::string& path) {
            storage::makeDirs(path.substr(0, path.rfind('/')));
            if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fputs("x\n", f); std::fclose(f); }
        };
        const std::vector<std::string> gone = {
            r + "/roms/snes/1 - Kept.sfc",       r + "/cache/snes/2 - Cached.sfc",
            r + "/users/1 - A/saves/snes/1/x.srm", r + "/users/1 - A/pending/1-x.srm",
            r + "/covers/srv_6005/a.jpg",        r + "/config/user.json",
            r + "/config/accounts.json",         home + "/.config/cabinetos/accounts/1.json",
            home + "/.config/cabinetos/pin",     home + "/.config/cabinetos/romm.json",
            // What was learned per game, by rom id: the next server's game 1
            // is another game (FILES-12).
            r + "/games/1.json",                 r + "/games/1.facts.json",
            r + "/config/wii-codes.txt",         r + "/config/wiiu-codes.txt",
            r + "/config/picture-quality.json",  r + "/config/ps2-widescreen.json",
        };
        const std::vector<std::string> kept = {
            r + "/bios/scph5501.bin", r + "/config/settings.json",
        };
        for (const auto& p : gone) put(p);
        for (const auto& p : kept) put(p);
        check(server::unsentSaves() == 1, "an unsent save is counted before signing out");
        check(server::signOut(&werr), "signOut reports success");
        const firstrun::Completion out = firstrun::completion();
        check(!out.done && out.why == firstrun::Why::SignedOut && out.clearing,
              "signed out: first run is needed, and the clearing is still owed");
        server::finishSignOut();
        bool allGone = true;
        for (const auto& p : gone)
            if (storage::exists(p)) { allGone = false; std::printf("        left: %s\n", p.c_str()); }
        check(allGone, "games and what was learned about them, saves, covers, accounts, tokens "
                       "and the PIN are gone");
        bool allKept = true;
        for (const auto& p : kept) allKept = allKept && storage::exists(p);
        check(allKept, "BIOS and the console's settings stay");
        check(storage::exists(r + "/roms") && storage::exists(r + "/cache") &&
                  storage::exists(r + "/users"),
              "the empty folders are back");
        const firstrun::Completion cleared = firstrun::completion();
        check(!cleared.done && cleared.why == firstrun::Why::SignedOut && !cleared.clearing,
              "still signed out, and cleared");
        check(!accounts::pinIsSet(), "no PIN");

        // 7. CHANGE SERVER ADDRESS keeps the covers, filed under the address.
        if (server::addressIsOurs()) {
            firstrun::setServerAddress("old.invalid:6005", &werr);
            put(r + "/covers/" + storage::safeSegment("old.invalid:6005") + "/a.jpg");
            check(server::changeAddress("old.invalid:6005", "new.invalid:6005", &werr),
                  "changeAddress reports success");
            check(firstrun::serverAddress() == "new.invalid:6005", "the new address is read back");
            check(storage::exists(r + "/covers/" + storage::safeSegment("new.invalid:6005") +
                                  "/a.jpg"),
                  "the covers moved with it");
        } else {
            std::printf("  note  the address is set by %s, so changing it cannot be checked "
                        "here\n", firstrun::serverAddressSource().c_str());
        }
    }

    // Leave nothing behind. A test that litters is a test nobody runs twice.
    std::string rm = "rm -rf ";
    rm += root;
    if (std::system(rm.c_str()) != 0)
        std::printf("\n  (could not remove %s)\n", root);

    std::printf("\nfirst-run writes: %d failure%s\n", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// The store's rules, asserted against a scratch root rather than reasoned
// about. Every case here is a REFUSAL, because the happy path is the part that
// already works — the same argument --first-run-rules is built on, and it found
// a real deadlock on its first run.
int accountsTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    char root[] = "/tmp/cabinetos-accounts-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[accounts] could not make a scratch root\n");
        return 1;
    }
    // Both halves have to move: the list follows the storage root, the tokens
    // follow HOME. Missing either one writes into the real console.
    storage::setRoot(root);
    setenv("HOME", root, 1);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[accounts] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);

    std::string err;
    accounts::Account alice; alice.id = 1; alice.name = "MMagTech";
    accounts::Account bob;   bob.id = 2;   bob.name = "Someone Else";

    check(accounts::all().empty(), "a fresh console knows nobody");
    check(accounts::activeId() == 0, "and is acting as nobody");

    check(!accounts::add(alice, "", &err), "an account with no token is refused");
    accounts::Account noId; noId.name = "nobody";
    check(!accounts::add(noId, "t", &err), "an account with no id is refused");

    check(accounts::add(alice, "alice-token", &err), "the first account is added");
    check(accounts::activeId() == 1, "and becomes active, having nobody to take over from");

    check(accounts::add(bob, "bob-token", &err), "a second account is added");
    check(accounts::activeId() == 1,
          "and does NOT take over from whoever is signed in");
    check(accounts::all().size() == 2, "both are listed");

    check(accounts::add(alice, "alice-again", &err), "re-pairing an existing id succeeds");
    check(accounts::all().size() == 2, "and replaces rather than making a second row");

    check(!accounts::remove(1, &err), "removing the ACTIVE account is refused");
    check(!accounts::remove(99, &err), "removing an unknown account is refused");
    check(accounts::remove(2, &err), "removing an inactive account works");
    check(accounts::all().size() == 1, "and it is gone from the list");

    check(accounts::add(bob, "bob-token", &err) && accounts::setActive(2, &err),
          "someone else can be added and signed in");
    check(accounts::ownerId() == 1, "the first account added owns the console");
    check(!accounts::remove(1, &err),
          "removing the OWNER is refused, even with someone else signed in");
    check(accounts::setActive(1, &err) && accounts::remove(2, &err),
          "and the other one can go once the owner is back");

    check(!accounts::setActive(99, &err), "switching to an unknown account is refused");

    // The list and the tokens can disagree if somebody has been in here by
    // hand. That has to refuse rather than sign the console out silently.
    accounts::add(bob, "bob-token", &err);
    ::unlink(accounts::tokenPath(2).c_str());
    check(!accounts::setActive(2, &err), "switching to an account whose token is gone is refused");


    check(!accounts::pinIsSet(), "no PIN by default");
    check(!accounts::checkPin("0000"), "and nothing matches when none is set");
    check(accounts::setPin("4821", &err), "a PIN can be set");
    check(accounts::pinIsSet() && accounts::checkPin("4821"), "and it matches");
    check(!accounts::checkPin("4822"), "and a wrong one does not");
    check(accounts::setPin("", &err) && !accounts::pinIsSet(), "an empty PIN clears it");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good",
                failures, failures == 1 ? "" : "s");
    std::printf("scratch root left at %s\n", root);
    return failures ? 1 : 0;
}

// Every combination of facts the flow can be handed, walked to the end.
//
// WHY THIS EXISTS RATHER THAN A DEMONSTRATION ON ONE MACHINE. The reference
// console is online over a cable with a paired token, so walking it there
// proves the happy path and nothing else — and the rules that matter most are
// the REFUSALS. Open question 15b's whole argument is that first run is a
// linear path with no skip, because on the far side of a skip there is nothing
// to show. A rule like that is only worth anything if it cannot be got round,
// and the way to know is to try every way round it rather than to read the code
// again.
//
// It needs no network, no server, no pad and no screen, so it runs in CI and it
// runs on a laptop.
int firstRunRules() {
    int cases = 0, failures = 0;
    auto fail = [&](const char* what, const firstrun::Facts& f) {
        ++failures;
        std::printf("  FAIL  %s\n        online=%d wired=%d wifiHw=%d wifiUp=%d "
                    "addr=%d checked=%d answered=%d token=%d pads=%d\n",
                    what, f.online, f.wiredOnline, f.wifiPresent, f.wifiConfigured,
                    f.haveServerAddress, f.serverChecked, f.serverAnswered,
                    f.havePairedToken, f.gamepadCount);
    };

    for (int bits = 0; bits < 512; ++bits) {
        firstrun::Facts f;
        f.online          = bits & 1;
        f.wiredOnline     = bits & 2;
        f.wifiPresent     = bits & 4;
        f.wifiConfigured  = bits & 8;
        f.haveServerAddress = bits & 16;
        f.serverAnswered  = bits & 32;
        f.havePairedToken = bits & 64;
        f.gamepadCount    = (bits & 128) ? 1 : 0;
        f.serverChecked   = bits & 256;

        // Nonsense the machine can never be handed: a wired link that is up
        // while nothing is online, or a radio connected with no radio. Skipped
        // rather than asserted about, because a rule about an impossible state
        // is a rule nobody can act on.
        if (f.wiredOnline && !f.online) continue;
        if (f.wifiConfigured && !f.wifiPresent) continue;
        if (f.wifiConfigured && !f.online) continue;
        // net::status() only ever reports online because of a wired device or a
        // radio, so "online by neither" cannot be handed to the machine. It is
        // skipped rather than asserted about — but the Wi-Fi gate is written to
        // survive it anyway, because that is one net.cpp change away from being
        // reachable.
        if (f.online && !f.wiredOnline && !f.wifiConfigured) continue;
        // A server cannot have answered without having been asked.
        if (f.serverAnswered && !f.serverChecked) continue;
        ++cases;

        firstrun::Machine m;
        m.update(f);

        int guard = 0;
        for (; guard < 16 && !m.finished(); ++guard) {
            const firstrun::Gate g = m.gate();

            // Every refusal explains itself. A setup screen that stops with no
            // sentence is worse than a black one.
            if (g == firstrun::Gate::Blocked && m.because().empty())
                fail("a Blocked step with nothing to say", f);

            // advance() and skip() are the only enforcement there is, so they
            // must refuse on anything but their own gate.
            if (g != firstrun::Gate::Ready) {
                firstrun::Machine copy = m;
                if (copy.advance()) fail("advance() got past a gate that was not ready", f);
            }
            if (g != firstrun::Gate::Skippable) {
                firstrun::Machine copy = m;
                if (copy.skip()) fail("skip() got past a step that may not be skipped", f);
            }

            if (g == firstrun::Gate::Blocked) break;
            if (g == firstrun::Gate::Skippable) m.skip();
            else m.advance();
        }
        if (guard >= 16) fail("the chain did not terminate", f);

        // THE THREE HARD GATES. Finishing setup without any one of them is a
        // console with nothing to show, which is the thing 15b forbids.
        if (m.finished()) {
            if (!f.online)          fail("finished setup while offline", f);
            if (!f.serverAnswered)  fail("finished setup with no server answering", f);
            if (!f.havePairedToken) fail("finished setup with no token", f);
        }

        // A machine with a radio, no cable and no Wi-Fi must not get past the
        // network step at all.
        if (!f.online && m.step() != firstrun::Step::Network)
            fail("got past the network step while offline", f);

        // The one soft gate, and it has to STAY soft: refusing to finish
        // without a controller would break the keyboard guarantee the whole
        // flow exists to make.
        if (f.online && f.serverAnswered && f.havePairedToken && !m.finished())
            fail("a fully configured machine could not finish setup", f);
    }

    // WHICH SCHEME THE SERVER STEP TRIES FIRST (#479): http for an address
    // that is the person's own, Tailscale's 100.64.0.0/10 included, and https
    // for the rest. The wrong one first cost a try at every heartbeat.
    const struct { const char* host; bool local; } schemes[] = {
        {"100.101.1.2", true},   {"100.101.1.2:8080", true}, {"100.64.0.1", true},
        {"100.127.255.254", true}, {"100.128.0.1", false},   {"100.63.0.1", false},
        {"192.168.1.10:6005", true}, {"romm", true},         {"romm.example.com", false},
    };
    for (const auto& c : schemes) {
        ++cases;
        if (romm::looksLocal(c.host) != c.local) {
            ++failures;
            std::printf("  FAIL  looksLocal(\"%s\") is not %s\n", c.host,
                        c.local ? "true" : "false");
        }
    }

    std::printf("first-run rules: %d fact combinations, %d failure%s\n", cases,
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
