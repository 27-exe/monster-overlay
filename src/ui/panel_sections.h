#pragma once

// Per-panel section visibility masks.
//
// Each overlay panel is composed of independently toggleable "sections"
// (e.g. the player panel's mantle row, the monster panel's part grid).
// The control panel UI flips individual bits in these masks; the panel's
// paintPanel() / height computation consult sectionOn() before emitting
// each block so a disabled section contributes neither pixels nor height.
//
// The masks are deliberately per-panel uint32_t bitfields (not a single
// global enum) because each panel owns a disjoint set of sections and
// stores its own mask. A parallel string table (sectionNames()) lets the
// control panel build its switches by name without knowing the bit layout.
//
// "Master" visibility (whether the whole panel may appear at all) is a
// separate boolean on Panel (panelEnabled()), not a mask bit — it gates
// setVisible() in the main loop rather than individual paint blocks.
//
// i18n: displayNames()/displayName() resolve through mhw::StringTable on
// EVERY call (key namespace console.section.*). They used to return a
// `static const QStringList` — that cache would freeze whichever language
// was active at first use, so a runtime language switch could never
// relabel the existing switches (see ControlPanel::retranslateUi()).
// A missing key falls back to the pre-i18n Chinese literal below.

#include <QString>
#include <QStringList>
#include <cstdint>

#include "core/string_table.h"

namespace mhw {

// ----- Player panel sections ----------------------------------------------
namespace PlayerSection {
enum : uint32_t {
    Conn    = 1u << 0,   // 已连接 · PID · BASE 行
    Quest   = 1u << 1,   // 区域·任务 / 剩余 / 猫车 三行
    Weapon  = 1u << 2,   // 武器槽 + 名字 + MR + 锋利度条
    Bars    = 1u << 3,   // HP + ST 两条
    Mantles = 1u << 4,   // 衣装两格 (World only — Rise hides this)
    Debuff  = 1u << 5,   // 异常状态胶囊
    Buff    = 1u << 6,   // 正面状态胶囊 (笛/道具/技能)
    Wirebug = 1u << 7,   // v0.7.1: 翔虫胶囊 (Rise only — World hides this)
};
constexpr uint32_t kAll =
    Conn | Quest | Weapon | Bars | Mantles | Debuff | Buff | Wirebug;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("conn"),
        QStringLiteral("quest"),
        QStringLiteral("weapon"),
        QStringLiteral("bars"),
        QStringLiteral("mantles"),
        QStringLiteral("debuff"),
        QStringLiteral("buff"),
        QStringLiteral("wirebug"),
    };
    return n;
}

constexpr int kCount = 8;

inline QString displayName(int index)
{
    // Key order matches `names()` (and therefore the bit index).
    static const char *const kKeys[kCount] = {
        "console.section.player.conn",
        "console.section.player.quest",
        "console.section.player.weapon",
        "console.section.player.bars",
        "console.section.player.mantles",
        "console.section.player.debuff",
        "console.section.player.buff",
        "console.section.player.wirebug",
    };
    static const QStringList kFallback = {
        QStringLiteral("连接状态"),
        QStringLiteral("任务块"),
        QStringLiteral("武器 + 锋利度"),
        QStringLiteral("HP / ST"),
        QStringLiteral("衣装"),
        QStringLiteral("异常状态"),
        QStringLiteral("正面状态"),
        QStringLiteral("翔虫"),
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    const QString val = StringTable::instance().tr(key);
    return val == key ? kFallback.value(index) : val;
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}
} // namespace PlayerSection

// ----- Monster panel sections ---------------------------------------------
namespace MonsterSection {
enum : uint32_t {
    Info     = 1u << 0,    // 六角肖像 + 金冠 + 名字 + 大小 + 激怒标签
    Hp       = 1u << 1,    // 总 HP 条
    Enrage   = 1u << 2,    // 怒气计量条
    Ail      = 1u << 3,    // 异常状态卡 (.srow)
    Parts    = 1u << 4,    // 部位网格 (.pgrid)
    Tenderize = 1u << 5,   // v0.7.3: 勾爪软化倒计时胶囊
};
constexpr uint32_t kAll = Info | Hp | Enrage | Ail | Parts | Tenderize;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("info"),
        QStringLiteral("hp"),
        QStringLiteral("enrage"),
        QStringLiteral("ail"),
        QStringLiteral("parts"),
        QStringLiteral("tenderize"),
    };
    return n;
}

constexpr int kCount = 6;

inline QString displayName(int index)
{
    static const char *const kKeys[kCount] = {
        "console.section.monster.info",
        "console.section.monster.hp",
        "console.section.monster.enrage",
        "console.section.monster.ail",
        "console.section.monster.parts",
        "console.section.monster.tenderize",
    };
    static const QStringList kFallback = {
        QStringLiteral("六角肖像"),
        QStringLiteral("HP 条"),
        QStringLiteral("怒气"),
        QStringLiteral("异常"),
        QStringLiteral("部位"),
        QStringLiteral("软化"),
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    const QString val = StringTable::instance().tr(key);
    return val == key ? kFallback.value(index) : val;
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}
} // namespace MonsterSection

// ----- Damage panel sections ----------------------------------------------
namespace DamageSection {
enum : uint32_t {
    Rows  = 1u << 0,     // 玩家行 (name/dmg/dps)
    Share = 1u << 1,     // 横向占比条
    Chart = 1u << 2,     // 折线图
};
constexpr uint32_t kAll = Rows | Share | Chart;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("rows"),
        QStringLiteral("share"),
        QStringLiteral("chart"),
    };
    return n;
}

constexpr int kCount = 3;

inline QString displayName(int index)
{
    static const char *const kKeys[kCount] = {
        "console.section.damage.rows",
        "console.section.damage.share",
        "console.section.damage.chart",
    };
    static const QStringList kFallback = {
        QStringLiteral("玩家行"),
        QStringLiteral("占比条"),
        QStringLiteral("折线图"),
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    const QString val = StringTable::instance().tr(key);
    return val == key ? kFallback.value(index) : val;
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}
} // namespace DamageSection

} // namespace mhw
