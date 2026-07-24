#include "formatters.h"

#include <QStringLiteral>
#include <algorithm>
#include <cmath>

namespace mhw {

QString percentage(float value, float maximum)
{
    if (!std::isfinite(value) || !std::isfinite(maximum) || maximum <= 0.0F)
        return QStringLiteral("--");
    return QStringLiteral("%1%").arg(
        std::clamp(value / maximum * 100.0F, 0.0F, 999.0F), 0, 'f', 1);
}

QString seconds(float value)
{
    if (!std::isfinite(value) || value <= 0.0F)
        return QStringLiteral("--");
    const int total = static_cast<int>(value);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace mhw