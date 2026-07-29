#pragma once

#include <QString>

namespace mhw {

// Returns "XX.X%" or "--" when values are NaN / zero / infinite.
QString percentage(float value, float maximum);

// Returns "MM:SS" or "--" when value <= 0 or NaN.
QString seconds(float value);

// Returns "18,420" — integer with thousands separator (locale-aware).
QString groupNumber(int value);

// Returns "1.25×" — size multiplier rendered to 2 decimals with × glyph.
QString sizeMultiplier(float value);

} // namespace mhw