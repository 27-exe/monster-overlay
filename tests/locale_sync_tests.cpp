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
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("zh-CN")).isEmpty(),
          "same change not reported twice");

    QThread::msleep(kMtimeTickMs);
    check(mhw::writeLocaleToConf(QStringLiteral("zh-CN"), conf2),
          "console flips back to zh-CN");
    check(mhw::localeSyncPoll(st, conf2, QStringLiteral("en-US"))
              == QStringLiteral("zh-CN"),
          "poll reports zh-CN after an EN session");

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

    // --------------------------------------------------------- string table
    auto &t = mhw::StringTable::instance();
    check(t.load(QStringLiteral("zh-CN")), "load zh-CN (directory of files)");
    check(t.currentLocale() == QStringLiteral("zh-CN"), "currentLocale zh-CN");
    check(!t.isEnglish(), "zh-CN is not english");
    check(t.tr(QStringLiteral("ui.app_title")) == QStringLiteral("MHW Linux Overlay"),
          "zh ui.app_title value preserved");
    check(t.tr(QStringLiteral("ui.context_hunting")) == QStringLiteral("狩猎 · %1"),
          "zh context_hunting value preserved");
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
    check(t.tr(QStringLiteral("ui.app_title")) == QStringLiteral("MHW Linux Overlay"),
          "zh value intact after round-trip");

    if (failures == 0)
        std::printf("\nmonster-locale-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nmonster-locale-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
