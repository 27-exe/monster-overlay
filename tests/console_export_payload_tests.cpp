// SPDX-License-Identifier: Apache-2.0
//
// The export snapshot must name the mode it was taken in.
//
// The user's own export said `"layout": "single"` while the console was in
// Mixed, because refreshExportPayload() answered the three-mode question
// with `split ? "split" : "single"` — a two-valued decision in a three-valued
// domain, which silently folds Mixed into Single. A snapshot that
// misreports the topology it was taken in is the one thing an export must
// never be, and nothing else in the tree would have caught it.
//
// THE SHAPE OF THE TEST. Walk all THREE modes and require each to name
// itself, using the same spelling the conf row uses
// (mhw::playerLayoutModeToString). A two-valued check would pass on two of
// the three and only ever fail on the third — so the loop, not a single
// Mixed case, is what pins the contract.

#include <QApplication>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#define private public
#define protected public
#include "ui/control_panel.h"
#include "ui/layout_package.h"
#include "ui/panel_sections.h"
#undef private
#undef protected

namespace {

int failures = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "PASS:" : "FAIL:",
                what.toUtf8().constData(),
                detail.isEmpty() ? "" : " -- ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
    if (!ok)
        ++failures;
}

void pump(int rounds = 40)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents();
}

} // namespace

int main(int argc, char *argv[])
{
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "FAIL: no scratch HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    {
        QSettings game;
        game.setValue(QStringLiteral("game"), QStringLiteral("world"));
        game.sync();
    }
    {
        // A store to export FROM. The test's config dir starts empty, and
        // an empty store has nothing to export — the dialog correctly says
        // so in words, which is not JSON and would make every parse below
        // fail for the wrong reason. Two scopes, so the granularity
        // levels have something to differ about.
        QSettings panels(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("monster-overlay"),
                         QStringLiteral("panels"));
        for (const char *scope : {"world/mixed", "world/single",
                                  "rise/mixed", "rise/single",
                                  "rise/split", "world/split"}) {
            panels.beginGroup(
                QString::fromLatin1(scope) + QStringLiteral("/player-bars"));
            panels.setValue(QStringLiteral("ml"), 30);
            panels.setValue(QStringLiteral("mt"), 40);
            panels.endGroup();
            panels.beginGroup(
                QString::fromLatin1(scope) + QStringLiteral("/player"));
            panels.setValue(QStringLiteral("ml"), 10);
            panels.setValue(QStringLiteral("mt"), 20);
            panels.endGroup();
        }
        panels.sync();
    }

    std::printf("== console_export_payload_tests ==\n");

    ControlPanel cp;
    cp.show();
    pump();
    // THE DIALOG'S WIDGETS, BUILT BY HAND. This test never opens the
    // dialog, so every member refreshExportPayload() reaches for has to
    // exist here too — exportScope_ being null is a segfault, not a
    // "defaults to the current scope" (the null check inside the function
    // covers the not-yet-opened case, but a member the test then DEREFERENCES
    // is a different matter).
    cp.exportText_ = new QPlainTextEdit();
    cp.exportScope_ = new QComboBox();
    cp.exportScope_->addItem(QStringLiteral("group"));
    cp.exportScope_->addItem(QStringLiteral("current"));
    cp.exportScope_->addItem(QStringLiteral("all"));

    using Mode = mhw::PlayerSection::PlayerLayoutMode;
    const QList<Mode> modes = {Mode::Single, Mode::Split, Mode::Mixed};

    // v1.1.5: the payload is a layout package, so the assertions are about
    // the package's HEADER — which is the thing an import reads first and
    // the thing that used to lie (a Mixed console exported "layout":
    // "single"). Walking all three modes is what pins it: a two-valued
    // check passes on two of them and only ever fails on the third.
    for (Mode mode : modes) {
        cp.layoutModeInForce_ = mode;
        cp.refreshExportPayload();
        const QByteArray text = cp.exportText_->toPlainText().toUtf8();
        QString error;
        const auto pkg = mhw::LayoutPackage::parse(text, &error);
        check(pkg.has_value(),
              QStringLiteral("mode %1 exports a parseable package -- %2")
                  .arg(mhw::playerLayoutModeToString(mode), error));
        if (!pkg)
            continue;
        const QString want =
            QStringLiteral("world/") + mhw::playerLayoutModeToString(mode);
        check(pkg->containsScope(want),
              QStringLiteral("mode %1's header names scope %2")
                  .arg(mhw::playerLayoutModeToString(mode), want));
        check(!pkg->scopeNames().isEmpty(),
              QStringLiteral("mode %1 carries at least one scope")
                  .arg(mhw::playerLayoutModeToString(mode)));
    }

    // The granularity levels must actually differ: a selector whose three
    // answers are identical is a control that lies about what it does.
    {
        cp.layoutModeInForce_ = Mode::Mixed;
        cp.exportScope_->setCurrentIndex(2);   // 全部打包
        cp.refreshExportPayload();
        QString error;
        const auto all = mhw::LayoutPackage::parse(
            cp.exportText_->toPlainText().toUtf8(), &error);
        check(all.has_value(),
              QStringLiteral("the whole-store export parses -- %1").arg(error));
        cp.exportScope_->setCurrentIndex(1);   // 当前作用域
        cp.refreshExportPayload();
        const auto one = mhw::LayoutPackage::parse(
            cp.exportText_->toPlainText().toUtf8(), &error);
        check(all && one && all->scopeNames().size() >= one->scopeNames().size(),
              QStringLiteral("全部打包 is not narrower than 当前作用域 "
                             "(%1 vs %2)")
                  .arg(all ? all->scopeNames().size() : -1)
                  .arg(one ? one->scopeNames().size() : -1));
        check(one && one->scopeNames().size() == 1,
              "当前作用域 exports exactly one scope");
        // The global block belongs to 全部打包 only: a single-group export
        // that also rewrote the masks on import would be a much bigger
        // hammer than the user asked for.
        check(all && all->hasGlobal,
              "全部打包 carries the global block (masks, language, plans)");
        check(one && !one->hasGlobal,
              "当前作用域 does NOT carry the global block");
    }

    // No `demo` flag any more, and that is the point: the old snapshot was
    // a view of demo-seeded PANELS and said so. A layout package carries
    // no content at all, so the honesty question the flag answered does
    // not arise — and a file that claims to hold content would be the new
    // version of the same lie.

    std::printf("\nRESULT: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
