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
#include <QMouseEvent>
#include <QString>
#include <QTextStream>
#include <QVector>

#include "ui/control_panel.h"
#include "ui/section_row.h"
#include "ui/toggle_chip.h"
#include "core/locale_conf.h"
#include "core/string_table.h"

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
    // v0.9.2: the constructor's locale safety net falls back to the detected
    // system locale, so pin the environment for a deterministic expectation
    // (a zh desktop -> zh-CN console chrome).
    qputenv("LC_ALL", "zh_CN.UTF-8");
    // XDG_CONFIG_HOME (read by QStandardPaths::GenericConfigLocation) is
    // picked up immediately on first writableLocation() call.
    QFile::remove(configPath());

    QApplication app(argc, argv);
    // v0.9.1 regression: the console's brand line must substitute the RUNTIME
    // version (it used to be a hardcoded "0.5" inside the translation string).
    // A synthetic value proves the substitution without pinning the release.
    QApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));

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

    // ------------------------------------------------------------------
    // Mask rows: three lowercase-hex lines, same order — the overlay's
    // locale_sync.h polls the file and older readers must keep working.
    //
    // v0.9.2 semantics: the optional 4th `locale=` row records an EXPLICIT
    // language choice (--locale or the EN/CH chip). A mask save must neither
    // invent nor drop it, otherwise a detected language would silently
    // become a pinned one; the chip block below asserts it appears on a
    // click and survives the next mask write.
    // ------------------------------------------------------------------
    {
        const QString text = readBack();
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.size() != 3
            || lines[0] != QStringLiteral("player=fb")
            || lines[1] != QStringLiteral("monster=2f")
            || lines[2] != QStringLiteral("damage=7")) {
            fprintf(stderr, "FAIL: mask conf layout wrong (%d lines):\n%s",
                    int(lines.size()), qPrintable(text));
            return 8;
        }
        if (text.contains(QStringLiteral("locale="))) {
            fprintf(stderr, "FAIL: a mask save pinned a locale row:\n%s", qPrintable(text));
            return 19;
        }
        fprintf(stderr, "mask conf layout OK (no locale row before a choice):\n%s",
                qPrintable(text));
    }

    // ------------------------------------------------------------------
    // v0.9 i18n: clicking the EN/CH chip must (1) retranslate the live UI
    // without a restart and (2) rewrite ONLY the locale row of the conf.
    // The chip is a frame with two mouse-transparent segment labels, so the
    // release is delivered to the frame and handled by
    // ControlPanel::eventFilter. This asserts both clicks (zh -> en -> zh).
    // ------------------------------------------------------------------
    {
        ControlPanel cp3;
        cp3.show();
        app.processEvents();

        QWidget *chip = cp3.findChild<QWidget *>(QStringLiteral("localeChip"));
        QLabel *brandSub = cp3.findChild<QLabel *>(QStringLiteral("railBrandSub"));
        if (!chip || !brandSub) {
            fprintf(stderr, "FAIL: locale chip / railBrandSub missing\n");
            return 9;
        }
        if (brandSub->text() != QStringLiteral("控制台  ·  %1").arg(QApplication::applicationVersion())) {
            fprintf(stderr, "FAIL: expected zh copy before the switch, got '%s'\n",
                    qPrintable(brandSub->text()));
            return 10;
        }

        auto clickChip = [&]{
            const QPoint p = chip->rect().center();
            const QPointF pf(p);
            QMouseEvent press(QEvent::MouseButtonPress, pf, pf,
                              chip->mapToGlobal(p), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, pf, pf,
                                chip->mapToGlobal(p), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(chip, &press);
            QApplication::sendEvent(chip, &release);
            app.processEvents();
        };

        clickChip();   // zh-CN -> en-US
        if (brandSub->text() != QStringLiteral("CONTROL CONSOLE  ·  %1").arg(QApplication::applicationVersion())) {
            fprintf(stderr, "FAIL: chip click did not retranslate ('%s')\n",
                    qPrintable(brandSub->text()));
            return 11;
        }
        const QString afterEn = readBack();
        if (!afterEn.contains(QStringLiteral("locale=en-US"))
            || !afterEn.contains(QStringLiteral("player=fb"))
            || !afterEn.contains(QStringLiteral("monster=2f"))
            || !afterEn.contains(QStringLiteral("damage=7"))) {
            fprintf(stderr, "FAIL: mask rows must survive the locale write:\n%s",
                    qPrintable(afterEn));
            return 12;
        }
        fprintf(stderr, "after EN click:\n%s", qPrintable(afterEn));
        // Evidence hook: keep a copy of the conf exactly as the EN click left
        // it (used by ws-d/ evidence capture; no effect when unset).
        const QByteArray dump = qgetenv("WS_D_CONF_DUMP");
        if (!dump.isEmpty()) {
            QFile out(QString::fromLocal8Bit(dump));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                out.write(afterEn.toUtf8());
        }

        clickChip();   // en-US -> zh-CN
        if (brandSub->text() != QStringLiteral("控制台  ·  %1").arg(QApplication::applicationVersion())) {
            fprintf(stderr, "FAIL: second click did not restore zh copy ('%s')\n",
                    qPrintable(brandSub->text()));
            return 13;
        }
        if (!readBack().contains(QStringLiteral("locale=zh-CN"))) {
            fprintf(stderr, "FAIL: second click did not write locale=zh-CN\n");
            return 14;
        }
        fprintf(stderr, "locale chip: zh -> en -> zh verified\n");
    }

    // ------------------------------------------------------------------
    // v0.9 i18n integration regression: a stale `locale=` row in the conf
    // must NOT override the process's explicitly loaded locale. At app
    // startup main_control.cpp loads the CLI locale (`--locale en-US`)
    // BEFORE constructing the panel while the conf may still say zh-CN;
    // the constructor must not silently re-apply the disk value (the old
    // loadMaskFromDisk() applyLocale() did, producing a split-language UI).
    // ------------------------------------------------------------------
    {
        mhw::writeLocaleToConf(QStringLiteral("en-US"), configPath());
        mhw::StringTable::instance().load(QStringLiteral("zh-CN"));
        ControlPanel cp4;
        cp4.show();
        app.processEvents();
        if (mhw::StringTable::instance().currentLocale() != QStringLiteral("zh-CN")) {
            fprintf(stderr, "FAIL: stale conf locale overrode the explicit table locale ('%s')\n",
                    qPrintable(mhw::StringTable::instance().currentLocale()));
            return 15;
        }
        QLabel *brandSub = cp4.findChild<QLabel *>(QStringLiteral("railBrandSub"));
        if (!brandSub || brandSub->text() != QStringLiteral("控制台  ·  %1").arg(QApplication::applicationVersion())) {
            fprintf(stderr, "FAIL: console chrome did not stay on the explicit locale ('%s')\n",
                    brandSub ? qPrintable(brandSub->text()) : "railBrandSub missing");
            return 16;
        }
        fprintf(stderr, "explicit-locale dominance over stale conf: OK\n");
    }

    QFile::remove(configPath());
    fprintf(stderr, "PASS\n");
    return 0;
}