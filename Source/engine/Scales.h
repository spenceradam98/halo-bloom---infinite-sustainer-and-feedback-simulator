// Scales and tunings. A scale is stored the way Scala (.scl) stores it: the pitches of
// degrees 1..n in cents above the root, ascending, where the last entry is the period
// (1200 for octave-repeating scales, 1901.955 for Bohlen-Pierce, and so on).
// Degree 0 is the root. Plain C++, fixed capacity, safe to copy on the audio thread.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace eb
{
struct Scale
{
    static constexpr int kMaxSteps = 128;
    std::array<double, kMaxSteps> steps {};   // cents of degrees 1..n; steps[n-1] is the period
    int n = 0;
    std::array<char, 64> name {};

    double period() const { return n > 0 ? steps[(size_t) (n - 1)] : 1200.0; }

    // Cents above the root for any integer degree (negative degrees go below the root).
    double degreeCents (int k) const;

    // Nearest degree to a pitch given in cents above the root.
    int nearestDegree (double cents) const;

    bool isValid() const { return n > 0 && period() > 0.0; }
    std::string getName() const { return std::string (name.data()); }
    void setName (const std::string& s);

    static Scale fromCents (const std::string& name, const std::vector<double>& cents);
    static Scale edo (const std::string& name, int divisions, double periodCents = 1200.0);
};

// Parse the text of a Scala .scl file. Returns false (and sets error) if malformed.
bool parseScala (const std::string& text, Scale& out, std::string& error);

// Built-in scales, in the order they appear in the Scale parameter.
const std::vector<Scale>& builtinScales();

// Category label for the UI menu ("12-TET modes", "Just intonation", ...), per builtin index.
const char* builtinCategory (int index);

const char* noteName (int pitchClass);   // 0 = C
} // namespace eb
