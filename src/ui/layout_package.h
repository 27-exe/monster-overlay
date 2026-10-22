// SPDX-License-Identifier: Apache-2.0
// LayoutPackage — the file format the console's layout export / import and
// the shipped default share.
//
// WHY A FORMAT AND NOT "JUST WRITE panels.ini". The export has to say what
// it contains down to the PANEL GROUP (the user's words: 「精细到崛起的怪物
// 面板的 mix 状态」), and an import has to answer "which of my scopes does
// this file touch" WITHOUT reading the body first. A raw panels.ini dump can
// express neither: it has no header, so a partial package is
// indistinguishable from a complete one, and importing it would overwrite
// scopes the file never mentioned.
//
// THE HEADER IS THE CONTRACT. `contains.scopes` maps a scope
// ("<game>/<mode>") to the panel groups whose windows are inside it. On
// import that map decides what gets written; on export it is derived from
// the body so the two can never drift. Parse VERIFIES the two agree and
// refuses the file when they do not — a file that lies about its own
// contents is the one thing an import must never trust.
//
// THE BODY. scopes[scope][windowKey] = the fields for that window. Every
// field is OPTIONAL: absent means "do not touch this one on import", which
// is what makes a package carrying only monster's rows leave every other
// window exactly where the user left it.
//
// UNKNOWN KEYS ARE KEPT, at all four levels (root / contains / global /
// window). The conf writer in this repo has that contract already
// (unknown rows survive a save verbatim) and a layout package must not
// quietly eat a field written by a newer build.
//
// Pure QtCore: no Widgets, so the codec is usable from tests, the CLI and
// the console alike.

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <optional>

namespace mhw {

// ─── one window's worth of tunable fields ─────────────────────────────────
//
// Semantics match the panels.ini rows exactly: ml/mt/mr/mb are the margins
// the inspector's x/y spin boxes write, columns is the dial only some
// windows have, and `visible` is the winVisible switch (absent == shown).
struct LayoutWindowRow {
    std::optional<int> ml, mt, mr, mb;
    std::optional<int> minW, minH, lockW, lockH;
    std::optional<int> bgAlpha, columns;
    std::optional<double> scale, opacity;
    std::optional<QString> outputName;
    std::optional<bool> visible;

    // Keys this type owns. Everything else in the JSON object is kept in
    // `extra` and re-emitted verbatim on serialize.
    QJsonObject extra;

    [[nodiscard]] bool isEmpty() const;
    bool operator==(const LayoutWindowRow &other) const;
};

// ─── the optional global block ────────────────────────────────────────────
//
// The four masks live in monster-overlay.conf as hex32 rows
// (player=1ff …), locale and `layout=` beside them, and each game's
// detached-window list is one `layout.windows.<game>=` row. All of it is
// per-file-optional for the same reason the window fields are.
struct LayoutGlobalBlock {
    std::optional<uint32_t> maskPlayer, maskMonster, maskDamage, maskPets;
    std::optional<QString> locale;
    std::optional<QString> layout;          // "single" | "split" | "mixed"
    QMap<QString, QStringList> plans;       // game -> detached window keys

    QJsonObject extra;

    [[nodiscard]] bool isEmpty() const;
    bool operator==(const LayoutGlobalBlock &other) const;
};

// ─── the package ─────────────────────────────────────────────────────────
struct LayoutPackage {
    static constexpr int kSchema = 1;
    static inline const QString kKind =
        QStringLiteral("monster-overlay-layout");

    bool hasGlobal = false;
    LayoutGlobalBlock global;
    // scope ("<world|rise>/<single|split|mixed>") -> window key -> fields
    QMap<QString, QMap<QString, LayoutWindowRow>> scopes;
    // header: scope -> the panel groups present in it, in registry order
    QMap<QString, QStringList> containsScopes;

    QJsonObject rootExtra;      // unknown top-level keys, verbatim
    QJsonObject containsExtra;  // unknown keys inside `contains`, verbatim

    // Parse a file. On failure returns nullopt and, when `error` is given,
    // a message naming the offending path/key — an import that refuses must
    // say WHY, not "invalid file".
    [[nodiscard]] static std::optional<LayoutPackage> parse(
        const QByteArray &json, QString *error = nullptr);

    // Serialize. Two consecutive serializations of the same package are
    // byte-identical (QJsonObject orders its keys), and parse() of the
    // result equals the input.
    [[nodiscard]] QByteArray serialize() const;

    // ── the queries the UI needs ──────────────────────────────────────
    [[nodiscard]] bool containsScope(const QString &scope) const;
    [[nodiscard]] QStringList scopeNames() const;
    [[nodiscard]] bool containsPanel(const QString &scope,
                                     const QString &panel) const;
    [[nodiscard]] QStringList panelsOf(const QString &scope) const;
    // Windows of one panel group inside one scope, registry order.
    [[nodiscard]] QStringList windowsOf(const QString &scope,
                                        const QString &panel) const;
};

// ── the disk half ─────────────────────────────────────────────────────────
//
// panels.ini is the store these two functions translate to and from. QSettings
// presents the file's `mixed\player-bars\ml` rows as `/`-separated paths
// (the backslash is its INI escaping of a `/` inside a key), so one walk of
// allKeys() yields "<game>/<mode>/<window>/<field>" — which is exactly the
// package's scope + window keys. Deriving the path from the file rather than
// from a registry is what lets a package be read even if a window is added
// upstream and this tree has not caught up yet.
//
// Read: every scope the file carries, with `contains` derived from what was
// actually found. Write: ONLY what the package declares — a scope the package
// does not mention is never opened, and a field it omits is never written, so
// importing a single panel group cannot disturb anything else on disk.
[[nodiscard]] std::optional<LayoutPackage> layoutPackageFromSettings(
    QSettings &settings, QString *error = nullptr);

[[nodiscard]] bool layoutPackageToSettings(const LayoutPackage &pkg,
                                           QSettings &settings,
                                           QString *error = nullptr);

// ── first run ─────────────────────────────────────────────────────────────
//
// WITHOUT THIS, a brand-new user gets the CODE default: Single mode, and
// all four mother panels at the corner margins (20,20 / 0,20,20,0 / …),
// which on a real screen means the windows the user expected to be spread
// across it are stacked on each other in the top-left. That is a fine
// default for a test fixture and a terrible first impression for a person.
//
// GUARDED ON THE STORE NOT EXISTING. An existing panels.ini is the user's
// own work — from a previous run, a backup, or a deliberate edit — and is
// never touched. That is also what makes this safe to call from a start-up
// path rather than from the low-level reader: an ambient seed would fire
// inside the test suite too, quietly giving every test a layout it did not
// ask for.
//
// ONLY panels.ini. The conf's mask rows are not written here: the overlay
// must not become a writer of monster-overlay.conf (the single-writer rule
// in core/locale_conf.h), and a missing conf already means "every section
// on", which is a coherent default on its own.
[[nodiscard]] bool seedShippedDefaultIfFresh(QString *error = nullptr);

// The path panels.ini has on this machine, named once so the freshness
// check and the writer cannot disagree about what "already exists" means.
[[nodiscard]] QString layoutPackageStorePath();

// ── the registry, spelled once ───────────────────────────────────────────
// The four mother panels and the eleven windows the split tables carve out
// of three of them. Derived from window_layout_plan.h's kWindowLayoutRows so
// the codec and the console cannot drift; the mothers are not in that table
// (it only names rows that leave a parent), hence the second list.

// The panels a game actually has: World 3 (no pets), Rise 4.
[[nodiscard]] QStringList layoutPackagePanelsFor(bool rise);

// The windows one panel group owns in one mode. `single` has no sub-windows.
[[nodiscard]] QStringList layoutPackageWindowsFor(const QString &panel,
                                                  const QString &mode);

// Scope grammar: <world|rise>/<single|split|mixed>. Fills the two halves
// when both pointers are non-null and the name is valid.
[[nodiscard]] bool layoutPackageScopeValid(const QString &scope,
                                           bool *rise = nullptr,
                                           int *mode = nullptr);

// Panel group names, registry order, for a game.
[[nodiscard]] QStringList layoutPackagePanelOrder(bool rise);

}  // namespace mhw
