#include "mhw_reader.h"

namespace mhw {

QuestSnapshot MhwReader::readQuest(QString *error)
{
    QuestSnapshot result;
    const std::uintptr_t quest = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_DATA_OFFSETS")), error);
    if (!quest)
        return result;

    if (const auto id = memory_.read<std::int32_t>(quest + 0x4C))
        result.id = *id;
    if (const auto stars = memory_.read<std::int32_t>(quest + 0x50))
        result.stars = *stars;
    if (const auto state = memory_.read<std::int32_t>(quest + 0x54))
        result.state = *state;
    if (const auto category = memory_.read<std::uint8_t>(quest + 0x7C))
        result.category = *category;

    const std::uintptr_t extra = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_EXTRA_DATA_OFFSETS")), nullptr);
    if (extra) {
        if (const auto data = memory_.read<QuestData>(extra)) {
            result.maxDeaths = data->maxDeaths;
            result.deaths = data->deaths;
        }
    }

    const std::uintptr_t timer = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_TIMER_OFFSETS")), nullptr);
    if (timer) {
        if (const auto ticks = memory_.read<std::uint64_t>(timer))
            result.timeLeftSeconds = static_cast<float>(*ticks) / 60.0F;
    }

    result.active = result.id > 0 && result.state == 2;
    return result;
}


} // namespace mhw