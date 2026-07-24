#pragma once

namespace mhw {

struct QuestSnapshot {
    int id{};
    int stars{};
    int state{};
    int category{};
    int deaths{};
    int maxDeaths{};
    float timeLeftSeconds{};
    bool active{};
};

} // namespace mhw