#include "look.h"

#include <algorithm>

namespace look {

namespace {

struct Swatch {
    const char* name;
    const char* word;
    ui::Gradient backdrop;
    ui::Color surface;
};

// STARTING VALUES, to be judged on the television one by one. Each is the
// purple's shape in another hue: a coloured top, a near-black middle at 0.55,
// and a bottom darker still, with the panel colour a step above the middle.
const Swatch kSwatches[kColourCount] = {
    {"Purple", "purple",
     {ui::palette::kBackdropTop, ui::palette::kBackdropMid, ui::palette::kBackdropBottom, 0.55f},
     ui::palette::kSurface},
    {"Blue", "blue",
     {ui::Color::rgb(0x1C3466), ui::Color::rgb(0x0A1226), ui::Color::rgb(0x050914), 0.55f},
     ui::Color::rgb(0x18263F)},
    {"Teal", "teal",
     {ui::Color::rgb(0x144E56), ui::Color::rgb(0x061B1F), ui::Color::rgb(0x030D10), 0.55f},
     ui::Color::rgb(0x143236)},
    {"Green", "green",
     {ui::Color::rgb(0x1F4A2C), ui::Color::rgb(0x0A1A0F), ui::Color::rgb(0x050D07), 0.55f},
     ui::Color::rgb(0x18301F)},
    {"Amber", "amber",
     {ui::Color::rgb(0x5A3514), ui::Color::rgb(0x1E1208), ui::Color::rgb(0x0F0904), 0.55f},
     ui::Color::rgb(0x3A2614)},
    {"Red", "red",
     {ui::Color::rgb(0x581618), ui::Color::rgb(0x1C0708), ui::Color::rgb(0x0E0405), 0.55f},
     ui::Color::rgb(0x381516)},
    {"Wine", "wine",
     {ui::Color::rgb(0x5A1C3A), ui::Color::rgb(0x1E0A14), ui::Color::rgb(0x10050A), 0.55f},
     ui::Color::rgb(0x3A1828)},
    {"Graphite", "graphite",
     {ui::Color::rgb(0x34363C), ui::Color::rgb(0x121316), ui::Color::rgb(0x08090A), 0.55f},
     ui::Color::rgb(0x26282D)},
};

// HOW FAR DARK TURNS THINGS DOWN. Starting values for the television.
constexpr float kDarkTop = 0.40f;      // the background's coloured top, kept
constexpr float kDarkMid = 0.55f;      // its middle
constexpr float kDarkBottom = 0.70f;   // its bottom, already near black
constexpr float kDarkSurface = 0.60f;  // panels
constexpr float kDarkArtFill = 0.50f;  // the game-art glow, kept
constexpr float kDarkArtScrim = 0.20f; // added to the black over it
constexpr float kDarkRestDim = 0.18f;  // added to the black over a resting cover

constexpr float kChangeSeconds = 1.5f;

const char* const kAppearanceNames[kAppearanceCount] = {"Standard", "Dark", "Scheduled"};
const char* const kAppearanceWords[kAppearanceCount] = {"standard", "dark", "scheduled"};

struct Fade {
    float from = 0, to = 0, t = 1;   // t runs 0 to 1
    void go(float target, bool instant) {
        from = instant ? target : value();
        to = target;
        t = instant ? 1.0f : 0.0f;
    }
    void tick(float dt) { t = std::min(1.0f, t + dt / kChangeSeconds); }
    float value() const {
        const float e = t * t * (3.0f - 2.0f * t);   // ease in and out
        return from + (to - from) * e;
    }
};

Colour gColour = Colour::Purple, gColourFrom = Colour::Purple;
Fade gColourFade;   // 0 is gColourFrom, 1 is gColour
Fade gDark;
bool gIsDark = false;

ui::Color mix(ui::Color a, ui::Color b, float k) {
    return ui::Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
                     a.a + (b.a - a.a) * k};
}
ui::Color scale(ui::Color c, float k) { return ui::Color{c.r * k, c.g * k, c.b * k, c.a}; }

const Swatch& swatch(Colour c) {
    const int i = static_cast<int>(c);
    return kSwatches[(i >= 0 && i < kColourCount) ? i : 0];
}

}  // namespace

const char* colourName(Colour c) { return swatch(c).name; }
const char* colourWord(Colour c) { return swatch(c).word; }
Colour colourFromWord(const std::string& word) {
    for (int i = 0; i < kColourCount; ++i)
        if (word == kSwatches[i].word) return static_cast<Colour>(i);
    return Colour::Purple;
}

const char* appearanceName(Appearance a) {
    const int i = static_cast<int>(a);
    return kAppearanceNames[(i >= 0 && i < kAppearanceCount) ? i : 0];
}
const char* appearanceWord(Appearance a) {
    const int i = static_cast<int>(a);
    return kAppearanceWords[(i >= 0 && i < kAppearanceCount) ? i : 0];
}
Appearance appearanceFromWord(const std::string& word) {
    for (int i = 0; i < kAppearanceCount; ++i)
        if (word == kAppearanceWords[i]) return static_cast<Appearance>(i);
    return Appearance::Standard;
}

bool scheduledDark(int hour, int from, int until) {
    if (from == until) return false;
    if (from < until) return hour >= from && hour < until;
    return hour >= from || hour < until;
}

std::string hourName(int hour) {
    hour = ((hour % 24) + 24) % 24;
    if (hour == 0) return "12 AM";
    if (hour == 12) return "12 PM";
    return std::to_string(hour % 12) + (hour < 12 ? " AM" : " PM");
}

void setColour(Colour c, bool instant) {
    if (instant) {
        gColour = gColourFrom = c;
        gColourFade.go(1.0f, true);
        return;
    }
    if (c == gColour) return;
    // A change mid-fade starts from where it is, near enough: whichever of the
    // two it was closer to.
    gColourFrom = gColourFade.value() < 0.5f ? gColourFrom : gColour;
    gColour = c;
    gColourFade.go(0.0f, true);
    gColourFade.go(1.0f, false);
}

void setDark(bool on, bool instant) {
    if (on == gIsDark && !instant) return;
    gIsDark = on;
    gDark.go(on ? 1.0f : 0.0f, instant);
}

Colour colour() { return gColour; }
bool dark() { return gIsDark; }

void tick(float dt) {
    gColourFade.tick(dt);
    gDark.tick(dt);
}

float darkness() { return gDark.value(); }

ui::Gradient backdrop() {
    const Swatch& a = swatch(gColourFrom);
    const Swatch& b = swatch(gColour);
    const float k = gColourFade.value();
    const float d = darkness();
    ui::Gradient g;
    g.top = scale(mix(a.backdrop.top, b.backdrop.top, k), 1.0f - (1.0f - kDarkTop) * d);
    g.mid = scale(mix(a.backdrop.mid, b.backdrop.mid, k), 1.0f - (1.0f - kDarkMid) * d);
    g.bottom =
        scale(mix(a.backdrop.bottom, b.backdrop.bottom, k), 1.0f - (1.0f - kDarkBottom) * d);
    g.midStop = b.backdrop.midStop;
    return g;
}

ui::Color surface() {
    const float k = gColourFade.value();
    return scale(mix(swatch(gColourFrom).surface, swatch(gColour).surface, k),
                 1.0f - (1.0f - kDarkSurface) * darkness());
}

float artFill(float standard) { return standard * (1.0f - (1.0f - kDarkArtFill) * darkness()); }
float artScrim(float standard) { return std::min(1.0f, standard + kDarkArtScrim * darkness()); }
float restDim(float standard) { return std::min(1.0f, standard + kDarkRestDim * darkness()); }

}  // namespace look
