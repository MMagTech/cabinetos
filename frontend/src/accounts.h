// The accounts this console knows, and which one it is acting as.
//
// No screens, no drawing. The switcher's picture is somewhere else; this is the
// list, the tokens, and the rules about what may be removed.
//
// docs/PROJECT.md open question 26 has the decisions and the reasoning. The
// four that shape this file:
//
//   1. ONE SERVER PER CONSOLE. Every account is a user on the one paired
//      server, so `/etc/cabinetos/session.env` stays machine-wide and an
//      account carries no address of its own.
//   2. The console boots as whoever played last.
//   3. Switch, add, remove and an optional PIN all ship together.
//   4. The PIN is off by default.
//
// --- WHY THE KEY IS RomM's OWN USER ID -------------------------------------
//
// The reference implementation keys a profile on a locally-generated UUID, and
// has to: `TVProfile` carries its own server URL, so two profiles can sit on
// one host and collide on a host-keyed token. Deciding on one server per
// console removes that case entirely, and the id RomM already issues is then
// unique across everything this machine will ever see.
//
// The payoff is not tidiness. `storage::userDir` is `users/<id> - <name>`,
// keyed on that same id, so the account list and the save tree agree without
// anything translating between them — and the save tree was built before this
// feature was discussed.
//
// **IF ONE SERVER PER CONSOLE IS EVER REVERSED, THE FOLDER NAME BREAKS FIRST.**
// User 1 on two different servers is one directory, and `romm.h` already
// records the underlying limit: two RomM instances both have a User:1 and RomM
// exposes no instance identity to tell them apart. Say so out loud before
// changing this.
//
// --- WHY THE TOKEN AND THE LIST LIVE IN DIFFERENT PLACES --------------------
//
// A token is a credential and lives under `$HOME/.config/cabinetos/`, 0600, the
// same tier the single token already used. The list is not a credential — it is
// cached identity, the same kind of thing as `config/user.json` — so it lives
// beside that, under the storage root, where it survives with the save tree
// rather than with the session user's home directory.
//
// That split is the reference implementation's too: profiles in UserDefaults,
// tokens in the Keychain.

#pragma once

#include <string>
#include <vector>

namespace romm { class Client; }

namespace accounts {

// One RomM account this console has been paired to.
//
// No server address, deliberately: see the header comment. If a field ever
// needs to be added here, ask whether it belongs to the ACCOUNT or to the
// MACHINE first — that is the distinction this whole feature rests on.
struct Account {
    // RomM's own user id, from /api/users/me. The key, and the same id
    // `storage::User` uses for the save directory.
    int id = 0;
    std::string name;
    // The endpoint that actually serves the picture, not RomM's reported
    // `avatar_path` — see `romm::User::avatarPath` for why those differ and
    // which one is a 404. Empty is normal and means a lettered disc.
    std::string avatar;

    bool valid() const { return id > 0; }
};

// Where things are. Exposed for the probes and for tests; nothing else should
// need to build these paths itself.
std::string listPath();              // <storage config>/accounts.json
std::string tokenPath(int id);       // ~/.config/cabinetos/accounts/<id>.json
std::string legacyTokenPath();       // ~/.config/cabinetos/romm.json

// --- Reading ---------------------------------------------------------------

// Every account, in the order they were added. Re-read from disk each call;
// this list is small and never hot.
std::vector<Account> all();

// Which account the console is acting as, or 0 when it has never been set.
int activeId();

// One account by id, or nullptr. The pointer is into a caller-owned vector, so
// this takes the list rather than hiding a static.
const Account* find(const std::vector<Account>& list, int id);

// --- Writing ---------------------------------------------------------------

// Records a freshly-paired account and its token. Does NOT make it active:
// adding somebody must not sign out whoever is already playing, which is the
// reference implementation's rule and the right one.
//
// Fails if `a` is not valid or the token is empty, because a half-written
// account is worse than none — see `needsSetup` in firstrun.cpp, which decides
// whether this console has been configured by looking for a token and a user.
bool add(const Account& a, const std::string& token, std::string* err);

// Forgets an account and its token.
//
// REFUSES THE ACTIVE ACCOUNT, and that is not a UI convenience: removing who
// the console is signed in as leaves it holding an identity whose token has
// just been deleted, with no way back except re-pairing. The switcher disables
// that row too; this is the enforcement rather than the hint.
//
// Does NOT touch `users/<id> - <name>/`. Forgetting a login is not the same as
// throwing away somebody's saves, and the second one needs its own deliberate
// act somewhere that says so.
bool remove(int id, std::string* err);

// Makes `id` the account the console acts as. Writes the choice down; it does
// NOT re-point the client or reload anything — that is `activate`.
bool setActive(int id, std::string* err);

// Updates a name or avatar in place, for when /api/users/me answers after the
// account was first written. Silent no-op for an unknown id.
bool update(int id, const std::string& name, const std::string& avatar);

// --- Switching -------------------------------------------------------------

// Puts `id`'s token into `client` and records it as active.
//
// **THIS IS HALF OF A SWITCH AND THE SMALLER HALF.** It changes who the console
// will be the next time anything asks. It does not re-resolve the user, does
// not refetch the library, and above all does not clear what the previous
// account put on the screen. See open question 26: Home, favourites and recents
// are the server's and belong to the account, and this console also holds a
// per-user download queue, pending uploads and a keep list. The caller owns
// that teardown, and it has to happen before anything is drawn again.
bool activate(int id, romm::Client& client, std::string* err);

// --- Adoption of the single token that came before this file ----------------

// True when there is no account list but the old single token is on disk.
//
// Every console installed before this feature is in that state, including both
// machines here. Nobody should have to re-pair to gain something they did not
// ask for.
bool needsAdoption();

// Files the existing `~/.config/cabinetos/romm.json` as the first account,
// under whatever id the server says it belongs to, and makes it active.
//
// `who` must have come from /api/users/me rather than from the cached
// `config/user.json`: filing a credential under a guessed id is how somebody
// ends up writing into another person's save directory. The caller resolves the
// user first and passes the answer in.
//
// The old file is left where it is. It costs nothing, and a console that has to
// be rolled back to an image without accounts still needs it.
bool adoptLegacyToken(const Account& who, std::string* err);

// --- The PIN ---------------------------------------------------------------
//
// WHAT THIS IS AND IS NOT WORTH, stated plainly because the file it writes is
// readable. There is no crypto library in this binary's link line, so the PIN
// is stored as it was typed, in a 0600 file under the session user's home.
//
// **It is a deterrent against a sibling with a controller, which is the threat
// it exists for.** It is NOT a secret against somebody with a shell, and it was
// never going to be: developer mode hands out SSH deliberately, and open
// question 9 gives a person their own files over SFTP on purpose. Anybody who
// can read this file could read the token beside it, which is worth more.
//
// Do not "fix" this with a hand-rolled hash. An unreviewed digest in this tree
// would buy nothing against that threat model and would look like it did.

bool pinIsSet();
// An empty string clears it, which is how the PIN is turned off.
bool setPin(const std::string& pin, std::string* err);
// False when no PIN is set, so a caller cannot accidentally gate on a console
// that has none — check `pinIsSet` to decide whether to ask at all.
bool checkPin(const std::string& pin);

}  // namespace accounts
