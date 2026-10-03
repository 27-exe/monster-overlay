// SPDX-License-Identifier: Apache-2.0
//
// PanelMaskFallback — the overlay's startup mask priority chain, lifted
// out of the lambda in src/main.cpp so it can be exercised by a plain
// logic test.
//
// The bug it fixes (v0.11.4)
// ---------------------------
// `parseMask` in main.cpp answered "no --mask-* flag -> 0xFFFFFFFF". The
// four mask rows the control console writes into
// ~/.config/monster-overlay/monster-overlay.conf therefore reached a live
// overlay ONLY through the flags the console appends when it spawns the
// child process. Launching ./monster-overlay by hand — which is what
// README.md and docs/USAGE.md tell the user to do — silently discarded
// every saved toggle, and the console UI showed the opposite state from
// what the overlay drew.
//
// The contract
// ------------
//     explicit, parseable CLI value  >  conf row  >  0xFFFFFFFF
//
// which is the same shape as mhw::resolveStartupLocale in
// core/locale_conf.h (`--locale` > conf `locale=` row > system locale).
// The three rules, and the reason each is a rule and not a detail:
//
//   1. An explicit CLI value wins. This is the pre-existing behaviour and
//      the whole reason the console path still works; the conf fallback
//      must never merge into it, because the tempting "conf ORs in"
//      implementation silently overrides what the console asked for.
//   2. With no CLI value, a conf row that is present and well-formed is
//      used. This is the fix.
//   3. A conf row that is missing OR malformed degrades to 0xFFFFFFFF —
//      never to 0 and never to a partially parsed value — so a truncated
//      or hand-mangled conf cannot blank a panel, which is what the
//      pre-v0.12 code did and is the safe answer for it.
//
// Layering
// --------
// This header takes plain `bool` + `uint32_t` values, never
// mhw::PanelMaskSet, so it does not include ui/. The dependency direction
// in this project is one-way (UI -> core, see docs/ARCHITECTURE.md §2.4),
// so a core header must never include a ui/ header. main.cpp is the UI
// layer: it does the single conf read and the single parse with
// mhw::parsePanelMasks() and hands the four resulting values in here.
//
// Header-only, like core/locale_conf.h and panel_mask_codec.h beside it,
// so the product target and the mask-fallback-tests target can both include
// it without a translation unit being added to either (and therefore
// without a zero-reference-gate census entry).
//
// Placement: ui/viewmodel/, next to the panel_mask_codec.h whose
// PanelMaskSet it consumes one layer up. It deliberately does NOT live in
// src/core/ even though it is game-agnostic: core is the kernel that knows
// nothing about panels, and a mask-per-panel decision is a panel concern
// that happens to be free of widget types. The layering rule that actually
// binds is the one its own comment states: it must never include ui/, and
// the include direction here is core -> none, ui -> core, so a core
// header reaching sideways into the mask domain would be the wrong edge
// to add. Nothing in the pure decision reads a file either, so it stays
// testable without ~/.config.

#pragma once

#include <cstdint>

#include <QFile>
#include <QString>
#include <QStringConverter>
#include <QTextStream>

namespace mhw {

// The four independent masks. Kept as an enum rather than four separate
// functions so a caller cannot wire three of them by accident; the slot is
// a parameter of the decision, not of the panel.
enum class PanelMaskSlot {
    Player,
    Monster,
    Damage,
    Pets,
};

// Why the effective mask is what it is. The source is part of the
// contract: it is what a startup log line reports, and it is how a
// wrong-but-right-valued fallback (the conf silently overriding an
// explicit CLI flag) becomes visible in a test.
enum class PanelMaskSource {
    CliExplicit,
    Conf,
    DefaultAll,
};

struct PanelMaskResolution {
    uint32_t value{0xFFFFFFFFu};
    PanelMaskSource source{PanelMaskSource::DefaultAll};
};

// One parsed CLI request for a single slot.
//
// `given` false means the flag was absent — the only state in which the
// conf row is consulted. `valid` false means the flag was handed over but
// did not parse, which is reported by the caller and must NOT fall through
// to the conf row (the pre-v0.12 behaviour was to warn and use the
// default, and a rescue from the conf would let a stale row override a
// flag the console deliberately passed).
struct PanelMaskCliRequest {
    bool given{false};
    bool valid{false};
    uint32_t value{0};
};

// One conf row for a single slot, exactly as mhw::parsePanelMasks reports
// it: `valid` is false both when the row is absent and when it is
// malformed, and the two cases must resolve identically.
struct PanelMaskConfRow {
    bool valid{false};
    uint32_t value{0};
};

// Read the conf text the way the console writes and the console reads it:
// text mode, UTF-8. Returns an empty string when the file is absent or
// unreadable, which parsePanelMasks() turns into "no rows at all" — the
// cold-start case, indistinguishable from an empty conf by design.
//
// This lives here (rather than in main.cpp) so a test can read a fixture
// path and never come near ~/.config.
inline QString readPanelMaskConfText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

// The decision. Pure: no filesystem, no QCommandLineParser, no widget, so
// the whole chain is testable from a plain main().
[[nodiscard]] inline PanelMaskResolution resolveStartupPanelMask(
    const PanelMaskCliRequest &cli, const PanelMaskConfRow &conf,
    PanelMaskSlot slot, uint32_t defaultEverything = 0xFFFFFFFFu)
{
    (void)slot;

    // Rule 1 — an explicit request wins, malformed or not. A malformed
    // CLI value resolves to the default rather than to the conf row (see
    // PanelMaskCliRequest).
    if (cli.given) {
        return cli.valid ? PanelMaskResolution{cli.value,
                                              PanelMaskSource::CliExplicit}
                         : PanelMaskResolution{defaultEverything,
                                               PanelMaskSource::DefaultAll};
    }

    // Rule 2 — the conf row. `valid` is false for both an absent and a
    // malformed row, so both fall through to rule 3 instead of one
    // becoming 0 and the other becoming a partial parse.
    if (conf.valid)
        return PanelMaskResolution{conf.value, PanelMaskSource::Conf};

    // Rule 3 — nothing usable was said: everything visible, which is what
    // the pre-v0.12 code returned and the safe answer for a first run.
    return PanelMaskResolution{defaultEverything, PanelMaskSource::DefaultAll};
}

} // namespace mhw
