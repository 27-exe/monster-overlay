// SPDX-License-Identifier: Apache-2.0
//
// Copy-semantics tests for the two console locale files
// (src/resources/i18n/<zh-CN|en-US>/console.json).
//
// ControlPanel / HudCanvas resolve every widget string through
// StringTable::tr(key): a key present in only one locale silently ships the
// wrong language, a key present in neither renders as the raw dot-path
// (string_table.cpp returns the key itself), and a dropped %1/%2 breaks a
// .arg() chain at runtime. This test pins the invariants the copy pass
// promised:
//
//   1. zh-CN and en-US expose exactly the same console key set.
//   2. per key the placeholder sets (%1..%6 and {n}/{g}/{p}/{r}/{s}/{t})
//      match.
//   3. every console.* key referenced by a C++ string literal in src/ exists
//      in BOTH locales with a non-empty value (the "NativeUI visible" set,
//      enumerated below).
//   4. none of those visible values carries the ambiguous / prototype-era
//      wording the copy pass removed (分开密度, 元数据, 舞台, localStorage,
//      HTML 交互原型, browser, metadata, stage, prototype, ...).
//   5. the 22 keys reserved for the upcoming layout/preview/edit/inspector
//      controls exist in both locales with a non-empty value.
//   6. no value at all ships a double-escaped \uXXXX, a raw "console." key
//      path, or an empty string.
//
// Registering it in CMakeLists.txt (parent session):
//
//   add_executable(copy-semantics-tests tests/copy_semantics_tests.cpp)
//   target_link_libraries(copy-semantics-tests PRIVATE Qt6::Core)
//   target_compile_definitions(copy-semantics-tests PRIVATE
//       MONSTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
//   target_compile_options(copy-semantics-tests PRIVATE -Wall -Wextra -Wpedantic)
//   add_test(NAME copy-semantics-tests COMMAND copy-semantics-tests)
//
// No game process, no GUI, no widget is created.

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdio>

#ifndef MONSTER_SOURCE_DIR
#error "MONSTER_SOURCE_DIR must name the repository root"
#endif

namespace {

int failures = 0;

void check(bool cond, const QString& what)
{
    if (cond)
        std::printf("PASS: %s\n", qPrintable(what));
    else {
        std::fprintf(stderr, "FAIL: %s\n", qPrintable(what));
        ++failures;
    }
}

using Table = QMap<QString, QString>;

// Flatten the "console" object the same way StringTable::load() does, so a
// nested {"rail": {"hide_stage": ...}} and a dotted key inside "proto" both
// become the exact dot-path the C++ side asks for.
bool flatten(const QJsonObject& node, const QString& prefix, Table& out)
{
    for (auto it = node.begin(); it != node.end(); ++it) {
        const QString key = prefix.isEmpty()
            ? it.key() : prefix + QLatin1Char('.') + it.key();
        if (it.value().isObject()) {
            if (!flatten(it.value().toObject(), key, out))
                return false;
        } else if (it.value().isString()) {
            out.insert(key, it.value().toString());
        } else {
            std::fprintf(stderr, "FAIL: %s is not a string value\n",
                         qPrintable(key));
            return false;
        }
    }
    return true;
}

bool loadConsole(const QString& locale, Table& out)
{
    const QString path = QStringLiteral(
        MONSTER_SOURCE_DIR "/src/resources/i18n/%1/console.json").arg(locale);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "FAIL: cannot open %s\n", qPrintable(path));
        return false;
    }
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        std::fprintf(stderr, "FAIL: %s is not valid JSON (%s)\n",
                     qPrintable(path), qPrintable(err.errorString()));
        return false;
    }
    const QJsonValue console = doc.object().value(QStringLiteral("console"));
    if (!console.isObject()) {
        std::fprintf(stderr, "FAIL: %s has no \"console\" object\n",
                     qPrintable(path));
        return false;
    }
    return flatten(console.toObject(), QStringLiteral("console"), out);
}

// The tokens a QString::arg() chain or a {name} formatter consumes.
QStringList placeholders(const QString& s)
{
    QStringList out;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('%')) {
            int j = i + 1;
            while (j < s.size() && s.at(j).isDigit())
                ++j;
            if (j > i + 1) {
                out << s.mid(i, j - i);
                i = j - 1;
            }
        } else if (c == QLatin1Char('{')) {
            int j = i + 1;
            while (j < s.size()
                   && (s.at(j).isLetterOrNumber() || s.at(j) == QLatin1Char('_')))
                ++j;
            if (j > i + 1 && j < s.size() && s.at(j) == QLatin1Char('}')) {
                out << s.mid(i, j - i + 1);
                i = j;
            }
        }
    }
    out.sort();
    return out;
}

// A value must never show "\uXXXX" as literal text: the shipped en-US file
// used to carry \u201c / \u2026 double-escaped, which rendered as those six
// characters on screen instead of a quote / ellipsis.
bool hasLiteralUnicodeEscape(const QString& s)
{
    const QChar bs = QLatin1Char('\\');
    for (int i = 0; i + 5 < s.size(); ++i) {
        if (s.at(i) != bs || s.at(i + 1) != QLatin1Char('u'))
            continue;
        bool hex = true;
        for (int j = i + 2; j <= i + 5; ++j) {
            const ushort u = s.at(j).unicode();
            const bool digit = s.at(j).isDigit();
            const bool lower = u >= 'a' && u <= 'f';
            const bool upper = u >= 'A' && u <= 'F';
            if (!digit && !lower && !upper) {
                hex = false;
                break;
            }
        }
        if (hex)
            return true;
    }
    return false;
}

bool containsAny(const QString& value, const QStringList& terms, QString* hit)
{
    for (const QString& t : terms) {
        if (value.contains(t)) {
            if (hit)
                *hit = t;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// NativeUI-visible keys: every console.* string literal in src/**/*.cpp|*.h
// (277 keys). One of these missing from a locale file means the raw dot-path
// is rendered to the user, so its presence is a hard requirement.
const QStringList kVisibleKeys {
    QStringLiteral("console.brand"), QStringLiteral("console.brand_sub"),
    QStringLiteral("console.canvas.disabled"), QStringLiteral("console.canvas.footer"),
    QStringLiteral("console.canvas.footer_right"), QStringLiteral("console.canvas.header"),
    QStringLiteral("console.canvas.primary"), QStringLiteral("console.canvas.selected_tag"),
    QStringLiteral("console.canvas.zoom"), QStringLiteral("console.corner.bottom_left"),
    QStringLiteral("console.corner.bottom_right"), QStringLiteral("console.corner.top_left"),
    QStringLiteral("console.corner.top_right"), QStringLiteral("console.corner.unknown"),
    QStringLiteral("console.detect.badge"), QStringLiteral("console.detect.none"),
    QStringLiteral("console.detect.short"), QStringLiteral("console.detect.startup_none"),
    QStringLiteral("console.detect.startup_running"), QStringLiteral("console.detect.startup_switch"),
    QStringLiteral("console.drawer.empty"), QStringLiteral("console.drawer.empty_sub"),
    QStringLiteral("console.edit.enter"), QStringLiteral("console.edit.hint"),
    QStringLiteral("console.edit.start"), QStringLiteral("console.edit.title"),
    QStringLiteral("console.game.rise"), QStringLiteral("console.game.world"),
    QStringLiteral("console.inspector.appearance"), QStringLiteral("console.inspector.auto_saved"),
    QStringLiteral("console.inspector.bg_alpha"), QStringLiteral("console.inspector.content"),
    QStringLiteral("console.inspector.count"), QStringLiteral("console.inspector.opacity"),
    QStringLiteral("console.inspector.primary"), QStringLiteral("console.inspector.primary_tag"),
    QStringLiteral("console.inspector.reset"), QStringLiteral("console.inspector.scale"),
    QStringLiteral("console.inspector.screen"), QStringLiteral("console.inspector.selected_object"),
    QStringLiteral("console.inspector.sub.damage"), QStringLiteral("console.inspector.sub.monster"),
    QStringLiteral("console.inspector.sub.pets"), QStringLiteral("console.inspector.sub.player"),
    QStringLiteral("console.layout.apply"), QStringLiteral("console.layout.apply_tip"),
    QStringLiteral("console.layout.confirm_single"), QStringLiteral("console.layout.confirm_split"),
    QStringLiteral("console.layout.confirm_title"), QStringLiteral("console.layout.single"),
    QStringLiteral("console.layout.split"), QStringLiteral("console.locale.en"),
    QStringLiteral("console.locale.tooltip"), QStringLiteral("console.locale.zh"),
    QStringLiteral("console.nav.damage"), QStringLiteral("console.nav.damage_summary"),
    QStringLiteral("console.nav.monster"), QStringLiteral("console.nav.monster_summary"),
    QStringLiteral("console.nav.pets"), QStringLiteral("console.nav.pets_summary"),
    QStringLiteral("console.nav.player"), QStringLiteral("console.nav.player_summary"),
    QStringLiteral("console.overview.display_tip"), QStringLiteral("console.overview.nav"),
    QStringLiteral("console.panel.damage"), QStringLiteral("console.panel.disabled"),
    QStringLiteral("console.panel.monster"), QStringLiteral("console.panel.pets"),
    QStringLiteral("console.panel.pets_rise_only"), QStringLiteral("console.panel.player"),
    QStringLiteral("console.pos.label"), QStringLiteral("console.proto.app.title"),
    QStringLiteral("console.proto.board.empty.overview"), QStringLiteral("console.proto.board.empty.overview.sub"),
    QStringLiteral("console.proto.board.empty.panel"), QStringLiteral("console.proto.board.empty.panel.sub"),
    QStringLiteral("console.proto.board.hint.edit"), QStringLiteral("console.proto.board.hint.idle"),
    QStringLiteral("console.proto.board.hint.overview"), QStringLiteral("console.proto.close"),
    QStringLiteral("console.proto.content"), QStringLiteral("console.proto.content.state"),
    QStringLiteral("console.proto.content.warn"), QStringLiteral("console.proto.copied"),
    QStringLiteral("console.proto.copy"), QStringLiteral("console.proto.corner.unknown"),
    QStringLiteral("console.proto.display"), QStringLiteral("console.proto.download"),
    QStringLiteral("console.proto.edit"), QStringLiteral("console.proto.export"),
    QStringLiteral("console.proto.export.desc"), QStringLiteral("console.proto.export.done"),
    QStringLiteral("console.proto.export.fail"), QStringLiteral("console.proto.export.notini"),
    // 第6条: console.proto.export.short / .tip left with the topbar's
    // duplicate export button — the rail's 导出 is the one entry now.
    QStringLiteral("console.proto.export.title"), QStringLiteral("console.proto.follow"),
    QStringLiteral("console.proto.game"), QStringLiteral("console.proto.game.note.rise"),
    QStringLiteral("console.proto.game.note.world"), QStringLiteral("console.proto.groupwins"),
    QStringLiteral("console.proto.help"), QStringLiteral("console.proto.help.k.esc"),
    QStringLiteral("console.proto.help.k.move"), QStringLiteral("console.proto.help.k.move10"),
    QStringLiteral("console.proto.help.k.panel"), QStringLiteral("console.proto.help.p1"),
    QStringLiteral("console.proto.help.p2"), QStringLiteral("console.proto.help.p3"),
    QStringLiteral("console.proto.help.p4"), QStringLiteral("console.proto.help.th.act"),
    QStringLiteral("console.proto.help.th.key"), QStringLiteral("console.proto.help.title"),
    QStringLiteral("console.proto.insp.chip.none"), QStringLiteral("console.proto.insp.empty.body"),
    QStringLiteral("console.proto.insp.empty.strong"), QStringLiteral("console.proto.insp.empty.sub"),
    QStringLiteral("console.proto.insp.sub.idle"), QStringLiteral("console.proto.insp.sub.selected"),
    QStringLiteral("console.proto.insp.title"), QStringLiteral("console.proto.language"),
    QStringLiteral("console.proto.layout"), QStringLiteral("console.proto.meta.title"),
    QStringLiteral("console.proto.meta.x"), QStringLiteral("console.proto.meta.y"),
    QStringLiteral("console.proto.notreal"), QStringLiteral("console.proto.off.conn"),
    QStringLiteral("console.proto.opacity"), QStringLiteral("console.proto.panel.damage"),
    QStringLiteral("console.proto.panel.hidden"), QStringLiteral("console.proto.panel.monster"),
    QStringLiteral("console.proto.panel.pets"), QStringLiteral("console.proto.panel.player"),
    QStringLiteral("console.proto.panel.shown"), QStringLiteral("console.proto.panels"),
    QStringLiteral("console.proto.pos.note"), QStringLiteral("console.proto.position"),
    QStringLiteral("console.proto.preview"), QStringLiteral("console.proto.preview.empty"),
    // v1.1.5: console.proto.reset / console.proto.save left this list with
    // their buttons — 重置演示 and 保存 were removed from the rail foot
    // (a reload beside a reset-to-default, and a save beside closeEvent's
    // own write). reset.done stays: resetPrototypeScope() still toasts it.
    QStringLiteral("console.proto.reset.done"),
    QStringLiteral("console.proto.scale"),
    QStringLiteral("console.proto.screen.rot"), QStringLiteral("console.proto.set.about"),
    QStringLiteral("console.proto.set.appearance"), QStringLiteral("console.proto.set.data"),
    QStringLiteral("console.proto.set.data.demo"), QStringLiteral("console.proto.set.data.note"),
    QStringLiteral("console.proto.set.rise"), QStringLiteral("console.proto.set.rise.note"),
    QStringLiteral("console.proto.set.version"), QStringLiteral("console.proto.settings"),
    QStringLiteral("console.proto.single"), QStringLiteral("console.proto.split"),
    QStringLiteral("console.proto.st.live"), QStringLiteral("console.proto.st.nodata"),
    QStringLiteral("console.proto.st.offline"), QStringLiteral("console.proto.st.running"),
    QStringLiteral("console.proto.st.stopped"), QStringLiteral("console.proto.target"),
    QStringLiteral("console.proto.theme"), QStringLiteral("console.proto.theme.dark"),
    QStringLiteral("console.proto.theme.light"), QStringLiteral("console.proto.theme.note"),
    QStringLiteral("console.proto.top.sub.overview"), QStringLiteral("console.proto.top.sub.panel"),
    QStringLiteral("console.proto.top.title.overview"), QStringLiteral("console.proto.top.title.panel"),
    QStringLiteral("console.proto.upd.badver"), QStringLiteral("console.proto.upd.check"),
    QStringLiteral("console.proto.upd.found"), QStringLiteral("console.proto.upd.latest"),
    QStringLiteral("console.proto.upd.norepo"), QStringLiteral("console.proto.upd.note"),
    QStringLiteral("console.proto.upd.offline"), QStringLiteral("console.proto.upd.rate"),
    QStringLiteral("console.proto.visible"), QStringLiteral("console.proto.win.short.damage"),
    QStringLiteral("console.proto.win.short.monster"), QStringLiteral("console.proto.win.short.pet"),
    QStringLiteral("console.proto.win.short.player"), QStringLiteral("console.proto.wins.count"),
    QStringLiteral("console.rail.detected"), QStringLiteral("console.rail.game"),
    QStringLiteral("console.rail.hide_stage"), QStringLiteral("console.rail.none"),
    QStringLiteral("console.rail.show_stage"), QStringLiteral("console.rail.start_overlay"),
    QStringLiteral("console.rail.stop_overlay"), QStringLiteral("console.reframework.confirm_fix_menu_state"),
    QStringLiteral("console.reframework.confirm_install"), QStringLiteral("console.reframework.confirm_remove_lua"),
    QStringLiteral("console.reframework.confirm_remove_reframework"), QStringLiteral("console.reframework.confirm_restore_menu_state"),
    QStringLiteral("console.reframework.confirm_title"), QStringLiteral("console.reframework.detail_raw"),
    QStringLiteral("console.reframework.failure"), QStringLiteral("console.reframework.fix_menu_state"),
    QStringLiteral("console.reframework.fix_menu_state_failed"), QStringLiteral("console.reframework.fix_menu_state_tip"),
    QStringLiteral("console.reframework.game_found"), QStringLiteral("console.reframework.game_invalid"),
    QStringLiteral("console.reframework.game_running"), QStringLiteral("console.reframework.install"),
    QStringLiteral("console.reframework.menu_state_default"), QStringLiteral("console.reframework.menu_state_header"),
    QStringLiteral("console.reframework.menu_state_no_core"), QStringLiteral("console.reframework.menu_state_overridden"),
    QStringLiteral("console.reframework.menu_state_paths"), QStringLiteral("console.reframework.menu_state_unknown"),
    QStringLiteral("console.reframework.no_worker"), QStringLiteral("console.reframework.not_found"),
    QStringLiteral("console.reframework.pending"), QStringLiteral("console.reframework.remove_lua"),
    QStringLiteral("console.reframework.remove_reframework"), QStringLiteral("console.reframework.restore_menu_state"),
    QStringLiteral("console.reframework.restore_menu_state_tip"), QStringLiteral("console.reframework.result_already_installed"),
    QStringLiteral("console.reframework.result_busy"), QStringLiteral("console.reframework.result_external_core_left"),
    QStringLiteral("console.reframework.result_game_dir_unusable"), QStringLiteral("console.reframework.result_installed_bundled"),
    QStringLiteral("console.reframework.result_installed_downloaded"), QStringLiteral("console.reframework.result_lua_already_absent"),
    QStringLiteral("console.reframework.result_lua_modified"), QStringLiteral("console.reframework.result_lua_modified_removed"),
    QStringLiteral("console.reframework.result_lua_not_present"), QStringLiteral("console.reframework.result_lua_removed"),
    QStringLiteral("console.reframework.result_lua_removed_modified"), QStringLiteral("console.reframework.result_manifest_invalid"),
    QStringLiteral("console.reframework.result_no_managed_files"), QStringLiteral("console.reframework.result_unexpected_error"),
    QStringLiteral("console.reframework.result_unexpected_unknown"), QStringLiteral("console.reframework.state_core_missing"),
    QStringLiteral("console.reframework.state_lua_missing"), QStringLiteral("console.reframework.state_needs_repair"),
    QStringLiteral("console.reframework.state_ready"), QStringLiteral("console.reframework.subtitle"),
    QStringLiteral("console.reframework.success"), QStringLiteral("console.reframework.title"),
    QStringLiteral("console.section.damage.chart"), QStringLiteral("console.section.damage.other_members"),
    QStringLiteral("console.section.damage.rows"), QStringLiteral("console.section.damage.share"),
    QStringLiteral("console.section.monster.ail"), QStringLiteral("console.section.monster.enrage"),
    QStringLiteral("console.section.monster.hp"), QStringLiteral("console.section.monster.info"),
    QStringLiteral("console.section.monster.parts"), QStringLiteral("console.section.monster.stamina"),
    QStringLiteral("console.section.monster.tenderize"), QStringLiteral("console.section.pets.local_pets"),
    QStringLiteral("console.section.pets.other_pets"), QStringLiteral("console.section.player.bars"),
    QStringLiteral("console.section.player.buff"), QStringLiteral("console.section.player.conn"),
    QStringLiteral("console.section.player.debuff"), QStringLiteral("console.section.player.mantles"),
    QStringLiteral("console.section.player.quest"), QStringLiteral("console.section.player.sharpness"),
    QStringLiteral("console.section.player.weapon"), QStringLiteral("console.section.player.wirebug"),
    QStringLiteral("console.stage.grid"), QStringLiteral("console.stage.safe_area"),
    QStringLiteral("console.stage.sub"), QStringLiteral("console.stage.title"),
    QStringLiteral("console.stage.windows"), QStringLiteral("console.status.plain_ready"),
    QStringLiteral("console.status.ready"), QStringLiteral("console.status.running_pid"),
    QStringLiteral("console.status.running_since"), QStringLiteral("console.status.switching"),
    QStringLiteral("console.theme.dark"), QStringLiteral("console.theme.light"),
    QStringLiteral("console.window_title"),
};

// Keys reserved for the controls a later change wires up (copy pass item 5).
const QStringList kReservedKeys {
    QStringLiteral("console.layout.mixed"), QStringLiteral("console.layout.group_title"),
    QStringLiteral("console.layout.detached"), QStringLiteral("console.layout.merged"),
    QStringLiteral("console.layout.merge_all"), QStringLiteral("console.layout.detach_all"),
    QStringLiteral("console.layout.custom_hint"), QStringLiteral("console.preview.fit"),
    QStringLiteral("console.preview.actual_size"), QStringLiteral("console.preview.zoom"),
    QStringLiteral("console.preview.size"), QStringLiteral("console.preview.height"),
    QStringLiteral("console.preview.hint"), QStringLiteral("console.preview.resize_tip"),
    QStringLiteral("console.edit.overlay_start"), QStringLiteral("console.edit.overlay_tip"),
    QStringLiteral("console.inspector.columns"), QStringLiteral("console.inspector.min_width"),
    QStringLiteral("console.inspector.min_height"), QStringLiteral("console.inspector.fixed_width"),
    QStringLiteral("console.inspector.fixed_height"), QStringLiteral("console.inspector.auto_size"),
};

const QStringList kAmbiguousZh {
    QStringLiteral("分开密度"), QStringLiteral("元数据"),
    QStringLiteral("舞台"), QStringLiteral("localStorage"),
    QStringLiteral("浏览器"), QStringLiteral("HTML"),
    QStringLiteral("原型"), QStringLiteral("demo")
};

const QStringList kAmbiguousEn {
    QStringLiteral("Metadata"), QStringLiteral("metadata"),
    QStringLiteral("localStorage"), QStringLiteral("Browser"),
    QStringLiteral("browser"), QStringLiteral("HTML"),
    QStringLiteral("prototype"), QStringLiteral("Prototype"),
    QStringLiteral("stage"), QStringLiteral("Stage"),
    QStringLiteral("STAGE")
};

} // namespace

int main()
{
    Table zh, en;
    if (!loadConsole(QStringLiteral("zh-CN"), zh)
        || !loadConsole(QStringLiteral("en-US"), en)) {
        std::fprintf(stderr, "FATAL: locale files unreadable\n");
        return 2;
    }

    // -- 1. key sets --------------------------------------------------------
    check(zh.size() >= 300,
          QStringLiteral("zh-CN console section parses to a non-trivial key set (%1 keys)")
              .arg(zh.size()));
    check(zh.keys() == en.keys(),
          QStringLiteral("zh-CN and en-US console key sets are equal (%1 vs %2 keys)")
              .arg(zh.size()).arg(en.size()));

    QStringList onlyZh, onlyEn;
    for (const QString& k : zh.keys())
        if (!en.contains(k))
            onlyZh << k;
    for (const QString& k : en.keys())
        if (!zh.contains(k))
            onlyEn << k;
    if (!onlyZh.isEmpty())
        std::fprintf(stderr, "  only in zh-CN: %s\n",
                     qPrintable(onlyZh.mid(0, 8).join(QLatin1String(", "))));
    if (!onlyEn.isEmpty())
        std::fprintf(stderr, "  only in en-US: %s\n",
                     qPrintable(onlyEn.mid(0, 8).join(QLatin1String(", "))));

    // -- 2. per-key placeholders -------------------------------------------
    int placeholderKeys = 0;
    int placeholderMismatch = 0;
    for (auto it = zh.constBegin(); it != zh.constEnd(); ++it) {
        const QStringList pz = placeholders(it.value());
        const QStringList pe = placeholders(en.value(it.key()));
        if (!pz.isEmpty())
            ++placeholderKeys;
        if (pz != pe) {
            ++placeholderMismatch;
            std::fprintf(stderr,
                         "  placeholder mismatch on %s: zh=[%s] en=[%s]\n",
                         qPrintable(it.key()),
                         qPrintable(pz.join(QLatin1String(","))),
                         qPrintable(pe.join(QLatin1String(","))));
        }
    }
    check(placeholderKeys >= 40,
          QStringLiteral("%1 keys carry a placeholder").arg(placeholderKeys));
    check(placeholderMismatch == 0,
          QStringLiteral("placeholder sets match per key (%1 mismatches)")
              .arg(placeholderMismatch));

    // -- 3. every C++-referenced key resolves in both locales ---------------
    int visibleBroken = 0;
    for (const QString& key : kVisibleKeys) {
        const QString vz = zh.value(key);
        const QString ve = en.value(key);
        if (vz.trimmed().isEmpty() || ve.trimmed().isEmpty()) {
            ++visibleBroken;
            std::fprintf(stderr, "  empty/missing value: %s\n", qPrintable(key));
        } else if (placeholders(vz) != placeholders(ve)) {
            ++visibleBroken;
            std::fprintf(stderr, "  placeholder mismatch: %s\n", qPrintable(key));
        }
    }
    check(visibleBroken == 0,
          QStringLiteral("all %1 C++-referenced keys resolve in both locales")
              .arg(kVisibleKeys.size()));

    // -- 4. ambiguous wording is gone from visible values -------------------
    int ambiguous = 0;
    for (const QString& key : kVisibleKeys) {
        QString hit;
        if (containsAny(zh.value(key), kAmbiguousZh, &hit)) {
            ++ambiguous;
            std::fprintf(stderr, "  zh-CN %s still says \"%s\"\n",
                         qPrintable(key), qPrintable(hit));
        }
        if (containsAny(en.value(key), kAmbiguousEn, &hit)) {
            ++ambiguous;
            std::fprintf(stderr, "  en-US %s still says \"%s\"\n",
                         qPrintable(key), qPrintable(hit));
        }
    }
    check(ambiguous == 0,
          QStringLiteral("no ambiguous/prototype-era wording in %1 visible keys")
              .arg(kVisibleKeys.size()));

    // The data-source note must keep the honest claim: never reads memory.
    check(zh.value(QStringLiteral("console.proto.set.data.note"))
              .contains(QStringLiteral("不读取游戏内存")),
          QStringLiteral("zh-CN set.data.note states the console never reads game memory"));
    check(en.value(QStringLiteral("console.proto.set.data.note"))
              .contains(QStringLiteral("never reads game memory")),
          QStringLiteral("en-US set.data.note states the console never reads game memory"));

    // -- 5. reserved keys for the upcoming controls -------------------------
    int reservedMissing = 0;
    int reservedEmpty = 0;
    for (const QString& key : kReservedKeys) {
        if (!zh.contains(key) || !en.contains(key)) {
            ++reservedMissing;
            std::fprintf(stderr, "  reserved key missing: %s\n", qPrintable(key));
        } else if (zh.value(key).trimmed().isEmpty()
                   || en.value(key).trimmed().isEmpty()) {
            ++reservedEmpty;
            std::fprintf(stderr, "  reserved key with empty value: %s\n",
                         qPrintable(key));
        }
    }
    check(reservedMissing == 0,
          QStringLiteral("all %1 reserved control keys exist in both locales")
              .arg(kReservedKeys.size()));
    check(reservedEmpty == 0,
          QStringLiteral("no reserved key has an empty value (%1 empty)")
              .arg(reservedEmpty));

    // -- 6. file-wide hygiene ------------------------------------------------
    int escapes = 0;
    int leaked = 0;
    int empties = 0;
    const Table* tables[2] = { &zh, &en };
    for (const Table* t : tables) {
        for (auto it = t->constBegin(); it != t->constEnd(); ++it) {
            if (hasLiteralUnicodeEscape(it.value())) {
                ++escapes;
                std::fprintf(stderr, "  literal \\uXXXX in %s\n",
                             qPrintable(it.key()));
            }
            if (it.value().contains(QStringLiteral("console."))) {
                ++leaked;
                std::fprintf(stderr, "  raw dot-path in %s\n",
                             qPrintable(it.key()));
            }
            if (it.value().trimmed().isEmpty()) {
                ++empties;
                std::fprintf(stderr, "  empty value: %s\n", qPrintable(it.key()));
            }
        }
    }
    check(escapes == 0, QStringLiteral("no double-escaped \\uXXXX left in any value"));
    check(leaked == 0, QStringLiteral("no value contains a raw console.* key path"));
    check(empties == 0, QStringLiteral("no empty values in either locale"));

    std::printf("visible=%d reserved=%d zh-keys=%d en-keys=%d failures=%d\n",
                int(kVisibleKeys.size()), int(kReservedKeys.size()),
                int(zh.size()), int(en.size()), failures);
    if (failures) {
        std::puts("RESULT: FAIL");
        return 1;
    }
    std::puts("RESULT: OK");
    return 0;
}
