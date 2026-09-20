#include "player/player_types.h"

#include <cstdio>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    else
        std::fprintf(stdout, "PASS: %s\n", message);
    return condition;
}

mhw::PartyMemberSnapshot member(const char *name, bool local, int slot)
{
    mhw::PartyMemberSnapshot value;
    value.name = QString::fromUtf8(name);
    value.local = local;
    value.slot = slot;
    value.damage = slot + 10;
    return value;
}

} // namespace

int main()
{
    const QVector<mhw::PartyMemberSnapshot> party{
        member("Remote", false, 0),
        member("Self", true, 1),
        member("Follower", false, 2),
    };
    const auto localOnly = mhw::visibleDamageParty(party, false);
    const auto all = mhw::visibleDamageParty(party, true);
    bool ok = true;
    ok &= check(localOnly.size() == 1 && localOnly[0].name == QStringLiteral("Self"),
                "other-member filter keeps only local damage row");
    ok &= check(all.size() == 3, "enabled filter preserves complete damage roster");
    return ok ? 0 : 1;
}
