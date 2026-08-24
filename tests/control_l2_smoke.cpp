// L2 persistence smoke test: open the console, flip a couple of switches
// so the mask is no longer all-on, then let it destruct (save runs in
// ~ControlPanel()). Re-open and verify load applied the same mask.
//
// Runs offscreen; uses QStandardPaths via XDG_CONFIG_HOME redirect so
// the config file lands in /tmp where the test can inspect it.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QString>
#include <QTextStream>
#include <QVector>

#include "ui/control_panel.h"
#include "ui/section_row.h"
#include "ui/toggle_chip.h"

namespace {
// The path MUST match ControlPanel::maskConfigPath() in
// ui/control_panel.cpp: $XDG_CONFIG_HOME/monster-overlay/monster-overlay.conf.
// Keep them in sync if you change one.
QString configPath()
{
    const QByteArray base = qgetenv("XDG_CONFIG_HOME");
    const QString root = base.isEmpty()
        ? QDir::homePath() + "/.config"
        : QString::fromLocal8Bit(base);
    return root + QStringLiteral("/monster-overlay/monster-overlay.conf");
}

// Walk ControlPanel's private ctl_ via Qt's introspection: every
// ToggleChip and SectionRow is a child of the central widget tree.
// Easier path for the test: add a thin friend hook instead? No —
// just dig via findChildren by class name.
void flipViaTree(ControlPanel *cp, int idx, int subIdx, bool on)
{
    auto rows = cp->findChildren<SectionRow *>();
    if (subIdx >= rows.size()) {
        qCritical("FAIL: not enough SectionRow children");
        std::exit(4);
    }
    // Group by parent — ctl_[0] children are first, then [1], then [2].
    // Sections-per-panel: 6, 5, 3. Use cumulative to pick the right one.
    int start = 0;
    for (int i = 0; i < idx; ++i) {
        // We need the per-panel count; read it back from the file
        // format count of subs. Hard-code from panel_sections.h.
        static const int kCounts[3] = { 6, 5, 3 };
        start += kCounts[i];
    }
    rows[start + subIdx]->setChecked(on);
}

QString readBack()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&f);
    return in.readAll();
}
} // namespace

int main(int argc, char *argv[])
{
    qputenv("XDG_CONFIG_HOME", "/tmp/monster-control-smoke");
    // XDG_CONFIG_HOME (read by QStandardPaths::GenericConfigLocation) is
    // picked up immediately on first writableLocation() call.
    QFile::remove(configPath());

    QApplication app(argc, argv);

    QString written;
    {
        ControlPanel cp;
        cp.show();
        app.processEvents();

        flipViaTree(&cp, 0, 2, false);  // player sub 2 OFF
        flipViaTree(&cp, 1, 4, false);  // monster sub 4 OFF
        app.processEvents();

        // Trigger save before destruction (dtor also saves, but being
        // explicit avoids any teardown-order surprises under QGuiAPP).
        // We can't call private saveMaskToDisk from outside, so the
        // dtor is the canonical save path. Drop cp out of scope.
    }
    written = readBack();
    if (written.isEmpty()) {
        fprintf(stderr, "FAIL: dtor did not write config\n");
        return 2;
    }
    fprintf(stderr, "written:\n%s", qPrintable(written));
    fflush(stderr);

    // Re-open: load should restore the same SectionRow checked state.
    {
        ControlPanel cp2;
        cp2.show();
        app.processEvents();
        auto rows = cp2.findChildren<SectionRow *>();
        // player sub 2 should be OFF
        // Cumulative: player 6 entries (0..5), sub 2 → row index 2.
        if (rows.size() < 7) {
            fprintf(stderr, "FAIL: not enough rows on reopen (have %d)\n", rows.size());
            return 5;
        }
        if (rows[2]->isChecked()) {
            fprintf(stderr, "FAIL: player sub 2 should be OFF after load\n");
            return 6;
        }
        if (rows[6 + 4]->isChecked()) {
            fprintf(stderr, "FAIL: monster sub 4 should be OFF after load\n");
            return 7;
        }
    }

    QFile::remove(configPath());
    fprintf(stderr, "PASS\n");
    return 0;
}