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
    // v0.12: the weapon IDENTITY TRIO only — slot + name + MR. The bar
    // used to ride on this bit (the original comment read "武器槽 + 名字 +
    // MR + 锋利度条"), which is why it had to be split off: the trio left
    // for its own window while the bar stayed in PlayerPanel, so one bit
    // for both would have made "turn the weapon row off" also hide "am I
    // about to bounce". See Sharpness below.
    //
    // The bar is now in the trio's window again, so the two bits are
    // claimed together by one row of kPlayerSplitTargets — but they are
    // still two bits, because they still answer two questions. One bit
    // for both would put "which window draws it" and "is this block on"
    // behind the same switch, which is the exact confusion the split
    // created the bit to end.
    Weapon  = 1u << 2,
    Bars    = 1u << 3,   // HP + ST 两条
    Mantles = 1u << 4,   // 衣装两格 (World only — Rise hides this)
    Debuff  = 1u << 5,   // 异常状态胶囊
    Buff    = 1u << 6,   // 正面状态胶囊 (笛/道具/技能)
    Wirebug = 1u << 7,   // v0.7.1: 翔虫胶囊 (Rise only — World hides this)
    // v0.12: the sharpness gauge's OWN bit. Weapon used to mean "weapon
    // slot + name + MR + sharpness bar", which is four things behind one
    // toggle; with the identity trio carved into its own window the bar
    // had to be told apart from the trio, or turning the weapon row off
    // in the console would also hide "am I about to bounce".
    //
    // It then spent one release as the sole claim of a window of its own
    // (PlayerSharpnessStripPanel, settingsKey "player-sharpness"), and
    // the second round of user testing said the gauge and the weapon
    // row belong in ONE window — in HunterPie they are one control
    // (WeaponSharpnessView). So the gauge's window is gone and this bit
    // is claimed by the WEAPON row of kPlayerSplitTargets. What did not
    // change is the reason the bit exists: it is still read by its own
    // gate (PlayerWeaponStripPanel::sharpnessEnabled()), so turning it
    // off still answers exactly one question and leaves the trio alone.
    // The bit is about TOGGLING, never about which window draws.
    Sharpness = 1u << 8, // 锋利度条 (独立于 Weapon 位)
};
constexpr uint32_t kAll =
    Conn | Quest | Weapon | Bars | Mantles | Debuff | Buff | Wirebug
    | Sharpness;

// =====================================================================
// v0.12.1: THE BIT-THAT-DID-NOT-EXIST — back-compat for a persisted mask
// written before Sharpness existed
// =====================================================================
//
// THE DEFECT, in the user's own words: 「斩味完全不见了，只有一个单独的
// 武器图标和MR等级」. Its shape is a plain read of the code: v0.12's
// abaa70e added `Sharpness = 1u << 8` while the console's mask rows
// (`player=<hex32>`) are a 32-bit field that had only ever needed bits
// 0-7. Every mask persisted before that commit stores bit 8 as ZERO,
// because there was no such thing to store. panel_player.cpp's prow
// then reads `smask & Sharpness` — false — and skips the gauge, while
// `Weapon` (bit 2, which in the old single bit meant icon + name + MR +
// SHARPNESS TOGETHER) is still set and the identity trio draws.
// Icon + MR with no gauge is exactly what was reported.
//
// WHY THIS IS A DIFFERENT THING FROM THE 4K MARGIN BUG. loadConfig()'s
// iron rule is "never silently rewrite what the user stored", and it
// is correct — a margin has one meaning, it was a real value the user
// picked, and clamping it at read time destroyed it. This is the other
// case, and the distinction is what decides the answer:
//
//   4K margin   : the USER chose that value, at a moment it was valid.
//                 It is stored intent. Rewriting it is data loss.
//
//   missing bit : the user never chose anything. The bit did not exist
//                 in their binary, so their config could not have said
//                 "sharpness off" — the ONLY way to turn the gauge off
//                 back then was the Weapon checkbox, which meant all
//                 four things including the trio. There is no intent to
//                 recover, and the stored ZERO is an artefact of the
//                 bit width, not a decision.
//
// So this is not a read-time rewrite; it is a read-time INTERPRETATION.
// `kAll` is the schema version of the mask field: "these are the
// sections this build knows about". A value that is a subset of the old
// 8-bit schema is an old-schema value, and reading it in the current
// schema means filling the bits that did not exist with what they meant
// before — which for Sharpness is "on, because it rode on Weapon".
//
// WHAT IS *NOT* REWRITTEN. Nothing here changes a stored bit the user
// could actually have set:
//
//   * a bit that is 1 stays 1;
//   * a bit that is 0 and EXISTED before v0.12 stays 0 — so a hunter
//     who had turned Weapon off in a pre-v0.12 console still sees no
//     weapon row and no gauge, exactly as before;
//   * the stored text is untouched. This function is a pure uint32_t
//     transform consumed at startup; serializePanelMasks() writes
//     whatever the console's checkboxes now say, and the console's
//     ninth row shows the migrated state so the user can switch it off
//     for real. Reading a file never writes it.
//
// THE GUARD, and why it must be a guard rather than a blanket OR. A
// blanket `| Sharpness` would also silently un-hide the gauge for a
// user on v0.12 or later who deliberately turned it off, which IS data
// loss of the ordinary kind. The migration therefore fires only when
// the mask is a bit-8-less value — i.e. it has at least one bit set
// inside the OLD schema and none outside it — and it is one-directional:
// a user who turns Sharpness off on the new build writes bit 8 = 0 in a
// mask that already uses a bit the old schema did not have, so
// `migrateOldSharpnessBit` no longer applies and their choice sticks.
//
// This is the same shape as the codec's legacy three-row migration
// (`damageMask |= OtherMembers` in ui/viewmodel/panel_mask_codec.h,
// which fires only when no `pets=` row exists — "this conf predates the
// Pets section"), just one layer lower: that one adds a BITS' worth of
// section, this one decides which old value the new bit inherits. Both
// answer "the user could not have said this, so their silence means what
// it meant in their binary".
[[nodiscard]] constexpr uint32_t migrateOldSharpnessBit(
    uint32_t mask) noexcept
{
    // Everything a pre-v0.12 build could express: bits 0-7.
    constexpr uint32_t kPreSplitMask = Conn | Quest | Weapon | Bars
                                     | Mantles | Debuff | Buff | Wirebug;

    // Not an old-schema value: some bit outside 0-7 is set, so this mask
    // was written by a binary that already knew the new sections. Leave
    // every bit of it alone — including a deliberate bit-8 zero.
    if ((mask & ~kPreSplitMask) != 0u)
        return mask;

    // A fully-zero mask is ambiguous: it is both "old build, nothing
    // hidden" and "user hid everything". The codec's damage migration
    // makes the same choice (`damageMask > 0u`), for the same reason:
    // hiding a whole panel is a real decision that must survive, while
    // nothing being said about it is the default.
    if (mask == 0u)
        return mask;

    // The bar rode on Weapon. If the weapon row was off there was no
    // gauge to show, so honour that instead of resurrecting a block the
    // user had switched off.
    if ((mask & Weapon) == 0u)
        return mask;

    return mask | Sharpness;
}

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
        QStringLiteral("sharpness"),
    };
    return n;
}

constexpr int kCount = 9;

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
        "console.section.player.sharpness",
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    return StringTable::instance().tr(key);
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}

// ----- Player panel window topology (v0.12 dual mode) -----------------
//
// What used to be one CLI flag per extracted section
// (`--split-player-strip`, then `--split-wirebug`, then
// `--split-mantles`, …) is now ONE decision — which window topology the
// user picked — plus ONE table that says which PlayerSection bits each
// split window owns. A third window means a row in the table, not a flag.
//
// The two questions this section answers, and why they are separate:
//
//   1. PlayerLayoutMode  — WHICH WINDOWS exist. The user's choice between
//      one big panel and several small windows. One value for the whole
//      session, resolved once at startup.
//   2. PlayerSection bits — WHICH BLOCKS are on. The user's visibility
//      toggles, per bit, owned by the console's masks.
//
// They must never be conflated. A mask bit answering "is this block on"
// would also be answering "which window draws it", so `--mask-player 0`
// would answer the window question as well and the console's saved
// toggles would silently change the overlay's window topology.
//
// Widget-free on purpose: everything below is constexpr / inline over
// uint32_t, so it is unit-testable from a plain main() and no translation
// unit is added to any target (therefore no zero-reference-gate census
// entry).
//
// Why here and not a new header: this file is already where every
// PlayerSection bit is defined, and the table below is a mapping FROM
// those bits. Splitting the table into its own file would let the enum
// and the mapping of the enum drift apart, which is exactly the failure a
// single declarative table exists to prevent.
//
// Layering: no Qt include beyond the QString already in this header, and
// no include of any widget header. The conf rows are read by main.cpp
// with the console's own codec (ui/viewmodel/panel_mask_fallback.h), so
// the console <-> overlay format contract stays in one place.

// Which window topology the user picked. NOT "is a section split out" —
// that is the per-bit question the table further down answers.
//
// Naming: `LayoutMode` because it names the whole window arrangement
// (one big panel / several small windows), and `Single` / `Split` are the
// user-facing words. Rejected alternatives, one reason each:
// `SplitMode` reads as "the mode of splitting" and implies more modes to
// come; `WindowMode` collides with the window-manager sense of the
// phrase; `Monolithic` is the opposite of the word the user uses.
enum class PlayerLayoutMode {
    Single,   // v0.11.4 behaviour, pixel for pixel. The default.
    Split,    // the sections named by kPlayerSplitTargets move into their
              // own layer-shell windows.
};

// Which split window a bit group belongs to. The index is the subscript
// into kPlayerSplitTargets, so adding a window is adding a row to that
// table plus one entry here — never a new CLI flag, never a new mask
// expression.
namespace PlayerSplitSlot {
enum : int {
    Status = 0,   // 异常 / 强化状态胶囊条 -> PlayerStatusStripPanel
    Mantles = 1,  // 衣装两格 (World only)   -> PlayerMantlesStripPanel
    Wirebug = 2,  // 翔虫胶囊 (Rise only)    -> PlayerWirebugStripPanel
    Bars = 3,     // HP + ST 两条            -> PlayerBarsStripPanel
    // The weapon identity trio (icon + player name + MR). Pointer to
    // its own row in kPlayerSplitTargets below, which is what makes the
    // slot index and the table row the same number: playerSubWindowMask()
    // subscripts the table with this value, so a slot that skipped an
    // index would silently answer 0 ("draw nothing") at runtime with no
    // compile-time complaint beyond the shared row-count assert.
    // 武器身份三件套 + 锋利度条: 图标 + 玩家名 + MR + 7 段色条。ONE
    // window, TWO bits. The gauge left with the trio and then came back
    // — the user's verdict on the arrangement was「武器身份三件套」和
    // 「锋利度条」属于同一个面板, and in HunterPie they are literally
    // one control (HunterPie.UI/Overlay/Widgets/Player/Views/
    // WeaponSharpnessView.xaml, mounted in PlayerHudView.xaml:380-387).
    //
    // TWO BITS DOES NOT MEAN ONE TOGGLE. Claiming the union is what
    // makes ONE window, and the two Users toggles stay independent —
    // Weapon answers "show my weapon row", Sharpness answers "is my
    // blade about to bounce", which is exactly why Sharpness became its
    // own bit in abaa70e. The window reads both through its own
    // weaponEnabled() / sharpnessEnabled() and drops each stream from
    // the width, height and paint on its own
    // (panel_player_split_weapon.cpp). So "off" for either one leaves
    // the other one drawing, exactly as the bits always promised; it
    // only changes where.
    //
    // The width is what the merge buys: the merged window is
    // max(identity line, gauge block) ≈ 218px, where the two separate
    // fragments were ~170 + ~196 = 366 added together. The identity
    // line's leftover width is what the gauge row now gets for free.
    Weapon = 4,   // 武器身份三件套 + 锋利度条   -> PlayerWeaponStripPanel
    // 「任务/进程」: the Quest block and the Conn block as ONE window
    // (design 02-interaction-design.md §4.2 「方案 A」). The only split
    // target that claims two sections, and the only one built around
    // re-typing them: the two halves share a qrow stream in the panel
    // (panel_player.cpp:732-734) and the window gives the PID/BASE meta
    // line its own 11px pitch instead.
    // Same warning as Weapon above: the value MUST be the subscript of the
    // row appended to kPlayerSplitTargets below.
    Info = 5,     // 任务四行 + 进程信息        -> PlayerInfoStripPanel
    // THE SIXTH AND LAST ROW. There used to be a seventh,
    // `Sharpness = 6 -> PlayerSharpnessStripPanel`, added because
    // v0.12's weapon split left the gauge behind and the leftover was a
    // PlayerPanel holding ONE bar in a window that always draws a title
    // row — the user's 「1所说的这个面板真有存在必要么？」. The answer
    // was to give the gauge a window, and a second round of testing said
    // the same thing about THAT: the gauge and the weapon row belong in
    // one window (HunterPie's WeaponSharpnessView is one control), so the
    // row is gone and its `Sharpness` bit joined Weapon's on row 4.
    //
    // `kCount` is 6 now and `static_assert` below re-checks it against
    // the table's row count, so a future row cannot be appended here
    // without the table following.
};
// The row count of kPlayerSplitTargets, cross-checked by static_assert.
constexpr int kCount = 6;
} // namespace PlayerSplitSlot

// One split window's claim on PlayerSection bits: "these blocks leave
// PlayerPanel and are drawn by this window instead".
struct PlayerSplitTarget {
    uint32_t mask;
};

// THE TABLE — the audit's A-2 gap. Declarative, compile-time, and the
// single source of truth for "which bits left PlayerPanel".
//
// main.cpp contains no bit arithmetic at all: it asks this table through
// playerPanelMaskFor() / playerSubWindowMask(). To add a window you add
// ONE row here (plus one panel instance in main.cpp and one
// PlayerSplitSlot entry) and every mask, the log line and the
// "nothing is painted twice" proof follow from the table.
//
// Held to, by static_assert below: every row is a non-empty subset of
// PlayerSection::kAll, and no bit is claimed by two windows — two
// windows drawing one block is the failure a user notices immediately
// and a test may not.
constexpr PlayerSplitTarget kPlayerSplitTargets[] = {
    // 异常状态 / 正面状态胶囊. First out: it maps onto HunterPie's
    // AbnormalityTray and carries the least state of the candidate
    // blocks (audit/06 §0.2).
    {mhw::PlayerSection::Debuff | mhw::PlayerSection::Buff},

    // 衣装两格. World only; a Rise snapshot resolves no slot, so the
    // window stays unmounted there by "no data, no window" rather than
    // by a game branch. Clears the single tallest block out of the panel
    // (kMantleGap + kMantleH = 2 + 60), and the two boxes carry a name
    // column that never fits a 378px panel without either eliding or
    // column arithmetic — as its own window the widest localized name
    // sizes the window instead.
    {mhw::PlayerSection::Mantles},

    // 翔虫胶囊. Rise only; the World reader publishes no wirebugs, so
    // this is the mirror of the mantle row. Same content-driven rule: the
    // row shrinks as wirebugs become ready and collapses entirely when
    // every one of them is, which is a width no panel column can express.
    {mhw::PlayerSection::Wirebug},

    // HP + ST 两条. Both games, both always-resolved whenever the player
    // struct is, so the window is the one split target that never
    // flickers on or off. It is 41 + 7 logical px tall and ~90px wide —
    // the second-largest width return in the audit, paid for by a pair
    // of bars whose numbers, not labels, are what actually matter to a
    // player glancing at them mid-hunt.
    {mhw::PlayerSection::Bars},

    // 武器身份三件套 + 锋利度条: 图标 + 玩家名 + MR + 7 段色条。The
    // merged row. It used to be `Weapon` ALONE, with the gauge claimed
    // by a seventh row further down ("PARTIAL extraction by design —
    // the row in the source panel is 352px and the gauge alone is 174px
    // of hard cost that buys nothing in its own window"), and the gauge
    // then got its own window, and then the user's second round of
    // testing said both fragments belong together: in HunterPie they
    // are ONE control (WeaponSharpnessView.xaml, mounted in
    // PlayerHudView.xaml:380-387), and two windows at two addresses is
    // over-splitting. Both rows are now this one, which claims the
    // UNION of the two bits.
    //
    // The union does NOT merge the toggles: the window reads each bit
    // through its own gate and drops each stream from the width, the
    // height and the paint independently
    // (panel_player_split_weapon.cpp's weaponEnabled() /
    // sharpnessEnabled()). Weapon off keeps the gauge, Sharpness off
    // keeps the trio — the promise the two bits always made, with one
    // fewer window to drag around.
    //
    // The measured width, 170px (probe weapon_row_probe.cpp: wslot 26 +
    // 8 + name + 8 + MR, the widest pair off the shipped demo names), is
    // what the panel's 378 could never give the name — the gauge's
    // centred block left the name column a 49px band and elided every
    // non-empty player name in single mode. The gauge block is 174px
    // wide, and the merged window takes max(170, 174) rather than their
    // sum, which is the compactness the merge is for.
    {mhw::PlayerSection::Weapon | mhw::PlayerSection::Sharpness},
    // 「任务/进程」: the Quest block and the Conn block as ONE window
    // (design 02-interaction-design.md §4.2 「方案 A」). The only split
    // target that claims two sections, and the only one built around
    // re-typing them rather than moving them: in the panel the two halves
    // share one qrow stream (panel_player.cpp:732-734) and so are forced
    // to share its 14px pitch, which is why a diagnostic line — "PID
    // 1234 · BASE 0x7ff…", the thing that answers "is the reader alive?"
    // — cost as much vertical space as a quest name. The window gives
    // the meta line its own 11px pitch and lets the countdown take the
    // large type it deserves.
    //
    // The coupling that kept both behind is real but not blocking: the
    // window re-reads snap.pid / snap.imageBase / snap.party.size()
    // itself rather than the view model's cached copies, and the panel's
    // "not connected" placeholder stays behind (a window cannot show a
    // placeholder row the way a panel can, and the no-data state is a
    // panel-level concern).
    //
    // Worth 82px of the single panel's 361 — 22.7%, the largest single
    // win left in it.
    {mhw::PlayerSection::Conn | mhw::PlayerSection::Quest},
};

// NOT in this table, and why — the audit's remaining candidates are
// named here so the omission is a recorded decision rather than an
// oversight:
//   (none — every PlayerSection bit is claimed. Sharpness used to be
//   NOT claimed, with the recorded reason "left in PlayerPanel on
//   purpose: 174px of gauge + badge with no width return, and the
//   right-edge avoidance arithmetic is welded to the name column that
//   just left". Both halves of that argument expired, and what replaced
//   it was a gauge window of its own, and what replaced THAT was this
//   row: the gauge joins the weapon row on row 4, because in HunterPie
//   they are one control and splitting them was over-splitting. See the
//   Weapon entry above.)
//
// Conn and Quest are NOT listed here any more — they moved, together, in
// the row above. The coupling argument that kept them behind was that
// they are what the console's live preview and the panel's "not
// connected" placeholder read; the window reads the snapshot directly
// and the placeholder stays in the panel, so the argument no longer
// applies. What survives of it is narrower and is written on the row.
//
// (The older note that followed here argued for leaving Conn and Quest
// behind because the console's live preview and the panel's placeholder
// read them. Both are answered on the row: the window reads the snapshot
// itself, and the placeholder is a panel-level state a window cannot
// express. Deleting the argument rather than contradicting it seemed
// better than leaving a decision and its reversal in the same file.)

// Row count of the table, as a constant the functions below index with.
inline constexpr int kPlayerSplitTargetCount =
    static_cast<int>(sizeof(kPlayerSplitTargets) / sizeof(kPlayerSplitTargets[0]));

// OR of every window's claim: exactly the bits that leave PlayerPanel in
// Split mode. Subtracted from the panel's mask panel_player.cpp needs no
// new gate — its existing `smask & PlayerSection::Debuff` / `::Buff`
// reads answer "off", which takes the height reservation, the layout and
// the paint with them.
[[nodiscard]] constexpr uint32_t playerSplitRemovedBits() noexcept
{
    uint32_t bits = 0;
    for (const PlayerSplitTarget &t : kPlayerSplitTargets)
        bits |= t.mask;
    return bits;
}

// The mask PlayerPanel must receive. THE expression the "single-panel
// mode is pixel-identical" promise rests on: in Single mode it returns
// `mask` verbatim, bit for bit, so playerPanel.setSectionMask() is handed
// exactly the value v0.11.4 handed it and no downstream code can detect
// the difference.
[[nodiscard]] constexpr uint32_t playerPanelMaskFor(
    PlayerLayoutMode mode, uint32_t mask) noexcept
{
    return mode == PlayerLayoutMode::Split
               ? static_cast<uint32_t>(mask & ~playerSplitRemovedBits())
               : mask;
}

// The mask split window `slot` must receive: the USER's own toggles
// intersected with what the table says that window owns. A window
// therefore only ever draws blocks the user left on, and PlayerPanel
// only ever loses blocks that moved out.
//
// An out-of-range slot answers 0 (draw nothing) rather than reading past
// the table, so a registry row naming a window the table does not have is
// inert instead of UB.
[[nodiscard]] constexpr uint32_t playerSubWindowMask(
    int slot, uint32_t mask) noexcept
{
    if (slot < 0 || slot >= kPlayerSplitTargetCount)
        return 0;
    return kPlayerSplitTargets[slot].mask & mask;
}

// Compile-time proof that the table says what it claims. Pairwise over
// every row pair, so a future row re-claiming a live bit fails the build
// instead of double-drawing a block on screen.
[[nodiscard]] constexpr bool playerSplitTargetsAreValid() noexcept
{
    for (int i = 0; i < kPlayerSplitTargetCount; ++i) {
        const uint32_t m = kPlayerSplitTargets[i].mask;
        if (m == 0)                                   return false; // no claim
        if ((m & ~mhw::PlayerSection::kAll) != 0)     return false; // not a bit
        for (int j = i + 1; j < kPlayerSplitTargetCount; ++j)
            if ((m & kPlayerSplitTargets[j].mask) != 0) return false; // twice
    }
    return true;
}

static_assert(playerSplitTargetsAreValid(),
              "kPlayerSplitTargets rows must be non-empty, inside "
              "PlayerSection::kAll and pairwise disjoint — two windows "
              "claiming one PlayerSection bit would draw it twice");
static_assert(PlayerSplitSlot::kCount == kPlayerSplitTargetCount,
              "PlayerSplitSlot::kCount must match the kPlayerSplitTargets "
              "row count");
// Bits set in `bits`, counting the claims rather than trusting the OR's
// habit of absorbing a duplicate. Exists for the disjointness proof below.
[[nodiscard]] constexpr int playerSplitBitCount(uint32_t bits) noexcept
{
    int n = 0;
    while (bits != 0) {
        bits &= static_cast<uint32_t>(bits - 1);   // clear the lowest set bit
        ++n;
    }
    return n;
}

// How many bits the table's rows claim in total, counted per row. Unlike
// playerSplitRemovedBits() this does NOT fold the rows through an OR, so
// two rows sharing a bit are counted twice here and once there — and the
// difference between the two counts is exactly the failure being asserted.
[[nodiscard]] constexpr int playerSplitClaimedBitCount() noexcept
{
    int n = 0;
    for (const PlayerSplitTarget &t : kPlayerSplitTargets)
        n += playerSplitBitCount(t.mask);
    return n;
}

// The removed bits are the table's rows OR-ed together, and no row's
// bit is claimed twice. This is the table's one invariants-as-an-assert:
//
//   * it holds for any number of rows and any masks, so adding a window
//     is adding a row and NOT touching a hand-written bit list — the v0.12
//     pilot asserted `== (Debuff|Buff)` here, which every new window had
//     to remember to update, and a forgotten edit failed the build with
//     a message about a value nobody had typed yet;
//   * it FAILS the moment two rows' masks overlap, because the OR drops
//     the duplicate bit and the two counts diverge. Two windows drawing
//     one block is the failure a user notices immediately and a pixel
//     baseline may not, so it belongs at compile time.
static_assert(playerSplitBitCount(playerSplitRemovedBits())
                  == playerSplitClaimedBitCount(),
              "the removed bits must be exactly the table's rows, with no "
              "bit claimed by two windows: an overlapping row's OR would "
              "lose a duplicate and these two counts would diverge");
// The whole pixel-identity argument, as two compile-time checks: in
// Single mode the derived mask IS the input mask.
static_assert(playerPanelMaskFor(PlayerLayoutMode::Single, 0xDEADBEEFu)
                  == 0xDEADBEEFu,
              "Single mode must return the mask unchanged (non-zero case)");
static_assert(playerPanelMaskFor(PlayerLayoutMode::Single, 0u) == 0u,
              "Single mode must return the mask unchanged (zero case)");
// And the split's invariant: the removed bits and what the panel keeps
// share no bit, so exactly one window draws a given block in either mode.
static_assert((playerPanelMaskFor(PlayerLayoutMode::Split, 0xFFFFFFFFu)
               & playerSplitRemovedBits())
                  == 0,
              "in Split mode PlayerPanel must hold none of the removed bits");
} // namespace PlayerSection

// ----- Startup resolution for the layout mode ---------------------------
//
// Same shape as mhw::resolveStartupPanelMask in
// ui/viewmodel/panel_mask_fallback.h — `cli > conf > default`, resolving to a
// value plus the source it came from, with the source as part of the
// contract because it is what makes a wrong-but-right-valued fallback (the
// conf silently overriding an explicit flag) visible to a test rather than
// invisible in production.
//
// Lives at the already-open mhw:: namespace rather than opening a nested
// one, because it answers a STARTUP question about the whole panel, not a
// question about one bit. This file is inside `namespace mhw` from the
// header comment onward, so re-opening it here would nest `mhw::mhw` and
// make every name below ambiguous at the call site.

// Why the mode is what it is. Mirrors PanelMaskSource: the source is not
// debug garnish, it is the half of the answer a test asserts on. `Default`
// is the no-information case, which is also what a malformed row resolves
// to — the two are deliberately the same answer.
enum class PlayerLayoutSource {
    CliExplicit,
    CliCompatFlag,
    Conf,
    Default,
};

struct PlayerLayoutResolution {
    PlayerSection::PlayerLayoutMode mode{PlayerSection::PlayerLayoutMode::Single};
    PlayerLayoutSource source{PlayerLayoutSource::Default};
};

// One parsed CLI request.
//
// `given` false is the only state in which the conf row is consulted.
// `valid` false means the flag arrived but did not parse; it must NOT fall
// through to the conf row, because rescuing a typo'd flag from a saved
// conf would let a stale row override what the user explicitly asked for.
//
// `compatSplit` carries the v0.12 pilot flag, which is folded in HERE rather
// than at the call site so that "two flags, one decision" stays one
// expression with a stated precedence: an explicit --layout= always wins,
// and the boolean alias only speaks when --layout= is absent. A boolean
// cannot outrank a value the user typed on purpose.
// Field order is (given, value, compatSplit) — `valid` is NOT a member.
// It is derived inside the resolver, because "did this parse" is a
// question about the value, not an independent fact the caller knows: a
// caller that filled `value` by hand is the same as one that copied
// parser.value(), and making them maintain a `valid` flag in step with the
// string is exactly the kind of redundant state that desynchronises. The
// one caller that does have an independent notion (it validated the value
// itself) can pass an empty string, which parses to false and takes the
// documented malformed path.
struct PlayerLayoutCliRequest {
    bool given{false};
    QString value;                            // the raw --layout= text
    PlayerSection::PlayerLayoutMode compatSplit{
        PlayerSection::PlayerLayoutMode::Single};   // from --split-player-strip
};

// The conf row, reduced to the single fact the resolver needs: did
// anything usable come out of it, and if so which mode.
//
// The console's conf file has no `layout=` row yet (see the v0.12 CHANGELOG
// "not done yet"), so today this is always the Default. Keeping the
// parameter as a bare PlayerLayoutSource rather than a two-field struct
// means adding the row later is a one-line change here instead of a
// signature change at every call site — and the day someone does add it,
// the resolver's rule 2 already says what to do with a value.
using PlayerLayoutConfRow = PlayerLayoutSource;

// The user-facing spelling of a mode, so the startup log line and the
// console can name the same thing the flag accepts.
[[nodiscard]] inline QString playerLayoutModeToString(
    PlayerSection::PlayerLayoutMode mode) noexcept
{
    return mode == PlayerSection::PlayerLayoutMode::Split
               ? QStringLiteral("split")
               : QStringLiteral("single");
}

// Parse one --layout= value. An unrecognised spelling is NOT Single: it
// returns false so the caller can warn and fall back, which is the same
// handling an invalid --mask-* hex value already gets in main.cpp.
[[nodiscard]] inline bool parsePlayerLayoutMode(
    const QString &text, PlayerSection::PlayerLayoutMode &out) noexcept
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("single")) {
        out = PlayerSection::PlayerLayoutMode::Single;
        return true;
    }
    if (t == QLatin1String("split")) {
        out = PlayerSection::PlayerLayoutMode::Split;
        return true;
    }
    return false;
}

// Map the v0.12 pilot flag --split-player-strip onto the layout question
// it was really asking. The flag is a BOOLEAN, so it can only ever say
// Single or Split; that is exactly the whole enum, which is why it is kept
// as a compatibility alias rather than a separate mode.
[[nodiscard]] inline PlayerSection::PlayerLayoutMode splitPlayerStripFlagSense(
    bool flagSet) noexcept
{
    return flagSet ? PlayerSection::PlayerLayoutMode::Split
                   : PlayerSection::PlayerLayoutMode::Single;
}

// The decision. Pure: no filesystem, no QCommandLineParser, no widget, so
// the whole chain is testable from a plain main().
//
// The chain is `cli > conf > default`, and the default is Single. That
// default is the pixel-identity guarantee's first half: a user who has
// never heard of --layout= gets one big window, which is what every
// release before v0.12 painted.
[[nodiscard]] inline PlayerLayoutResolution resolveStartupLayoutMode(
    const PlayerLayoutCliRequest &cli, const PlayerLayoutConfRow &conf)
{
    // Rule 1 — an explicit --layout= wins over everything, malformed or not.
    // A malformed value resolves to the DEFAULT rather than to the conf row.
    if (cli.given) {
        PlayerSection::PlayerLayoutMode parsed{};
        if (parsePlayerLayoutMode(cli.value, parsed))
            return {parsed, PlayerLayoutSource::CliExplicit};
        return {PlayerSection::PlayerLayoutMode::Single, PlayerLayoutSource::Default};
    }

    // Rule 1b — the v0.12 boolean alias. It speaks only when --layout= is
    // absent, and it cannot say more than Single or Split, which is the
    // whole enum. Checked before the conf row so a user who has been using
    // --split-player-strip since the pilot keeps getting what they asked
    // for even if a conf row later disagrees.
    if (cli.compatSplit == PlayerSection::PlayerLayoutMode::Split)
        return {PlayerSection::PlayerLayoutMode::Split,
                PlayerLayoutSource::CliCompatFlag};

    // Rule 2 — the conf row. `Default` covers all three "nothing usable"
    // cases at once: the row is absent, unread, or malformed. A future
    // `layout=` row reports `Conf` when it parsed; anything else it manages
    // to report is treated as no information, so a hand-mangled conf can
    // never select a window topology the user did not ask for.
    if (conf == PlayerLayoutSource::Conf)
        return {PlayerSection::PlayerLayoutMode::Split,
                PlayerLayoutSource::Conf};

    // Rule 3 — nothing usable was said: one big window.
    return {PlayerSection::PlayerLayoutMode::Single, PlayerLayoutSource::Default};
}

// ----- Monster panel sections ---------------------------------------------
namespace MonsterSection {
enum : uint32_t {
    Info     = 1u << 0,    // 六角肖像 + 金冠 + 名字 + 大小 + 激怒标签
    Hp       = 1u << 1,    // 总 HP 条
    Enrage   = 1u << 2,    // 怒气计量条
    Ail      = 1u << 3,    // 异常状态卡 (.srow)
    Parts    = 1u << 4,    // 部位网格 (.pgrid)
    Tenderize = 1u << 5,   // v0.7.3: 勾爪软化倒计时胶囊
    // v0.12 (monster split): 怪物耐力/疲劳条 (.bar.st) — its OWN bit.
    //
    // It has never had one. v0.8.4-r23 added the gauge and parked it
    // behind `Hp` ("Follows the HP toggle until a dedicated section bit
    // exists", panel_monster.cpp's stamDrawn), which is fine for one
    // panel and useless for a window of its own: the v0.12 split gives
    // the fatigue meter its own layer-shell surface
    // (MonsterStaminaStripPanel, settingsKey "monster-stamina"), and a
    // window whose visibility rides on another window's bit cannot be
    // toggled on its own. HunterPie draws it as its own gauge
    // (BossMonsterWidget's Stamina bar, directly under the HP bar), so
    // "is my monster out of stamina" is one question and "how hurt is
    // it" is another.
    //
    // APPENDED AT BIT 6, NOT INSERTED. The console builds its switch
    // rows from names()/displayName() in index order and writes each
    // row's state as `1u << b` (control_panel.cpp:196), so the ith name
    // and the ith bit are the same thing by construction. Inserting
    // Stamina anywhere but the end would rename every existing row in
    // the console while the enum kept the old values — the switch
    // labelled "耐力" would write the Enrage bit.
    Stamina = 1u << 6,
};
constexpr uint32_t kAll = Info | Hp | Enrage | Ail | Parts | Tenderize
                        | Stamina;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("info"),
        QStringLiteral("hp"),
        QStringLiteral("enrage"),
        QStringLiteral("ail"),
        QStringLiteral("parts"),
        QStringLiteral("tenderize"),
        QStringLiteral("stamina"),
    };
    return n;
}

constexpr int kCount = 7;

inline QString displayName(int index)
{
    static const char *const kKeys[kCount] = {
        "console.section.monster.info",
        "console.section.monster.hp",
        "console.section.monster.enrage",
        "console.section.monster.ail",
        "console.section.monster.parts",
        "console.section.monster.tenderize",
        "console.section.monster.stamina",
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    return StringTable::instance().tr(key);
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}

// =====================================================================
// v0.12 (monster split): THE BIT-THAT-DID-NOT-EXIST, take two —
// back-compat for a persisted `monster=` mask written before the
// fatigue gauge had a bit.
// =====================================================================
// The shape is PlayerSection::migrateOldSharpnessBit's above, and the
// reasoning applies verbatim; what differs is stated here.
//
// THE DEFECT: every `monster=` row written before this change stores bits
// 0-5, with bit 6 zero because there was no bit 6 to store. Reading that
// verbatim in a build that gives the fatigue gauge its own bit (and its
// own window) makes 「怪物耐力条不见了」 — a gauge the user has been
// looking at since v0.8.4-r23 disappearing on the first launch after an
// upgrade, with nothing in the UI to explain it.
//
// WHAT IS INTERPRETED, NOT REWRITTEN: the stored text is untouched and no
// function here writes anything. This is a read-time transform consumed
// at startup, exactly like the Sharpness one. The console's next save
// writes whatever its checkboxes now say, and the console's new 「耐力」
// row shows the migrated state, so a user who truly wants the gauge off
// can turn it off for real. One bit further along than Sharpness, the
// same argument: the user could not have expressed "stamina off" before
// the bit existed.
//
// WHY THE GUARD MATTERS MORE HERE THAN FOR SHARPNESS: Sharpness is bit 8,
// a whole byte above the old schema's top bit, so the "some bit outside
// the old schema is set" test clears almost every new-schema mask for
// free. Stamina is bit 6 of a mask whose other five bits are all in
// active use, so the guard has to be tested properly and one bit is the
// only thing it protects. See the body.
[[nodiscard]] constexpr uint32_t migrateOldMonsterStaminaBit(
    uint32_t mask) noexcept
{
    // Everything a pre-v0.12 monster build could express: bits 0-5. The
    // Stamina bit is deliberately NOT in this set — that is the bit
    // being tested for.
    constexpr uint32_t kPreSplitMask = Info | Hp | Enrage | Ail | Parts
                                    | Tenderize;

    // Not an old-schema value: some bit outside 0-5 is set, so this mask
    // was written by a binary that already knew the fatigue bit. Leave
    // every bit alone — including a deliberate bit-6 zero.
    if ((mask & ~kPreSplitMask) != 0u)
        return mask;

    // A fully-zero mask is ambiguous ("old build, nothing hidden" vs
    // "user hid everything") and resolves the way the Sharpness one
    // does: hiding a whole panel is a real decision that must survive.
    if (mask == 0u)
        return mask;

    // The gauge rode on Hp (panel_monster.cpp's
    // `stamDrawn = onHp && maxStamina > 0`). A hunter who had turned the
    // HP bar off saw no fatigue meter either, so honour that instead of
    // resurrecting a block inside a panel they had switched off.
    if ((mask & Hp) == 0u)
        return mask;

    return mask | Stamina;
}
} // namespace MonsterSection

static_assert(MonsterSection::kCount == 7);
static_assert(MonsterSection::kAll == 0x7Fu);

// ----- Damage panel sections ----------------------------------------------
namespace DamageSection {
enum : uint32_t {
    Rows         = 1u << 0,     // 玩家行 (name/dmg/dps)
    Share        = 1u << 1,     // 横向占比条
    Chart        = 1u << 2,     // 折线图
    OtherMembers = 1u << 3,     // 队友 / 盟友行
};
constexpr uint32_t kAll = Rows | Share | Chart | OtherMembers;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("rows"),
        QStringLiteral("share"),
        QStringLiteral("chart"),
        QStringLiteral("otherMembers"),
    };
    return n;
}

constexpr int kCount = 4;

inline QString displayName(int index)
{
    static const char *const kKeys[kCount] = {
        "console.section.damage.rows",
        "console.section.damage.share",
        "console.section.damage.chart",
        "console.section.damage.other_members",
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    return StringTable::instance().tr(key);
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

// ============================================================================
// Damage panel window topology (v0.12 third wave: monster / damage split)
//
// The player split's pattern, applied to the DPS panel. ONE enum value
// still decides which windows exist (PlayerLayoutMode, reused verbatim —
// see below) and ONE table says which DamageSection bits each window
// owns. A third window means a row in the table, not a flag.
//
// The five claims this section is held to, all by static_assert at the
// bottom of this file:
//
//   1. Single mode returns the mask UNCHANGED. This is the whole
//      pixel-identity promise behind the ten single-mode baselines,
//      all_damage.png included, and it is the arm every pre-split user
//      resolves through.
//   2. Every row is a non-empty subset of DamageSection::kAll.
//   3. No bit is claimed by two windows, counted per row — a duplicate
//      bit's OR would be absorbed and the two counts would diverge.
//   4. In Split mode the panel and every window claim disjoint bits,
//      and their union is exactly what the user toggled.
//   5. The removed bits are exactly the table's rows.
//
// Widget-free on purpose, for the reason the player table is:
// everything below is constexpr / inline over uint32_t, so it is
// unit-testable from a plain main() and adds NO translation unit to any
// target — therefore no zero-reference-gate census entry either.
// ============================================================================

// ---- Which split window a DamageSection bit group belongs to. ----
//
// The index is the subscript into kDamageSplitTargets, so adding a
// window is adding a row to that table plus one entry here — never a
// new CLI flag, never a new mask expression.
//
// This reuses the PLAYER table's PlayerLayoutMode rather than opening a
// DamageLayoutMode. The value names the whole window arrangement (one
// big panel / several small windows), not which panel it is about, and
// one enum answering "which topology" cannot disagree with itself
// between two panels: --layout=split has to mean the same topology for
// both, and the player split's resolveStartupLayoutMode() already owns
// the cli > compat-flag > conf > default chain that resolves it. A second
// enum would be a second precedence chain, and the day the two resolved
// differently a user running --layout=split would get five player
// windows and one un-split damage panel.
//
// Rows and the bits they claim:
//
//   Share=1        -> DamageShareStripPanel  (panel_damage.cpp:422-430)
//   Chart=2        -> DamageChartStripPanel  (panel_damage.cpp:432-434)
//   OtherMembers=3 -> DamageChartStripPanel's roster only, via the
//                     section mask this shared header reads. It is NOT
//                     claimed by a row of its own; see the comment
//                     below on why the follower half stays with the
//                     panel rather than becoming a third window.
//
// WHY ONLY THESE TWO ROWS. The user's ask「同样的对怪物和dps进行拆分」
// is a window count, and these are the only two blocks whose
// WIDTH-OR-ASPECT the panel was actively getting wrong:
//
//   * the share bar was pinned to the panel's own 380px, so four
//     players always produced four ~74px strips whether the user wanted
//     a bar or a colour budget;
//   * the chart lost kChartLeft (36) of its 358 to the y-axis labels, so
//     the four curves shared 318px no matter how the panel was resized
//     — widening the panel moved the labels but the plot area grew by
//     the same amount regardless of what the curves needed.
//
// Neither is true of the other two blocks, which is the argument that
// grounds the design:
//
//   * Rows (bit 0) is the DPS panel's SUBSTANCE — name / weapon icon /
//     MR / total / DPS. Splitting it would leave a panel that mounts as
//     an empty title row, which is precisely the「1所说的这个面板真有
//     存在必要么？」the player split's Sharpness removal answered by
//     NOT doing it. A panel whose only remaining content is a title
//     should have its title merged, not its substance removed.
//   * OtherMembers (bit 3) is the roster filter, not a block. It exists
//     as a bit because riseDisplayOptions.showOtherMembers is derived
//     from it at startup (main.cpp:433-434) and the ViewModel's own
//     actor filter reads that option — the bit decides WHICH ROWS the
//     panel and the windows draw. Its per-row rendering is Rows', so a
//     window claiming it would claim the same pixels the rows window
//     claims, and static_assert 3 would fail the build. That is not an
//     oversight to work around: it is the table doing its job.
namespace DamageSplitSlot {
enum : int {
    Share = 0,   // 横向占比条 -> DamageShareStripPanel
    Chart = 1,   // 折线图      -> DamageChartStripPanel
};
// The row count of kDamageSplitTargets, cross-checked by static_assert.
//
// v0.15: this is DELIBERATELY still 2, and the rows window does NOT get
// an entry here. The reason is that this count is the index space of
// kDamageSplitTargets — damageSubWindowMask() bounds-checks against it
// and then reads the table with it — so growing it without growing the
// table would give every caller an out-of-range read.
//
// The rows window (DamageRowsStripPanel) lives BESIDE this table rather
// than in it, because it claims no bit: damagePanelMaskFor() leaves
// DamageSection::Rows with the panel, so adding a `{DamageSection::Rows}`
// row here would take the rows away from DamagePanel and change what a
// real `--layout=split` launch draws. That is a main.cpp decision, not a
// console-preview one.
//
// See kSplitDamageRowsRow in control_panel.cpp for the row index the
// preview uses for it, and for the full argument.
constexpr int kCount = 2;
} // namespace DamageSplitSlot

// One split window's claim on DamageSection bits: "these blocks leave
// DamagePanel and are drawn by this window instead".
struct DamageSplitTarget {
    uint32_t mask;
};

// THE TABLE. Declarative, compile-time, and the single source of truth
// for "which bits left DamagePanel".
//
// main.cpp contains no bit arithmetic at all: it asks this table through
// damagePanelMaskFor() / damageSubWindowMask(). To add a window you add
// ONE row here (plus one panel instance in main.cpp and one
// DamageSplitSlot entry) and every mask, the log line and the "nothing
// is painted twice" proof follow from the table.
//
// Held to by the static_asserts below: every row is a non-empty subset
// of DamageSection::kAll, and no bit is claimed by two windows — two
// windows drawing one block is the failure a user notices immediately
// and a pixel baseline may not.
constexpr DamageSplitTarget kDamageSplitTargets[] = {
    // The share bar. First out for the same reason the player split's
    // status pill went first: it maps onto a HunterPie control of its
    // own (the damage meter's share strip) and carries the least state
    // of the two candidates — one last-sample read, no history walk.
    {DamageSection::Share},
    // The chart. The heaviest of the two, and the one the width change
    // does the most for: the y-axis label column is a fixed 36px, so
    // every extra pixel of window width is a pixel of plot area.
    {DamageSection::Chart},
};

// How many bits the table's rows claim in total, counted per row. Unlike
// damageSplitRemovedBits() this does NOT fold the rows through an OR, so
// two rows sharing a bit are counted twice here and once there — and
// the difference between the two counts is exactly the failure being
// asserted.
[[nodiscard]] constexpr int damageSplitBitCount(uint32_t bits) noexcept
{
    int n = 0;
    while (bits != 0) {
        bits &= static_cast<uint32_t>(bits - 1);   // clear the lowest set bit
        ++n;
    }
    return n;
}

// The mask DamagePanel itself keeps: the user's toggles minus the rows
// above.
[[nodiscard]] constexpr uint32_t damagePanelMaskFor(
    PlayerSection::PlayerLayoutMode mode, uint32_t mask) noexcept
{
    // SINGLE: the user's mask verbatim. Every pre-split user resolves
    // here and gets one window painted byte-for-byte what it always
    // painted.
    if (mode == PlayerSection::PlayerLayoutMode::Single)
        return mask;
    // SPLIT: the panel only ever loses blocks that moved out.
    uint32_t removed = 0;
    for (const DamageSplitTarget &t : kDamageSplitTargets)
        removed |= t.mask;
    return mask & ~removed;
}

// The mask ONE sub-window draws: its own claim, intersected with what
// the user left on.
//
// An out-of-range slot answers 0 (draw nothing) rather than reading
// past the table, so a registry row naming a window the table does not
// have is inert instead of UB.
[[nodiscard]] constexpr uint32_t damageSubWindowMask(
    int slot, uint32_t mask) noexcept
{
    if (slot < 0 || slot >= DamageSplitSlot::kCount)
        return 0;
    return kDamageSplitTargets[slot].mask & mask;
}

// The removed bits are the table's rows OR-ed together, and no row's
// bit is claimed twice.
[[nodiscard]] constexpr uint32_t damageSplitRemovedBits() noexcept
{
    uint32_t removed = 0;
    for (const DamageSplitTarget &t : kDamageSplitTargets)
        removed |= t.mask;
    return removed;
}

[[nodiscard]] constexpr int damageSplitClaimedBitCount() noexcept
{
    int n = 0;
    for (const DamageSplitTarget &t : kDamageSplitTargets)
        n += damageSplitBitCount(t.mask);
    return n;
}

// ---- Compile-time proofs ------------------------------------------------

[[nodiscard]] constexpr bool damageSplitTargetsAreValid() noexcept
{
    for (int i = 0; i < DamageSplitSlot::kCount; ++i) {
        const uint32_t m = kDamageSplitTargets[i].mask;
        if (m == 0)                                  return false; // no claim
        if ((m & ~DamageSection::kAll) != 0)    return false; // not a bit
        for (int j = i + 1; j < DamageSplitSlot::kCount; ++j)
            if ((m & kDamageSplitTargets[j].mask) != 0) return false; // twice
    }
    return true;
}

static_assert(damageSplitTargetsAreValid(),
              "kDamageSplitTargets rows must be non-empty, inside "
              "DamageSection::kAll and pairwise disjoint — two windows "
              "claiming one DamageSection bit would draw it twice");
static_assert(DamageSplitSlot::kCount == 2,
              "DamageSplitSlot::kCount must match the kDamageSplitTargets "
              "row count");
static_assert(damageSplitBitCount(damageSplitRemovedBits())
                  == damageSplitClaimedBitCount(),
              "the removed bits must be exactly the table's rows, with no "
              "bit claimed by two windows: an overlapping row's OR would "
              "lose a duplicate and these two counts would diverge");
// The whole pixel-identity argument, as two compile-time checks: in
// Single mode the derived mask IS the input mask.
static_assert(damagePanelMaskFor(PlayerSection::PlayerLayoutMode::Single, 0xDEADBEEFu)
                  == 0xDEADBEEFu,
              "Single mode must return the mask unchanged (non-zero case)");
static_assert(damagePanelMaskFor(PlayerSection::PlayerLayoutMode::Single, 0u) == 0u,
              "Single mode must return the mask unchanged (zero case)");
// And the split's invariant: the removed bits and what the panel keeps
// share no bit, so exactly one window draws a given block in either mode.
static_assert((damagePanelMaskFor(PlayerSection::PlayerLayoutMode::Split, 0xFFFFFFFFu)
               & damageSplitRemovedBits())
                  == 0,
              "in Split mode DamagePanel must hold none of the removed bits");
// The two rows claim exactly the two bits they name, and no third bit
// leaves the panel — so Rows and OtherMembers stay put with the panel.
static_assert(damageSplitRemovedBits()
                  == (DamageSection::Share
                      | DamageSection::Chart),
              "the damage split owns exactly Share|Chart; Rows stays the "
              "panel's substance and OtherMembers is a filter over Rows");

// ----- Pet damage panel sections ------------------------------------------
namespace PetDamageSection {
enum : uint32_t {
    LocalPets = 1u << 0,   // 本地玩家拥有的猫狗
    OtherPets = 1u << 1,   // 队友 / 盟友拥有的猫狗
};
constexpr uint32_t kAll = LocalPets | OtherPets;

inline const QStringList &names()
{
    static const QStringList n = {
        QStringLiteral("localPets"),
        QStringLiteral("otherPets"),
    };
    return n;
}

constexpr int kCount = 2;

inline QString displayName(int index)
{
    static const char *const kKeys[kCount] = {
        "console.section.pets.local_pets",
        "console.section.pets.other_pets",
    };
    if (index < 0 || index >= kCount)
        return {};
    const QString key = QString::fromLatin1(kKeys[index]);
    return StringTable::instance().tr(key);
}

inline QStringList displayNames()
{
    QStringList n;
    n.reserve(kCount);
    for (int i = 0; i < kCount; ++i)
        n << displayName(i);
    return n;
}
} // namespace PetDamageSection

static_assert(DamageSection::kCount == 4);
static_assert(DamageSection::kAll == 0x0Fu);
static_assert(PetDamageSection::kCount == 2);
static_assert(PetDamageSection::kAll == 0x03u);

// =====================================================================
// v0.12 (monster split): the monster panel's window topology.
// =====================================================================
// The same two questions PlayerSection's section above answers, asked of
// MonsterPanel for the same reason: ONE decision for which windows exist
// (`PlayerSection::PlayerLayoutMode`, REUSED — see the note at the enum
// below), ONE table for which MonsterSection bits each window owns, and
// TWO pure functions that turn (mode, user mask) into the two masks
// main.cpp hands out.
//
// The pixel-identity promise rests entirely on the Single arm of
// monsterPanelMaskFor() returning `mask` VERBATIM, exactly as
// playerPanelMaskFor() does: single mode hands MonsterPanel the value
// v0.12 hands it today, so the 10 single-mode baselines cannot move.
//
// Widget-free, like everything above: inline constexpr over uint32_t, so
// the whole table is checkable from a plain main() and adds no
// translation unit to any target (therefore no census entry).

namespace MonsterSplitSlot {
// One alias per row of kMonsterSplitTargets. The value MUST be the
// subscript of the row below, and kCount is cross-checked against the
// table's row count by static_assert at the bottom.
//
// v0.13: TWO slots, not five. The rows they name are the user's own
// grouping (「图标、激怒、血量在一块；异常状态和部位在一块」) — the
// per-block windows that preceded them were a layout nobody asked for
// and the first complaint was that five fragments was too many. The
// names are Identity / Conditions because that is the distinction the
// rows actually draw.
enum : int {
    Identity   = 0,   // 肖像+名字+大小+HP+激怒(+耐力) -> MonsterIdentityStripPanel
    Conditions = 1,   // 异常状态卡 + 部位网格          -> MonsterConditionsStripPanel
};
constexpr int kCount = 2;
} // namespace MonsterSplitSlot

// One split window's claim on MonsterSection bits: "these blocks leave
// MonsterPanel and are drawn by this window instead".
struct MonsterSplitTarget {
    uint32_t mask;
};

// =====================================================================
// THE TWO ROWS, AND WHY TWO.
//
// The user's report, verbatim: 「怪物的切太狠了，应该图标，激怒，血量
// 是在一块，异常状态和部位在一块。同时还可选择性的关闭部分，比如激怒条」.
// Three decisions, all recorded here.
//
// WHAT THE ROWS MEAN
// ------------------
//   Identity  = the monster as an ACTOR: who it is (六角肖像 + 金冠 +
//              名字 + 大小) and how it is doing right now (HP 条 /
//              怒气条 / 耐力条). This is the window a hunter looks at
//              to answer "can I fight this and how is it going".
//   Conditions = the monster as an OPPONENT: what is wrong with it
//               (异常状态卡) and what can be broken (部位网格), plus
//               the 勾爪软化 strip that lives INSIDE each .pc card.
//              This is the window a hunter looks at to answer "where
//              do I hit next".
//
// The two questions are asked at different times and read at different
// speeds: identity is a glance, conditions is a plan. Splitting one
// column into two windows that separate them is what "the split was too
// aggressive" was describing — five fragments forced the user to
// assemble the picture themselves, every hunt.
//
// WHAT THIS GIVES UP
// ------------------
// Two windows instead of five. Five windows meant five sub-windows to
// arrange, five panels.ini sections to place, and a clipboard of
// "which window is which" knowledge the user never asked for. Two is
// the number at which the windows are distinguishable by their CONTENT
// rather than by their geometry.
//
// WHAT IT DOES NOT GIVE UP
// ------------------------
// The per-block toggles. This is the third recorded decision, and the
// one that keeps the user's "还可选择性的关闭部分，比如激怒条" from
// being a regression: EVERY MonsterSection bit the two rows claim is
// still readable from the windows' own section mask. The row's mask
// is the UNION of the bits that block's SURFACE is allowed to draw;
// each block inside the window re-tests its own bit before drawing.
//
// The mechanism, and why the window reads the mask itself rather than
// being gated by an outer layer:
//
//   * The claim row is the union; the WINDOW receives
//     monsterSubWindowMask(slot, userMask) — the user's own toggles
//     intersected with the union. It then divides the received mask
//     back out per block at every consumer of that block:
//     the height reservation, the width reservation and the paint all
//     call identityEnrageEnabled() / conditionsPartsEnabled() etc.,
//     never the row's mask directly.
//
//   * That is the SAME discipline the split already has (the ail
//     window's ailEnabled() the parts window's partsEnabled(), the
//     hp window's hpEnabled()), just applied INSIDE a window instead
//     of as the window's whole identity. Each window's own title
//     case is still partly defined by what the user turned off:
//     identity with Enrage off is portrait+name+HP, still a window.
//     conditions with Ail off is parts alone, still a window.
//
//   * The outer layer CANNOT do it for us: a window's outer mask is
//     the union of FOUR bits (Info|Hp|Enrage|Stamina) and the window's
//     mount/unmount question is decided by that union. Gating the
//     window on "every bit off" and then gating the blocks on their
//     own bits is correct, but it means every DRAW site has to be
//     block-aware. Making the draw site block-aware is the honest
//     answer, not a complication to be avoided.
//
// So: four blocks can be turned off independently inside one surface,
// and the surface still exists while any of them is on. That is what
// the user asked for and it costs no new bits — Enrage is still
// MonsterSection::Enrage, the console's existing 激怒 row still
// changes it, and the console's slot-based mask writing
// (control_panel.cpp:196) is untouched because MonsterSection's bit
// order never moved.
//
// Row order is the cascade order (main.cpp's setSubWindowIndex), so
// Identity cascades from the monster panel's own corner and Conditions
// one step further out.
constexpr MonsterSplitTarget kMonsterSplitTargets[] = {
    // ── Identity — the monster as an ACTOR ──────────────────────────
    //
    // Info|Hp|Enrage|Stamina. The four blocks the panel used to stack
    // in one column (六角肖像, .bar.hp, .bar.st, .bar.er), and the four
    // that answer the two most frequent in-hunt questions: WHICH
    // monster is this, and HOW HURT it is.
    //
    // THE STACKING ORDER IS THE PANEL'S OWN. The window draws them in
    // exactly the sequence panel_monster.cpp does (Info, then HP, then
    // stamina, then enrage) so the fragment a hunter already knows how
    // to read does not get reordered on the way out.
    //
    // Info is the block that USED to be excluded, and the reason it
    // is now here is the user's grouping. Portrait + name + size +
    // crown + the enrage tag is what makes the window read as "this
    // monster"; a window holding only bars has no subject.
    {mhw::MonsterSection::Info | mhw::MonsterSection::Hp
     | mhw::MonsterSection::Enrage | mhw::MonsterSection::Stamina},

    // ── Conditions — the monster as an OPPONENT ────────────────────
    //
    // Ail|Parts. The 15 s change-based AutoHide card grid and the
    // 3-column part grid: the two grids' wrap is what a window is for,
    // and Tenderize travels here because since v0.7.4 PR C the 勾爪
    // 软化 countdown is a strip INSIDE each .pc card, height-reserved
    // by the same predicate that draws it. It cannot be separated from
    // Parts without drawing it twice.
    //
    // Both blocks are the panel's heaviest data and the least
    // glanceable, which is exactly why they belong together: a hunter
    // opening this window is planning an attack, not checking a
    // status.
    {mhw::MonsterSection::Ail | mhw::MonsterSection::Parts},
};

// NOT in this table, and why — the ONE remaining MonsterSection bit is
// named here so its omission is a recorded decision rather than an
// oversight:
//
//   Tenderize (1u << 5) — 勾爪软化倒计时胶囊. NOT a block of its own:
//                         since v0.7.4 PR C it is a strip INSIDE each
//                         .pc card, drawn and height-reserved by the
//                         same pcHasTenderize() gate the parts grid
//                         uses (panel_monster.cpp). Giving it a window
//                         would mean either re-parenting a strip that
//                         is defined by the card it sits in, or
//                         drawing it twice. It travels with Parts
//                         (and therefore with Conditions), which is
//                         where it lives.
//
// Rows are OR-disjoint by construction below, so exactly one window
// draws a given block in Split mode and the panel draws none of them.

// Row count of the table, as a constant the functions below index with.
inline constexpr int kMonsterSplitTargetCount = static_cast<int>(
    sizeof(kMonsterSplitTargets) / sizeof(kMonsterSplitTargets[0]));

// OR of every window's claim: exactly the bits that leave MonsterPanel
// in Split mode.
[[nodiscard]] constexpr uint32_t monsterSplitRemovedBits() noexcept
{
    uint32_t bits = 0;
    for (const MonsterSplitTarget &t : kMonsterSplitTargets)
        bits |= t.mask;
    return bits;
}

// The mask MonsterPanel must receive. THE expression the "single-panel
// mode is pixel-identical" promise rests on: in Single mode it returns
// `mask` verbatim, bit for bit, so monsterPanel.setSectionMask() is
// handed exactly the value v0.12 hands it and no downstream code can
// detect the difference.
[[nodiscard]] constexpr uint32_t monsterPanelMaskFor(
    PlayerSection::PlayerLayoutMode mode, uint32_t mask) noexcept
{
    return mode == PlayerSection::PlayerLayoutMode::Split
               ? static_cast<uint32_t>(mask & ~monsterSplitRemovedBits())
               : mask;
}

// The mask split window `slot` must receive: the USER's own toggles
// intersected with what the table says that window owns. An out-of-range
// slot answers 0 (draw nothing) rather than reading past the table.
[[nodiscard]] constexpr uint32_t monsterSubWindowMask(
    int slot, uint32_t mask) noexcept
{
    if (slot < 0 || slot >= kMonsterSplitTargetCount)
        return 0;
    return kMonsterSplitTargets[slot].mask & mask;
}

// Compile-time proof that the table says what it claims, pairwise over
// every row pair: no empty claim, no bit outside MonsterSection::kAll,
// and no bit claimed twice.
[[nodiscard]] constexpr bool monsterSplitTargetsAreValid() noexcept
{
    for (int i = 0; i < kMonsterSplitTargetCount; ++i) {
        const uint32_t m = kMonsterSplitTargets[i].mask;
        if (m == 0)                                   return false;
        if ((m & ~mhw::MonsterSection::kAll) != 0)     return false;
        for (int j = i + 1; j < kMonsterSplitTargetCount; ++j)
            if ((m & kMonsterSplitTargets[j].mask) != 0) return false;
    }
    return true;
}

static_assert(monsterSplitTargetsAreValid(),
              "kMonsterSplitTargets rows must be non-empty, inside "
              "MonsterSection::kAll and pairwise disjoint — two windows "
              "claiming one MonsterSection bit would draw it twice");
static_assert(MonsterSplitSlot::kCount == kMonsterSplitTargetCount,
              "MonsterSplitSlot::kCount must match the "
              "kMonsterSplitTargets row count");
static_assert(monsterPanelMaskFor(PlayerSection::PlayerLayoutMode::Single,
                                  0xDEADBEEFu) == 0xDEADBEEFu,
              "Single mode must return the mask unchanged (non-zero case)");
static_assert(monsterPanelMaskFor(PlayerSection::PlayerLayoutMode::Single,
                                  0u) == 0u,
              "Single mode must return the mask unchanged (zero case)");
static_assert((monsterPanelMaskFor(PlayerSection::PlayerLayoutMode::Split,
                                   0xFFFFFFFFu)
               & monsterSplitRemovedBits()) == 0,
              "in Split mode MonsterPanel must hold none of the removed "
              "bits");
// Bits set in `bits`, counted rather than OR-folded — the same
// disjointness proof playerSplitRemovedBits() pairs with.
[[nodiscard]] constexpr int monsterSplitBitCount(uint32_t bits) noexcept
{
    int n = 0;
    while (bits != 0) {
        bits &= static_cast<uint32_t>(bits - 1);
        ++n;
    }
    return n;
}

// How many bits the table's rows claim in total, counted PER ROW.
// Unlike monsterSplitRemovedBits() this does not fold the rows through
// an OR, so two rows sharing a bit are counted twice here and once
// there — and the difference between the two counts is exactly the
// failure being asserted.
//
// v0.13: this function did not exist, because every row used to claim
// exactly one bit and `bitCount(removed) == kMonsterSplitTargetCount`
// was the same statement. The two grouped rows (Identity claims FOUR
// bits, Conditions TWO) break that identity, so the assertion below is
// now the player table's: COUNTED CLAIMS against the OR-folded union.
// The guarantee it buys is the same one and it is stronger — it still
// fails the instant two rows overlap.
[[nodiscard]] constexpr int monsterSplitClaimedBitCount() noexcept
{
    int n = 0;
    for (const MonsterSplitTarget &t : kMonsterSplitTargets)
        n += monsterSplitBitCount(t.mask);
    return n;
}
static_assert(monsterSplitBitCount(monsterSplitRemovedBits())
                  == monsterSplitClaimedBitCount(),
              "the removed bits must be exactly the table's rows, with "
              "no bit claimed by two windows");
} // namespace mhw
