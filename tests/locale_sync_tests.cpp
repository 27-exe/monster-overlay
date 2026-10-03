// SPDX-License-Identifier: Apache-2.0
// Unit tests for the i18n locale plumbing introduced with the EN/CH
// language switch:
//
//   locale_conf.h  — conf read/write, mask-line preservation, atomic write
//   locale_sync.h  — overlay-side change-detection state machine
//   StringTable    — per-locale multi-file qrc loading, in-place reload,
//                    isEnglish() switch
//
// No game process required. Conf tests only ever touch paths inside a
// QTemporaryDir — the user's real ~/.config/monster-overlay is never
// opened, let alone written.

#include "core/locale_conf.h"
#include "core/locale_sync.h"
#include "core/string_table.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

QString slurp(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

bool writeAll(const QString& path, const QString& text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    f.write(text.toUtf8());
    f.close();
    return true;
}

// mtime granularity: ms timestamps need a gap so a rewrite produces a new
// mtime. Real console clicks are seconds apart; tests just need 2 distinct
// OS ticks.
constexpr int kMtimeTickMs = 30;

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "FATAL: no temp dir\n");
        return 2;
    }

    // ---------------------------------------------------------------- conf
    const QString conf = tmp.filePath("monster-overlay.conf");

    check(mhw::readLocaleFromConf(conf).isEmpty(),
          "read on missing conf -> empty");
    check(!mhw::writeLocaleToConf(QString(), conf),
          "write empty locale refused");

    check(mhw::writeLocaleToConf(QStringLiteral("zh-CN"), conf),
          "write locale to fresh file");
    check(mhw::readLocaleFromConf(conf) == QStringLiteral("zh-CN"),
          "read back zh-CN");
    check(mhw::writeLocaleToConf(QStringLiteral("ZH_cn.UTF-8"), conf),
          "write locale spelling variant");
    check(slurp(conf).contains(QStringLiteral("locale=zh-CN\n")),
          "locale writer persists the canonical locale id");

    check(writeAll(conf, QStringLiteral(
              "player=ff\nmonster=1f\ndamage=7\nlocale=zh-CN\n")),
          "seed masks + locale");
    check(mhw::writeLocaleToConf(QStringLiteral("en-US"), conf),
          "rewrite locale over existing file");
    const QString txt = slurp(conf);
    check(txt.contains(QStringLiteral("player=ff"))
              && txt.contains(QStringLiteral("monster=1f"))
              && txt.contains(QStringLiteral("damage=7")),
          "mask lines preserved across locale write");
    check(txt.count(QStringLiteral("locale=")) == 1
              && mhw::readLocaleFromConf(conf) == QStringLiteral("en-US"),
          "locale line replaced exactly once");

    check(writeAll(conf, QStringLiteral("locale=ZH_cn.UTF-8\n")),
          "seed locale spelling variant");
    check(mhw::readLocaleFromConf(conf) == QStringLiteral("zh-CN"),
          "conf reader canonicalizes locale spelling variants");
    check(writeAll(conf, QStringLiteral("locale=unknown_LOCALE\n")),
          "seed unsupported locale");
    check(mhw::readLocaleFromConf(conf) == QStringLiteral("en-US"),
          "conf reader safely maps unsupported locale to en-US");

    // ---------------------------------------------------------------- sync
    const QString conf2 = tmp.filePath("sync.conf");
    mhw::LocaleSyncState st = mhw::localeSyncInit(conf2);
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "poll before file exists -> no change");

    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf2, QStringLiteral("locale=zh-CN\n")),
          "create conf with zh-CN");
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "first poll, conf matches active locale -> no flip");
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "steady state -> no change");

    QThread::msleep(kMtimeTickMs);
    check(mhw::writeLocaleToConf(QStringLiteral("en-US"), conf2),
          "console flips conf to en-US");
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN"))
              == QStringLiteral("en-US"),
          "poll reports the new locale en-US");
    // The flip is settled the way the overlay does it: the poll only
    // proposes, the caller applies and then retires the request.
    mhw::localeSyncAccept(st, QStringLiteral("en-US"));
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("en-US")).isEmpty(),
          "same change not reported twice");

    QThread::msleep(kMtimeTickMs);
    check(mhw::writeLocaleToConf(QStringLiteral("zh-CN"), conf2),
          "console flips back to zh-CN");
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("en-US"))
              == QStringLiteral("zh-CN"),
          "poll reports zh-CN after an EN session");
    mhw::localeSyncAccept(st, QStringLiteral("zh-CN"));
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "settled zh-CN flip is not re-offered");

    QFile::remove(conf2);
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "poll after deletion -> no change, no crash");

    // --locale override: the overlay primes the state AFTER reading its
    // startup locale, so a pre-existing conf value equal to the override
    // must not re-trigger.
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf2, QStringLiteral("locale=zh-CN\n")), "re-create conf");
    mhw::LocaleSyncState st2 = mhw::localeSyncInit(conf2);   // primed, sees zh-CN
    check(mhw::localeSyncPoll(st2, conf2, QStringLiteral("en-US")).isEmpty(),
          "pre-existing conf value does not override --locale startup");

    // ------------------------------------------------- refused flips (v0.11.4)
    //
    // The regression this file exists to guard: proposing a locale and
    // applying it are two phases. localeSyncPoll() only reports what the
    // conf asks for; the caller applies it and only then retires the
    // request with localeSyncAccept(). A load that FAILS (missing /
    // unreadable locale dir) must therefore leave the request
    // outstanding, so the very next poll re-offers the same flip from the
    // very same conf file. Before v0.11.4 the poll itself recorded the
    // locale as seen before the caller had loaded anything, and the conf
    // mtime never changed afterwards — the candidate was swallowed by
    // the mtime short-circuit on every later poll and the UI was stuck on
    // the old language until a restart.
    //
    // applyLikeOverlay() mirrors src/main.cpp's applyLocale(): try to
    // load, and retire the request only when the load succeeded.
    const QString conf3 = tmp.filePath("stuck.conf");
    mhw::LocaleSyncState st3 = mhw::localeSyncInit(conf3);    // sees ""
    QString uiLocale = QStringLiteral("zh-CN");
    bool resourcesMissing = true;   // the load will be refused
    auto applyLikeOverlay = [&](const QString& next) -> bool {
        if (resourcesMissing)
            return false;           // StringTable::load() would fail
        uiLocale = next;            // StringTable::currentLocale() now
        mhw::localeSyncAccept(st3, uiLocale);
        return true;
    };
    auto tick = [&](const QString& proposed) {
        return applyLikeOverlay(proposed);
    };

    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("locale=en-US\n")),
          "console asks for en-US");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "poll proposes en-US");
    check(!tick(QStringLiteral("en-US")),
          "refused load returns false (resources missing)");
    check(uiLocale == QStringLiteral("zh-CN"),
          "refused load leaves the UI on the previous locale");

    // THE BUG: identical conf file, nothing rewritten on disk. The
    // proposal must survive the mtime short-circuit and be re-offered.
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "refused flip is re-offered from the SAME conf file");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "refused flip keeps being offered until it lands");
    check(!tick(QStringLiteral("en-US")),
          "still refused -> still not accepted");

    // The resource files show up; the untouched conf now gets honoured.
    resourcesMissing = false;
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "same conf still proposes en-US after the resources return");
    check(tick(QStringLiteral("en-US")),
          "applying the flip now succeeds");
    check(uiLocale == QStringLiteral("en-US"),
          "UI actually switched");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "accepted flip stops being offered (steady state)");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "still quiet after acceptance");

    // A pending proposal must not survive the user abandoning the
    // requested language: the conf goes back to what the UI shows.
    resourcesMissing = true;
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("locale=zh-CN\n")),
          "console asks for zh-CN while the resources are gone");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("zh-CN"),
          "zh-CN proposed");
    check(!tick(QStringLiteral("zh-CN")), "that request is refused too");

    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("locale=en-US\n")),
          "user flips back to the locale already on screen");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "conf agreeing with the live locale retires the proposal");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "nothing left outstanding");
    // An unshipped request canonicalizes to en-US, i.e. to whatever the
    // UI already shows — it must not be turned into a flip.
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("locale=ja-JP\n")),
          "console writes an unshipped locale id");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "unshipped request canonicalizes to the live locale, no flip");

    // A superseding request replaces an outstanding one rather than
    // being buried by it.
    resourcesMissing = false;
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("player=1\nlocale=zh-CN\n")),
          "console asks for zh-CN (masks rewritten too)");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("zh-CN"),
          "newer request is proposed");
    check(tick(QStringLiteral("zh-CN")), "newer request applied");
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("player=2\nlocale=zh-CN\n")),
          "masks rewritten again, same locale row");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "settled request is not disturbed by an unrelated rewrite");
    check(tick(QString()), "nothing left to apply");

    // localeSyncAccept() only retires the request it is actually
    // delivering: a late / stray accept for something else must not bury
    // the request that is still outstanding. The overlay calls it exactly
    // once per successful load, but the guard keeps the detector honest
    // even if a caller does not.
    QThread::msleep(kMtimeTickMs);
    check(writeAll(conf3, QStringLiteral("locale=en-US\n")), "console asks for en-US");
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "en-US proposed again");
    mhw::localeSyncAccept(st3, QStringLiteral("zh-CN"));   // stray accept
    check(mhw::localeSyncPoll(st3, conf3, uiLocale) == QStringLiteral("en-US"),
          "stray accept does not bury the live proposal");
    mhw::localeSyncAccept(st3, QStringLiteral("en-US"));
    check(mhw::localeSyncPoll(st3, conf3, uiLocale).isEmpty(),
          "accepting the right request settles it");
    uiLocale = QStringLiteral("en-US");

    // The real loader is the other half of the contract. Accepting only
    // after a successful load is safe precisely because a load that FAILS
    // leaves currentLocale() (and therefore the UI) untouched — if it did
    // not, retiring the request after `load()` would strand the state on a
    // language nobody ever showed.
    check(mhw::StringTable::instance().load(QStringLiteral("zh-CN")),
          "real loader: zh-CN loads");
    check(!mhw::StringTable::instance().load(QStringLiteral("no-such-locale")),
          "real loader: unshipped locale fails");
    check(mhw::StringTable::instance().currentLocale() == QStringLiteral("zh-CN"),
          "real loader: failed load keeps the active locale");

    // Why the "resources missing" path above is mocked rather than driven
    // through StringTable::load(): readLocaleFromConf() canonicalizes any
    // unshipped id (ja-JP, de-DE, ...) down to en-US, so the conf can never
    // name a locale whose directory is absent — the poll's `repeat` branch
    // correctly suppresses it (see the ja-JP case above). A missing
    // resource dir is a packaging fault the conf row cannot express, so the
    // mock is the only way to reach it; everything else in this block runs
    // against the real loader.

    // --------------------------------------- locale detection (v0.9.2 policy)
    // --locale > conf row > detected system locale. Detection answers zh-CN
    // only for a zh* environment; C/POSIX/unset and any unshipped language
    // fall back to the shipping default, en-US.
    check(mhw::normalizeLocaleCode(QStringLiteral("zh_CN.UTF-8")) == QStringLiteral("zh-cn"),
          "normalize: zh_CN.UTF-8 -> zh-cn");
    check(mhw::normalizeLocaleCode(QStringLiteral("en_US.UTF-8@euro")) == QStringLiteral("en-us"),
          "normalize: strips .codeset and @modifier");
    check(mhw::normalizeLocaleCode(QStringLiteral("C")).isEmpty(), "normalize: C is not a language");
    check(mhw::normalizeLocaleCode(QStringLiteral("POSIX")).isEmpty(),
          "normalize: POSIX is not a language");

    // Explicit and persisted locale values share one startup resolver in the
    // console and overlay. Every accepted spelling must leave that helper as
    // one of the two locale ids that actually ship.
    check(mhw::resolveStartupLocale(QStringLiteral("zh_CN.UTF-8"),
                                    QStringLiteral("en-US"),
                                    QStringLiteral("en-US"))
              == QStringLiteral("zh-CN"),
          "policy: CLI zh_CN.UTF-8 canonicalizes to zh-CN");
    check(mhw::resolveStartupLocale(QStringLiteral("EN_us"),
                                    QStringLiteral("zh-CN"),
                                    QStringLiteral("zh-CN"))
              == QStringLiteral("en-US"),
          "policy: CLI case/underscore variant canonicalizes to en-US");
    check(mhw::resolveStartupLocale(QString(), QStringLiteral("ZH_cn.utf8"),
                                    QStringLiteral("en-US"))
              == QStringLiteral("zh-CN"),
          "policy: conf locale variant canonicalizes to zh-CN");
    check(mhw::resolveStartupLocale(QStringLiteral("xx_YY"),
                                    QStringLiteral("zh-CN"),
                                    QStringLiteral("zh-CN"))
              == QStringLiteral("en-US"),
          "policy: unknown explicit locale safely falls back to en-US");
    check(mhw::resolveStartupLocale(QStringLiteral("zhish"), QString(),
                                    QStringLiteral("en-US"))
              == QStringLiteral("en-US"),
          "policy: unknown zh-prefixed locale safely falls back to en-US");
    check(mhw::resolveStartupLocale(QString(), QStringLiteral("POSIX"),
                                    QStringLiteral("zh-CN"))
              == QStringLiteral("en-US"),
          "policy: POSIX persisted locale safely falls back to en-US");

    check(mhw::detectLocaleFromEnv(QString(), QString(), QStringLiteral("zh_CN.UTF-8"))
              == QStringLiteral("zh-CN"),
          "detect: LANG zh_CN -> zh-CN");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QStringLiteral("zh_TW.UTF-8"))
              == QStringLiteral("zh-CN"),
          "detect: zh_TW maps to zh-CN (only two tables ship)");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QStringLiteral("en_US.UTF-8"))
              == QStringLiteral("en-US"),
          "detect: en_US -> en-US");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QStringLiteral("de_DE.UTF-8"))
              == QStringLiteral("en-US"),
          "detect: unshipped language -> en-US");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QStringLiteral("C")) == QStringLiteral("en-US"),
          "detect: LANG=C -> en-US, never zh-CN");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QString()) == QStringLiteral("en-US"),
          "detect: empty environment -> en-US");
    check(mhw::detectLocaleFromEnv(QStringLiteral("en_US.UTF-8"), QString(),
                                   QStringLiteral("zh_CN.UTF-8")) == QStringLiteral("en-US"),
          "detect: LC_ALL wins over LANG");
    check(mhw::detectLocaleFromEnv(QStringLiteral("C"), QString(),
                                   QStringLiteral("zh_CN.UTF-8")) == QStringLiteral("en-US"),
          "detect: LC_ALL=C is an en-US decision, not an absent override");
    check(mhw::detectLocaleFromEnv(QString(), QStringLiteral("zh_CN.UTF-8"),
                                   QStringLiteral("en_US.UTF-8")) == QStringLiteral("zh-CN"),
          "detect: LC_MESSAGES wins over LANG");
    check(mhw::detectLocaleFromEnv(QString(), QString(), QString(),
                                   QStringLiteral("zh_CN:en_US")) == QStringLiteral("zh-CN"),
          "detect: LANGUAGE list is the last resort");

    check(mhw::resolveStartupLocale(QStringLiteral("en-US"), QStringLiteral("zh-CN"),
                                    QStringLiteral("zh-CN")) == QStringLiteral("en-US"),
          "policy: --locale beats conf and detection");
    check(mhw::resolveStartupLocale(QString(), QStringLiteral("en-US"), QStringLiteral("zh-CN"))
              == QStringLiteral("en-US"),
          "policy: the conf row is sticky (manual choice survives)");
    check(mhw::resolveStartupLocale(QString(), QString(), QStringLiteral("zh-CN"))
              == QStringLiteral("zh-CN"),
          "policy: no choice follows the detected locale");
    check(mhw::systemLocale().startsWith(QLatin1String("zh"))
              || mhw::systemLocale() == QStringLiteral("en-US"),
          "policy: systemLocale() always yields a shipping locale id");

    // ----------------------------------------------------- QSettings migration
    const QString legacySettings = tmp.filePath("Unknown Organization/monster-control.conf");
    const QString currentSettings = tmp.filePath("monster-overlay/monster-control.conf");
    QDir().mkpath(QFileInfo(legacySettings).absolutePath());
    {
        QSettings legacy(legacySettings, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("game"), QStringLiteral("rise"));
        legacy.setValue(QStringLiteral("ui/zoom"), 2.5);
        legacy.setValue(QStringLiteral("ui/geometry"), QByteArray("opaque-geometry"));
        legacy.sync();
    }
    check(mhw::migrateLegacySettingsFile(legacySettings, currentSettings),
          "settings: legacy file migrates when current file is absent");
    {
        QSettings current(currentSettings, QSettings::IniFormat);
        check(current.value(QStringLiteral("game")).toString() == QStringLiteral("rise")
                  && current.value(QStringLiteral("ui/zoom")).toDouble() == 2.5
                  && current.value(QStringLiteral("ui/geometry")).toByteArray()
                         == QByteArray("opaque-geometry"),
              "settings: migration preserves nested keys and value types");
    }
    check(QFileInfo::exists(legacySettings),
          "settings: migration leaves the legacy file intact");
    {
        QSettings legacy(legacySettings, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("game"), QStringLiteral("world"));
        legacy.setValue(QStringLiteral("legacyOnly"), 42);
        legacy.sync();
        QSettings current(currentSettings, QSettings::IniFormat);
        current.setValue(QStringLiteral("game"), QStringLiteral("rise"));
        current.sync();
    }
    check(mhw::migrateLegacySettingsFile(legacySettings, currentSettings),
          "settings: existing current file makes migration a no-op");
    {
        QSettings current(currentSettings, QSettings::IniFormat);
        check(current.value(QStringLiteral("game")).toString() == QStringLiteral("rise")
                  && !current.contains(QStringLiteral("legacyOnly")),
              "settings: current config wins and existing keys are not overwritten");
    }

    // --------------------------------------------------------- string table
    auto &t = mhw::StringTable::instance();
    check(t.load(QStringLiteral("zh-CN")), "load zh-CN (directory of files)");
    check(t.currentLocale() == QStringLiteral("zh-CN"), "currentLocale zh-CN");
    check(!t.isEnglish(), "zh-CN is not english");
    check(t.tr(QStringLiteral("ui.player_title")) == QStringLiteral("猎人状态"),
          "zh ui.player_title value resolved from the table");
    check(t.tr(QStringLiteral("ui.zone_unknown_area")) == QStringLiteral("未知区域"),
          "zh ui.zone_unknown_area value resolved from the table");
    check(t.tr(QStringLiteral("no.such.key")) == QStringLiteral("no.such.key"),
          "missing key returns key");

    check(t.load(QStringLiteral("en-US")), "load en-US (stub dir loads)");
    check(t.currentLocale() == QStringLiteral("en-US"), "currentLocale en-US");
    check(t.isEnglish(), "en-US is english");

    check(!t.load(QStringLiteral("xx-XX")), "missing locale dir fails");
    check(t.currentLocale() == QStringLiteral("en-US"),
          "failed reload keeps the previous locale");
    check(t.tr(QStringLiteral("no.such.key")) == QStringLiteral("no.such.key"),
          "tr still serves keys after failed reload");

    check(t.load(QStringLiteral("zh-CN")), "reload back to zh-CN");
    check(!t.isEnglish(), "back to chinese");
    check(t.tr(QStringLiteral("ui.player_title")) == QStringLiteral("猎人状态"),
          "zh value intact after round-trip");

    if (failures == 0)
        std::printf("\nmonster-locale-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nmonster-locale-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
