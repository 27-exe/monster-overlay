// L2 persistence smoke test: open the console, flip a couple of switches
// so the mask is no longer all-on, then let it destruct (save runs in
// ~ControlPanel()). Re-open and verify load applied the same mask.
//
// Runs offscreen; uses QStandardPaths via XDG_CONFIG_HOME redirect so
// the config file lands in /tmp where the test can inspect it.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QString>
#include <QTextStream>
#include <QVector>

#include "ui/control_panel.h"
#include "ui/panel_damage.h"
#include "ui/panel_pet_damage.h"
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
// order — is NOT the ctl_[0..3] order, so the old hard-coded starts
// {6,5,3} silently flipped the wrong panel's rows (and the counts were
// stale anyway: Player has 8 bits since v0.7.1 Wirebug, Monster 6 since
// v0.7.3 Tenderize, Damage 4 with OtherMembers, and Pets 2).
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
    if (row->isChecked() == on)
        return;
    row->setChecked(on);
    row->stateChanged(on ? Qt::Checked : Qt::Unchecked);
}

template <typename T>
T *findTopLevelPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *panel = qobject_cast<T *>(widget))
            return panel;
    }
    return nullptr;
}

bool selectRise(ControlPanel *cp)
{
    const QString rise = mhw::StringTable::instance().tr(
        QStringLiteral("console.game.rise"));
    for (QPushButton *button : cp->findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("gameBtn")
            && button->text() == rise) {
            button->click();
            return true;
        }
    }
    return false;
}

QString readBack()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&f);
    return in.readAll();
}

bool writeConfig(const QString &text)
{
    const QFileInfo info(configPath());
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile f(info.absoluteFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    return f.write(bytes) == bytes.size();
}

bool checked(ControlPanel *cp, const QString &key)
{
    SectionRow *row = findRow(cp, key);
    return row && row->isChecked();
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

    // A pre-pets three-row config must migrate Damage to include the new
    // OtherMembers bit while Pets keeps its all-on default.
    if (!writeConfig(QStringLiteral(
            "player=fb\nmonster=2f\ndamage=7\n"))) {
        fprintf(stderr, "FAIL: could not seed legacy config\n");
        return 20;
    }
    {
        ControlPanel legacy;
        legacy.show();
        app.processEvents();
        if (!checked(&legacy, QStringLiteral("otherMembers"))
            || !checked(&legacy, QStringLiteral("localPets"))
            || !checked(&legacy, QStringLiteral("otherPets"))) {
            fprintf(stderr, "FAIL: three-row config did not migrate new bits on\n");
            return 21;
        }
    }
    const QString migrated = readBack();
    if (!migrated.contains(QStringLiteral("damage=f"))
        || !migrated.contains(QStringLiteral("pets=3"))) {
        fprintf(stderr, "FAIL: migrated masks were not persisted:\n%s",
                qPrintable(migrated));
        return 22;
    }

    // Invalid and overflowing hexadecimal masks are ignored independently;
    // they must retain each panel's all-on default rather than parse as zero.
    if (!writeConfig(QStringLiteral(
            "player=fb\nmonster=2f\ndamage=not-hex\npets=100000000\n"))) {
        fprintf(stderr, "FAIL: could not seed invalid config\n");
        return 23;
    }
    {
        ControlPanel invalid;
        invalid.show();
        app.processEvents();
        const QStringList damageRows{
            QStringLiteral("rows"), QStringLiteral("share"),
            QStringLiteral("chart"), QStringLiteral("otherMembers")};
        for (const QString &key : damageRows) {
            if (!checked(&invalid, key)) {
                fprintf(stderr, "FAIL: invalid damage mask disabled '%s'\n",
                        qPrintable(key));
                return 24;
            }
        }
        if (!checked(&invalid, QStringLiteral("localPets"))
            || !checked(&invalid, QStringLiteral("otherPets"))) {
            fprintf(stderr, "FAIL: overflowing pets mask disabled defaults\n");
            return 25;
        }
    }
    QFile::remove(configPath());

    QString written;
    {
        ControlPanel cp;
        cp.show();
        app.processEvents();

        if (!selectRise(&cp)) {
            fprintf(stderr, "FAIL: Rise game selector missing\n");
            return 26;
        }
        app.processEvents();

        DamagePanel *damagePanel = findTopLevelPanel<DamagePanel>();
        PetDamagePanel *petPanel = findTopLevelPanel<PetDamagePanel>();
        if (!damagePanel || !petPanel) {
            fprintf(stderr, "FAIL: preview panels missing\n");
            return 27;
        }
        const int damageAllHeight = damagePanel->contentSize().height();
        const int petsAllHeight = petPanel->contentSize().height();

        // Both owner-scope toggles must change the edit-mode Pets demo.
        flipViaKey(&cp, QStringLiteral("localPets"), false);
        app.processEvents();
        const int petsOtherOnlyHeight = petPanel->contentSize().height();
        if (petsOtherOnlyHeight >= petsAllHeight) {
            fprintf(stderr, "FAIL: LocalPets did not reduce Pets preview (%d >= %d)\n",
                    petsOtherOnlyHeight, petsAllHeight);
            return 28;
        }
        flipViaKey(&cp, QStringLiteral("localPets"), true);
        flipViaKey(&cp, QStringLiteral("otherPets"), false);
        app.processEvents();
        const int petsLocalOnlyHeight = petPanel->contentSize().height();
        if (petsLocalOnlyHeight >= petsOtherOnlyHeight) {
            fprintf(stderr, "FAIL: OtherPets did not reduce Pets preview (%d >= %d)\n",
                    petsLocalOnlyHeight, petsOtherOnlyHeight);
            return 29;
        }
        flipViaKey(&cp, QStringLiteral("otherPets"), true);
        app.processEvents();
        if (petPanel->contentSize().height() != petsAllHeight) {
            fprintf(stderr, "FAIL: re-enabling OtherPets did not restore Pets preview\n");
            return 34;
        }
        flipViaKey(&cp, QStringLiteral("otherPets"), false);

        // DamagePanel itself is intentionally frozen; the console must still
        // prepare a local-only demo when OtherMembers is disabled.
        flipViaKey(&cp, QStringLiteral("otherMembers"), false);
        app.processEvents();
        const int damageLocalHeight = damagePanel->contentSize().height();
        if (damageLocalHeight >= damageAllHeight) {
            fprintf(stderr, "FAIL: OtherMembers did not reduce Damage preview (%d >= %d)\n",
                    damageLocalHeight, damageAllHeight);
            return 30;
        }
        flipViaKey(&cp, QStringLiteral("otherMembers"), true);
        app.processEvents();
        if (damagePanel->contentSize().height() != damageAllHeight) {
            fprintf(stderr, "FAIL: re-enabling OtherMembers did not restore Damage preview\n");
            return 35;
        }
        flipViaKey(&cp, QStringLiteral("otherMembers"), false);

        flipViaKey(&cp, QStringLiteral("weapon"),      false); // PlayerSection::Weapon OFF
        flipViaKey(&cp, QStringLiteral("parts"),       false); // MonsterSection::Parts OFF
        // After all flips the file must read:
        //   player=fb  (0xff & ~(1<<2))
        //   monster=2f (0x3f & ~(1<<4))
        //   damage=7   (0x0f & ~(1<<3))
        //   pets=1     (0x03 & ~(1<<1))
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
        SectionRow *weapon       = findRow(&cp2, QStringLiteral("weapon"));
        SectionRow *parts        = findRow(&cp2, QStringLiteral("parts"));
        SectionRow *otherMembers = findRow(&cp2, QStringLiteral("otherMembers"));
        SectionRow *otherPets    = findRow(&cp2, QStringLiteral("otherPets"));
        if (!weapon || !parts || !otherMembers || !otherPets) {
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
        if (otherMembers->isChecked() || otherPets->isChecked()) {
            fprintf(stderr, "FAIL: Rise damage filters should be OFF after load\n");
            return 17;
        }
    }

    // ------------------------------------------------------------------
    // Mask rows: four lowercase-hex lines, same order — the overlay's
    // locale_sync.h polls the file and older three-row configs remain readable.
    //
    // v0.9.2 semantics: the optional trailing `locale=` row records an EXPLICIT
    // language choice (--locale or the EN/CH chip). A mask save must neither
    // invent nor drop it, otherwise a detected language would silently
    // become a pinned one; the chip block below asserts it appears on a
    // click and survives the next mask write.
    // ------------------------------------------------------------------
    {
        const QString text = readBack();
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.size() != 4
            || lines[0] != QStringLiteral("player=fb")
            || lines[1] != QStringLiteral("monster=2f")
            || lines[2] != QStringLiteral("damage=7")
            || lines[3] != QStringLiteral("pets=1")) {
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
        if (mhw::StringTable::instance().tr(
                QStringLiteral("ui.pet_name_fallback"))
            != QStringLiteral("伙伴")) {
            fprintf(stderr, "FAIL: zh pet fallback is not localized\n");
            return 31;
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
        if (mhw::StringTable::instance().tr(
                QStringLiteral("ui.pet_name_fallback"))
            != QStringLiteral("Buddy")) {
            fprintf(stderr, "FAIL: en pet fallback is not localized\n");
            return 32;
        }
        const QString afterEn = readBack();
        if (!afterEn.contains(QStringLiteral("locale=en-US"))
            || !afterEn.contains(QStringLiteral("player=fb"))
            || !afterEn.contains(QStringLiteral("monster=2f"))
            || !afterEn.contains(QStringLiteral("damage=7"))
            || !afterEn.contains(QStringLiteral("pets=1"))) {
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
        if (mhw::StringTable::instance().tr(
                QStringLiteral("ui.pet_name_fallback"))
            != QStringLiteral("伙伴")) {
            fprintf(stderr, "FAIL: zh pet fallback was not restored\n");
            return 33;
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