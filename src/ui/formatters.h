#pragma once

#include <QString>

namespace mhw {

// Returns "XX.X%" or "--" when values are NaN / zero / infinite.
QString percentage(float value, float maximum);

// Returns "MM:SS" or "--" when value <= 0 or NaN.
QString seconds(float value);

} // namespace mhw