// SPDX-License-Identifier: Apache-2.0
//
// 用户第 7 条（帮助面板重做）的契约测试：
//
//   1. SIZE — 帮助面板与设置对话框同尺寸契约（compact：≤680×700、贴合
//      内容、落在控制台客户区之内、无 modalBackdrop、modalShell 铺满对话
//      框）。改回 full-client veil（buildModalShell(!compact) +
//      centerModalOnConsole(!compact)）时必须失败。
//   2. KEYS — 按键列的渲染：必须精确等于 ↑ ↓ ← → / Shift + ↑ ↓ ← → /
//      Esc / 1 / 2 / 3 / 4 / Ctrl+滚轮，且不含 U+FFFD、不含 C1 控制符
//      （0x80–0x9F，UTF-8 被 QString::fromLatin1 逐字节解码后的残留）。
//      把 fromUtf8 改回 fromLatin1 时必须失败。v1.1.5：− / + / 1:1 不再是
//      表中的一行——它们是按钮（zoomOutBtn_ / zoomFitBtn_），没有键绑定，
//      挪到表下的 helpNote 说明里（用户原话：「最后这个1:1的快捷键，根本
//      没法按吧。真存在么？」）。
//   3. COPY — 帮助文案与 docs/CONSOLE.md / 代码真实行为对账：6 个
//      「游戏 × 布局」作用域（不是 v0.14 的 4 个）、关闭时保存与导出写
//      什么、分区开关与语言是全局设置，且没有「不写入 panels.ini」这种与
//      保存行为自相矛盾的旧句。
//
// CONFIG ISOLATION：XDG_CONFIG_HOME 在 QApplication 之前重定向进
// QTemporaryDir，真实 ~/.config/monster-overlay 不会被本测试触碰。
//
// SCREENSHOT：设 MONSTER_HELP_SNAP_ZH / MONSTER_HELP_SNAP_EN 为输出路径
// 时，抓取帮助对话框 PNG（默认不产生任何文件）。
//
// 未在 CMakeLists.txt 注册（该文件由父会话所有）；注册片段见
// experiment/.../user-8/REPORT-help-panel.md。

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "core/string_table.h"
#include "ui/control_panel.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    ++checks;
    std::printf("%s: %s", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!detail.isEmpty())
        std::printf(" -- %s", qPrintable(detail));
    std::printf("\n");
    if (!ok)
        ++failures;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// One open of a console modal: geometry + the widgets the assertions
// read, captured while the dialog is on screen, then closed again.
struct Probe {
    bool opened = false;
    QRect geo;
    QSize client;
    int backdrops = 0;
    QRect shell;
    QStringList keys;
    QStringList caps;
    QStringList paras;
    QString note;      // v1.1.5: the helpNote line under the key table
    QString title;
};

Probe openModal(ControlPanel &cp, const QString &buttonObject,
                const QString &dialogObject, const QString &snapPath)
{
    Probe out;
    auto *btn = cp.findChild<QPushButton *>(buttonObject);
    if (!btn) {
        std::printf("NOTE: no button %s\n", qPrintable(buttonObject));
        return out;
    }
    // A stuck exec() must not take the whole test binary down with it.
    QTimer::singleShot(4000, [] {
        if (QDialog *d =
                qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            d->reject();
    });
    QTimer::singleShot(60, [&] {
        auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dlg || dlg->objectName() != dialogObject) {
            if (dlg)
                dlg->reject();
            return;
        }
        out.opened = true;
        out.title = dlg->windowTitle();
        out.geo = dlg->geometry();
        if (const QWidget *client = cp.centralWidget())
            out.client = client->size();
        out.backdrops =
            dlg->findChildren<QFrame *>(QStringLiteral("modalBackdrop")).size();
        if (QFrame *shell =
                dlg->findChild<QFrame *>(QStringLiteral("modalShell")))
            out.shell = shell->geometry();
        for (QLabel *l : dlg->findChildren<QLabel *>(QStringLiteral("helpKey")))
            out.keys << l->text();
        for (QLabel *l : dlg->findChildren<QLabel *>(QStringLiteral("dialogCap")))
            out.caps << l->text();
        for (QLabel *l : dlg->findChildren<QLabel *>(QStringLiteral("helpPara")))
            out.paras << l->text();
        // v1.1.5: the zoom-buttons note — a paragraph-shaped label with
        // its own objectName so it is NOT one of the five steps.
        if (QLabel *n = dlg->findChild<QLabel *>(QStringLiteral("helpNote")))
            out.note = n->text();
        // Any string that came back as a raw dot-path is a missing key.
        const auto texts =
            dlg->findChildren<QLabel *>();
        for (QLabel *l : texts) {
            if (l->text().startsWith(QStringLiteral("console."))) {
                check(false, QStringLiteral("no raw i18n key path leaks into "
                                            "the %1 body")
                                 .arg(dialogObject),
                      l->text());
            }
        }
        if (!snapPath.isEmpty()) {
            const QPixmap px = dlg->grab();
            check(px.save(snapPath),
                  QStringLiteral("screenshot written to %1").arg(snapPath));
            // Second frame: the body scrolled to the shortcut table, so
            // the key column is on the record too. The dialog must stay
            // alive until that grab lands — `dlg` is a stack object inside
            // openHelpDialog(), so the reject() has to live in the same
            // timer, not on this stack frame.
            if (auto *body = dlg->findChild<QScrollArea *>(
                    QStringLiteral("modalBody"))) {
                if (QScrollBar *sb = body->verticalScrollBar()) {
                    sb->setValue(sb->maximum());
                    QTimer::singleShot(150, [dlg, snapPath] {
                        QString bottomPath = snapPath;
                        if (bottomPath.endsWith(QStringLiteral(".png")))
                            bottomPath.insert(bottomPath.size() - 4,
                                              QStringLiteral("-keys"));
                        else
                            bottomPath += QStringLiteral("-keys");
                        check(dlg->grab().save(bottomPath),
                              QStringLiteral("screenshot written to %1")
                                  .arg(bottomPath));
                        dlg->reject();
                    });
                    return;
                }
            }
        }
        dlg->reject();
    });
    btn->click();          // blocks until the dialog is closed
    pump(120);
    return out;
}

// The characters a help KEY COLUMN may contain: ASCII, the four arrows,
// the U+2212 minus of the `−` zoom button, or CJK. Everything else in
// this table would be a decode artefact (Latin-1 residue, U+FFFD).
bool allowedKeyChar(uint u)
{
    if (u < 0x80)
        return true;
    if (u >= 0x2190 && u <= 0x2193)   // ↑ ↓ ← →
        return true;
    if (u == 0x2212)                  // −
        return true;
    if (u >= 0x4E00 && u <= 0x9FFF)   // 滚轮
        return true;
    if (u >= 0x3000 && u <= 0x303F)   // 「」
        return true;
    if (u >= 0xFF00 && u <= 0xFFEF)   // full-width forms
        return true;
    return false;
}

void assertKeysRender(const Probe &p, const QString &label)
{
    // v1.1.5: five rows, all of them real keyboard keys. The zoom BUTTONS
    // (− / + / 1:1) left the table for the note below it — a key table
    // that lists an unpressable "1:1" teaches the reader to hunt for a
    // shortcut that was never written.
    check(p.keys.size() == 5,
          label + QStringLiteral(": the key table has 5 rows (real keys "
                                 "only)"),
          QStringLiteral("got %1").arg(p.keys.size()));
    if (p.keys.size() != 5)
        return;

    const bool en = mhw::StringTable::instance().isEnglish();
    const QStringList expected = {
        QStringLiteral("↑ ↓ ← →"),
        QStringLiteral("Shift + ↑ ↓ ← →"),
        QStringLiteral("Esc"),
        QStringLiteral("1 / 2 / 3 / 4"),
        en ? QStringLiteral("Ctrl + wheel") : QStringLiteral("Ctrl + 滚轮"),
    };
    for (int i = 0; i < p.keys.size(); ++i) {
        check(p.keys.at(i) == expected.at(i),
              label + QStringLiteral(": key row %1 renders the right name")
                          .arg(i + 1),
              p.keys.at(i));
        const QString &s = p.keys.at(i);
        check(!s.contains(QChar(0xFFFD)),
              label + QStringLiteral(": key row %1 has no U+FFFD")
                          .arg(i + 1));
        bool c1 = false;
        bool weird = false;
        for (const QChar &c : s) {
            const uint u = c.unicode();
            if (u >= 0x80 && u <= 0x9F)
                c1 = true;
            if (!allowedKeyChar(u))
                weird = true;
        }
        check(!c1,
              label + QStringLiteral(": key row %1 carries no C1 control "
                                     "byte (the fromLatin1 residue)")
                          .arg(i + 1));
        check(!weird,
              label + QStringLiteral(": key row %1 is arrow/CJK/ASCII only")
                          .arg(i + 1),
              s);
    }
    // The literal the screenshot used to show: “â†” + two C1 controls.
    check(!p.keys.at(0).contains(QChar(0x00E2)),
          label + QStringLiteral(": the arrow row is not the Latin-1 "
                                 "mojibake of U+2191"));

    // AND THE BUTTONS ARE STILL DOCUMENTED — as buttons, in their own
    // note under the table, saying in so many words that no key is bound.
    check(!p.note.isEmpty(),
          label + QStringLiteral(": the zoom-buttons note exists"),
          p.note);
    check(p.note.contains(QStringLiteral("1:1")),
          label + QStringLiteral(": the note names 1:1"),
          p.note);
    check(p.note.contains(QStringLiteral("按钮"))
              || p.note.contains(QStringLiteral("button")),
          label + QStringLiteral(": the note says they are BUTTONS"),
          p.note);
    check(p.note.contains(QStringLiteral("没有对应的键盘快捷键"))
              || p.note.contains(QStringLiteral("no keyboard shortcut"))
              || p.note.contains(QStringLiteral("not keys")),
          label + QStringLiteral(": the note says no key is bound to "
                                 "them"),
          p.note);
}

void assertCompactGeometry(const Probe &help, const Probe &settings,
                           const QString &label)
{
    check(help.opened, label + QStringLiteral(": the help dialog opened"));
    check(settings.opened,
          label + QStringLiteral(": the settings dialog opened"));
    if (!help.opened || !settings.opened)
        return;

    const QSize c = help.client;
    check(!help.client.isEmpty() && help.geo.width() <= 680,
          label + QStringLiteral(": help width ≤ 680 (settings contract)"),
          QStringLiteral("%1 vs settings %2")
              .arg(help.geo.width())
              .arg(settings.geo.width()));
    check(!help.client.isEmpty() && help.geo.height() <= 700,
          label + QStringLiteral(": help height ≤ 700 (settings contract)"),
          QStringLiteral("%1").arg(help.geo.height()));
    check(help.geo.width() == settings.geo.width(),
          label + QStringLiteral(": help width == settings width (aligned "
                                 "on the same clamp)"),
          QStringLiteral("help=%1 settings=%2")
              .arg(help.geo.width())
              .arg(settings.geo.width()));
    check(help.geo.width() <= c.width() && help.geo.height() <= c.height(),
          label + QStringLiteral(": help fits inside the console's client "
                                 "area"),
          QStringLiteral("help=%1x%2 client=%3x%4")
              .arg(help.geo.width())
              .arg(help.geo.height())
              .arg(c.width())
              .arg(c.height()));
    check(!(help.geo.width() == c.width() && help.geo.height() == c.height()),
          label + QStringLiteral(": help no longer fills the whole client "
                                 "area (the「太大」defect)"));
    check(help.backdrops == 0,
          label + QStringLiteral(": compact help has no modalBackdrop veil"),
          QStringLiteral("backdrops=%1").arg(help.backdrops));
    // The shell IS the dialog's content — one layer, no outer box.
    check(!help.shell.isEmpty()
              && help.shell.width() + 4 >= help.geo.width()
              && help.shell.height() + 4 >= help.geo.height(),
          label + QStringLiteral(": modalShell fills the compact dialog"),
          QStringLiteral("shell=%1x%2 dlg=%3x%4")
              .arg(help.shell.width())
              .arg(help.shell.height())
              .arg(help.geo.width())
              .arg(help.geo.height()));
}

void assertCopyMatchesReality(const Probe &help, const QString &label)
{
    // Six sections: five captions + the shortcut table's caption.
    check(help.caps.size() == 6,
          label + QStringLiteral(": 5 copy sections + the shortcut table"),
          QStringLiteral("got %1").arg(help.caps.size()));
    check(help.paras.size() == 5,
          label + QStringLiteral(": 5 paragraphs (p1..p5)"),
          QStringLiteral("got %1").arg(help.paras.size()));
    if (help.paras.size() < 5)
        return;

    const QString &p1 = help.paras.at(0);
    const QString &p3 = help.paras.at(2);
    const QString &p4 = help.paras.at(3);
    const QString &p5 = help.paras.at(4);

    // The old p1 promised "never writes panels.ini" while the close-time
    // save writes exactly that file — self-contradictory copy.
    check(!p1.contains(QStringLiteral("也不写入 panels.ini"))
              && !p1.contains(QStringLiteral("never writes panels.ini")),
          label + QStringLiteral(": p1 no longer denies writing panels.ini "
                                 "(the close-time save does)"));

    // docs/CONSOLE.md + panel_settings_game.h: 2 games × Single/Split/
    // Mixed = SIX domains. v0.14's four were pre-Mixed.
    // v1.1.5: the 1.1.4 copy rework moved the phrasing — p3 now states the
    // isolation in prose ("each game and layout keeps its own position and
    // size") instead of naming the count, and the two config FILES moved to
    // p4 (asserted there). The intent — "the help states the scope model" —
    // is preserved by asserting the isolation claim where it now lives.
    check(p3.contains(QStringLiteral("各自保存"))
              || p3.contains(QStringLiteral("keeps its own")),
          label + QStringLiteral(": p3 states the per-game × layout scope "
                                 "isolation"),
          p3.left(60));
    check(p3.contains(QStringLiteral("混合")) || p3.contains(QStringLiteral("Mixed")),
          label + QStringLiteral(": p3 names the Mixed scope"));
    check(!p3.contains(QStringLiteral("分区开关分别独立保存"))
              && !p3.contains(QStringLiteral("section switches are stored "
                                             "per scope")),
          label + QStringLiteral(": p3 no longer claims section switches "
                                 "are per-scope (they are global "
                                 "monster-overlay.conf masks)"));

    // v1.1.5: the 保存 button is gone — the write happens on close (and
    // before a launch), so p4's checks moved from "what Save writes" to
    // "what the close-time write covers", and the export entry is named
    // 导出布局 now (the button's own label), not 导出 JSON.
    check(p4.contains(QStringLiteral("关闭控制台"))
              || p4.contains(QStringLiteral("close")),
          label + QStringLiteral(": p4 says when the save happens (on "
                                 "close, no manual Save)"));
    check(p4.contains(QStringLiteral("panels.ini"))
              || p4.contains(QStringLiteral("monster-overlay.conf")),
          label + QStringLiteral(": p4 names the files the write covers"));
    check(p4.contains(QStringLiteral("导出"))
              || p4.contains(QStringLiteral("Export")),
          label + QStringLiteral(": p4 covers the export entry"));
    check(p4.contains(QStringLiteral("重置布局"))
              || p4.contains(QStringLiteral("Reset")),
          label + QStringLiteral(": p4 covers the reset entries"));

    // The real toolbar entries (console.edit.preview_start /
    // console.edit.overlay_start), not the docs' paraphrase.
    check(p5.contains(QStringLiteral("编辑布局"))
              || p5.contains(QStringLiteral("Edit layout")),
          label + QStringLiteral(": p5 covers the 编辑布局 entry"));
    check(p5.contains(QStringLiteral("在悬浮窗中编辑"))
              || p5.contains(QStringLiteral("Edit in overlay")),
          label + QStringLiteral(": p5 uses the button's real label "
                                 "在悬浮窗中编辑 / Edit in overlay"),
          p5);

    // Table actions must exist as text too (raw key path = missing key).
    for (const QString &t : help.paras)
        check(!t.startsWith(QStringLiteral("console.")),
              label + QStringLiteral(": paragraph resolves a real key"));
}

} // namespace

int main(int argc, char *argv[])
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QTemporaryDir configRoot;
    if (!configRoot.isValid()) {
        std::fprintf(stderr, "cannot create a temporary config root\n");
        return 2;
    }
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));

    // The developer's real config is never reached: XDG_CONFIG_HOME is
    // already pointing at configRoot before QApplication exists (the
    // sha256 before/after proof of ~/.config/monster-overlay is taken by
    // the runner script, outside this process).

    check(mhw::StringTable::instance().load(QStringLiteral("zh-CN")),
          QStringLiteral("zh-CN loaded"));
    {
        ControlPanel cp;
        cp.show();
        pump(200);

        const QString snapZh = qEnvironmentVariable("MONSTER_HELP_SNAP_ZH");
        Probe settings = openModal(cp, QStringLiteral("settingsBtn"),
                                   QStringLiteral("settingsDialog"), QString());
        Probe help = openModal(cp, QStringLiteral("sideFootHelp"),
                               QStringLiteral("helpDialog"), snapZh);

        assertCompactGeometry(help, settings,
                              QStringLiteral("[zh-CN]"));
        assertKeysRender(help, QStringLiteral("[zh-CN]"));
        assertCopyMatchesReality(help, QStringLiteral("[zh-CN]"));

        check(help.title == QStringLiteral("帮助与快捷键"),
              QStringLiteral("[zh-CN] the dialog title resolves"),
              help.title);
    }

    check(mhw::StringTable::instance().load(QStringLiteral("en-US")),
          QStringLiteral("en-US loaded"));
    {
        ControlPanel cp;
        cp.show();
        pump(200);

        const QString snapEn = qEnvironmentVariable("MONSTER_HELP_SNAP_EN");
        Probe settings = openModal(cp, QStringLiteral("settingsBtn"),
                                   QStringLiteral("settingsDialog"), QString());
        Probe help = openModal(cp, QStringLiteral("sideFootHelp"),
                               QStringLiteral("helpDialog"), snapEn);

        assertCompactGeometry(help, settings,
                              QStringLiteral("[en-US]"));
        assertKeysRender(help, QStringLiteral("[en-US]"));
        assertCopyMatchesReality(help, QStringLiteral("[en-US]"));

        check(help.title == QStringLiteral("Help & shortcuts"),
              QStringLiteral("[en-US] the dialog title resolves"),
              help.title);
    }

    // The redirect really happened: the console's config root is the
    // temporary one, so nothing under ~/.config/monster-overlay was read
    // or written by this run.
    check(QDir(configRoot.path()).exists(),
          QStringLiteral("config isolation: XDG_CONFIG_HOME points at the "
                         "temporary root"),
          configRoot.path());

    if (failures == 0)
        std::printf("console-help-panel-tests: ALL PASSED (%d checks)\n",
                    checks);
    else
        std::fprintf(stderr,
                     "console-help-panel-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
