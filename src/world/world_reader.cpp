#include "mhw_reader.h"

namespace mhw {

Zone MhwReader::readZone(QString *error)
{
    Zone result = Zone::Unknown;
    const std::uintptr_t zoneAddress = followPointerChain(memory_, absolute(QStringLiteral("ZONE_OFFSET")),
                                                         map_.offsets(QStringLiteral("ZoneOffsets")), error);
    if (!zoneAddress)
        return result;
    const auto value = memory_.read<std::int32_t>(zoneAddress, nullptr);
    if (!value)
        return result;
    const int intValue = static_cast<int>(*value);
    if (intValue < 0 || intValue > 1000)
        return result;
    return static_cast<Zone>(intValue);
}


} // namespace mhw