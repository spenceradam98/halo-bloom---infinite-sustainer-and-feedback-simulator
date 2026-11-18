#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace eb
{
namespace
{
double ratioToCents (double r) { return 1200.0 * std::log2 (r); }

int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

std::string trim (const std::string& s)
{
    const auto b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}

// One pitch line of a .scl file: "701.955", "3/2", "2" (ratio), with optional trailing text.
bool parsePitch (const std::string& line, double& cents)
{
    std::string tok;
    std::istringstream ss (line);
    ss >> tok;
    if (tok.empty()) return false;
    if (tok.find ('.') != std::string::npos)
    {
        char* end = nullptr;
        cents = std::strtod (tok.c_str(), &end);
        return end != tok.c_str();
    }
    long num = 0, den = 1;
    const auto slash = tok.find ('/');
    try
    {
        num = std::stol (tok.substr (0, slash));
        if (slash != std::string::npos) den = std::stol (tok.substr (slash + 1));
    }
    catch (...) { return false; }
    if (num <= 0 || den <= 0) return false;
    cents = ratioToCents ((double) num / (double) den);
    return true;
}

Scale ratios (const char* name, std::initializer_list<double> r)
{
    std::vector<double> c;
    for (double x : r) c.push_back (ratioToCents (x));
    return Scale::fromCents (name, c);
}
} // namespace

void Scale::setName (const std::string& s)
{
    name.fill (0);
    std::strncpy (name.data(), s.c_str(), name.size() - 1);
}

double Scale::degreeCents (int k) const
{
    if (n <= 0) return 100.0 * k;
    const int oct = floorDiv (k, n);
    const int idx = k - oct * n;
    return oct * period() + (idx == 0 ? 0.0 : steps[(size_t) (idx - 1)]);
}

int Scale::nearestDegree (double cents) const
{
    if (n <= 0) return (int) std::lround (cents / 100.0);
    const double p = period();
    const int oct = (int) std::floor (cents / p);
    const double r = cents - oct * p;
    int best = 0;
    double bestDist = std::abs (r);
    for (int i = 1; i <= n; ++i)   // i == n is the root of the next period
    {
        const double d = std::abs (r - steps[(size_t) (i - 1)]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return oct * n + best;
}

Scale Scale::fromCents (const std::string& nm, const std::vector<double>& cents)
{
    Scale s;
    s.setName (nm);
    s.n = (int) std::min<size_t> (cents.size(), kMaxSteps);
    for (int i = 0; i < s.n; ++i) s.steps[(size_t) i] = cents[(size_t) i];
    return s;
}

Scale Scale::edo (const std::string& nm, int divisions, double periodCents)
{
    std::vector<double> c;
    for (int i = 1; i <= divisions; ++i) c.push_back (periodCents * i / divisions);
    return fromCents (nm, c);
}

bool parseScala (const std::string& text, Scale& out, std::string& error)
{
    std::istringstream in (text);
    std::string line, description;
    int expected = -1;
    std::vector<double> cents;
    bool haveDescription = false;

    while (std::getline (in, line))
    {
        if (! line.empty() && line[0] == '!') continue;
        if (! haveDescription) { description = trim (line); haveDescription = true; continue; }
        const auto t = trim (line);
        if (expected < 0)
        {
            if (t.empty()) continue;
            try { expected = std::stoi (t); }
            catch (...) { error = "Missing note count"; return false; }
            if (expected < 1 || expected > Scale::kMaxSteps) { error = "Note count must be 1 to 128"; return false; }
            continue;
        }
        if (t.empty()) continue;
        double c = 0;
        if (! parsePitch (t, c)) { error = "Can't read pitch: " + t; return false; }
        cents.push_back (c);
        if ((int) cents.size() == expected) break;
    }
    if (expected < 0) { error = "Not a Scala file"; return false; }
    if ((int) cents.size() != expected) { error = "File lists fewer pitches than its note count"; return false; }
    for (size_t i = 1; i < cents.size(); ++i)
        if (cents[i] <= cents[i - 1]) { error = "Pitches must ascend"; return false; }
    if (cents.back() <= 0) { error = "Period must be above the root"; return false; }

    out = Scale::fromCents (description.empty() ? "Custom scale" : description, cents);
    return true;
}

const char* noteName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

namespace
{
struct Entry { const char* category; Scale scale; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> list = [] {
        std::vector<Entry> v;
        auto tet = [&] (const char* nm, std::initializer_list<double> semis) {
            std::vector<double> c;
            for (double s : semis) c.push_back (s * 100.0);
            v.push_back ({ "12-TET scales", Scale::fromCents (nm, c) });
        };
        tet ("Major (Ionian)",   { 2, 4, 5, 7, 9, 11, 12 });
        tet ("Natural Minor",    { 2, 3, 5, 7, 8, 10, 12 });
        tet ("Dorian",           { 2, 3, 5, 7, 9, 10, 12 });
        tet ("Phrygian",         { 1, 3, 5, 7, 8, 10, 12 });
        tet ("Lydian",           { 2, 4, 6, 7, 9, 11, 12 });
        tet ("Mixolydian",       { 2, 4, 5, 7, 9, 10, 12 });
        tet ("Locrian",          { 1, 3, 5, 6, 8, 10, 12 });
        tet ("Harmonic Minor",   { 2, 3, 5, 7, 8, 11, 12 });
        tet ("Melodic Minor",    { 2, 3, 5, 7, 9, 11, 12 });
        tet ("Phrygian Dominant",{ 1, 4, 5, 7, 8, 10, 12 });
        tet ("Major Pentatonic", { 2, 4, 7, 9, 12 });
        tet ("Minor Pentatonic", { 3, 5, 7, 10, 12 });
        tet ("Blues",            { 3, 5, 6, 7, 10, 12 });
        tet ("Whole Tone",       { 2, 4, 6, 8, 10, 12 });
        tet ("Chromatic",        { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 });

        auto ji = [&] (Scale s) { v.push_back ({ "Just intonation", s }); };
        ji (ratios ("Just Major (5-limit)", { 9./8, 5./4, 4./3, 3./2, 5./3, 15./8, 2 }));
        ji (ratios ("Just Minor (5-limit)", { 9./8, 6./5, 4./3, 3./2, 8./5, 9./5, 2 }));
        ji (ratios ("Just Chromatic (5-limit)", { 16./15, 9./8, 6./5, 5./4, 4./3, 45./32, 3./2, 8./5, 5./3, 9./5, 15./8, 2 }));
        ji (ratios ("Pythagorean Major", { 9./8, 81./64, 4./3, 3./2, 27./16, 243./128, 2 }));
        ji (ratios ("Septimal Blues (7-limit)", { 7./6, 4./3, 7./5, 3./2, 7./4, 2 }));
        ji (ratios ("Harmonic Series 8-16", { 9./8, 10./8, 11./8, 12./8, 13./8, 14./8, 15./8, 2 }));
        ji (ratios ("Partch Otonal Hexad", { 5./4, 3./2, 7./4, 9./8 * 2, 11./8 * 2, 4 }));   // two-octave period

        auto ed = [&] (Scale s) { v.push_back ({ "Equal divisions", s }); };
        ed (Scale::edo ("24-EDO Quarter Tones", 24));
        ed (Scale::edo ("17-EDO", 17));
        ed (Scale::edo ("19-EDO", 19));
        ed (Scale::edo ("22-EDO", 22));
        ed (Scale::edo ("31-EDO", 31));
        ed (Scale::edo ("53-EDO", 53));
        ed (Scale::edo ("Bohlen-Pierce (13 of 3:1)", 13, ratioToCents (3.0)));
        ed (Scale::fromCents ("Carlos Alpha (78c steps)", { 78.0 }));
        ed (Scale::fromCents ("Carlos Beta (63.8c steps)", { 63.8 }));

        auto w = [&] (const char* nm, std::initializer_list<double> c) {
            v.push_back ({ "World / quarter-tone", Scale::fromCents (nm, std::vector<double> (c)) });
        };
        w ("Maqam Rast",          { 200, 350, 500, 700, 900, 1050, 1200 });
        w ("Maqam Bayati",        { 150, 300, 500, 700, 800, 1000, 1200 });
        w ("Maqam Saba",          { 150, 300, 400, 700, 800, 1000, 1200 });
        w ("Maqam Hijaz",         { 100, 400, 500, 700, 800, 1000, 1200 });
        w ("Maqam Sikah (on E-half-flat)", { 150, 350, 500, 650, 850, 1050, 1200 });
        w ("Neutral Major",       { 200, 350, 500, 700, 850, 1050, 1200 });
        w ("Slendro (approx.)",   { 240, 480, 720, 960, 1200 });
        w ("Pelog (approx.)",     { 120, 270, 540, 670, 785, 950, 1200 });
        w ("Raga Todi (22-shruti)", { 90.2, 294.1, 590.2, 702.0, 792.2, 1088.3, 1200 });
        return v;
    }();
    return list;
}
} // namespace

const std::vector<Scale>& builtinScales()
{
    static const std::vector<Scale> s = [] {
        std::vector<Scale> v;
        for (auto& e : entries()) v.push_back (e.scale);
        return v;
    }();
    return s;
}

const char* builtinCategory (int index)
{
    const auto& e = entries();
    return (index >= 0 && index < (int) e.size()) ? e[(size_t) index].category : "Custom";
}
} // namespace eb
