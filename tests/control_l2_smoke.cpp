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

// Locate a SectionRow by its English key label (e.g. "WEAPON"), which is
// unique across the whole console. Index-based lookup is a trap: the
// QStackedWidget page order — and therefore findChildren()'s traversal
// order — is NOT the ctl_[0..2] order, so the old hard-coded starts
// {6,5,3} silently flipped the wrong panel's rows (and the counts were
// stale anyway: Player has 8 bits since v0.7.1 Wirebug, Monster 6 since
// v0.7.3 Tenderize).
SectionRow *findRow(ControlPanel *cp, const QString &key)
{
    const QString want = key.toUpper();
    for (SectionRow *r : cp->findChildren<SectionRow *>()) {
        for (QLabel *l : r->findChildren<QLabel *>()) {
            if (l->text() == want)
                return r;
        }
    }
    return nullptr;
}

void flipViaKey(ControlPanel *cp, const QString &key, bool on)
{
    SectionRow *row = findRow(cp, key);
    if (!row) {
        qCritical("FAIL: SectionRow '%s' not found", qPrintable(key));
        std::exit(4);
    }
    row->setChecked(on);
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

        flipViaKey(&cp, QStringLiteral("weapon"), false);  // PlayerSection::Weapon OFF
        flipViaKey(&cp, QStringLiteral("parts"),  false);  // MonsterSection::Parts OFF
        // After both flips the file must read:
        //   player=fb  (0xff & ~(1<<2))
        //   monster=2f (0x3f & ~(1<<4))
        //   damage=7
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
        SectionRow *weapon = findRow(&cp2, QStringLiteral("weapon"));
        SectionRow *parts  = findRow(&cp2, QStringLiteral("parts"));
        if (!weapon || !parts) {
            fprintf(stderr, "FAIL: rows missing on reopen\n");
            return 5;
        }
        if (weapon->isChecked()) {
            fprintf(stderr, "FAIL: player weapon section should be OFF after load\n");
            return 6;
        }
        if (parts->isChecked()) {
            fprintf(stderr, "FAIL: monster parts section should be OFF after load\n");
            return 7;
        }
    }

    QFile::remove(configPath());
    fprintf(stderr, "PASS\n");
    return 0;
}