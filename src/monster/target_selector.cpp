#include "target_selector.h"

namespace mhw {

int selectMonsterTarget(const QVector<MonsterSnapshot> &monsters,
                        std::uintptr_t currentAddress)
{
    for (int i = 0; i < monsters.size(); ++i) {
        if (monsters[i].health > 0.0F && monsters[i].isLockOnTarget)
            return i;
    }
    for (int i = 0; i < monsters.size(); ++i) {
        if (monsters[i].health > 0.0F && monsters[i].address == currentAddress)
            return i;
    }
    for (int i = 0; i < monsters.size(); ++i) {
        if (monsters[i].health > 0.0F)
            return i;
    }
    return -1;
}

} // namespace mhw
