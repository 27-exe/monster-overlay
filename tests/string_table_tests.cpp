// SPDX-License-Identifier: Apache-2.0
// Unit tests for the i18n StringTable.
//
// These don't touch /proc/<mhw pid>/mem, so they run cleanly under
// `ctest` without the game running.

#include "core/string_table.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

} // namespace

int main(int argc, char **argv)
{
    // StringTable uses QHash which needs a QCoreApplication for
    // thread-local data, but instance() is a Meyers singleton so we
    // can construct it without QCoreApplication if the JSON is the
    // in-memory path. Tests below use a synthetic JSON object directly
    // to avoid the QRC dependency.
    QCoreApplication app(argc, argv);

    // Direct API: missing key returns the key itself.
    auto &t = mhw::StringTable::instance();
    check(t.tr(QStringLiteral("nonexistent.key")) == QStringLiteral("nonexistent.key"),
          "missing key returns key unchanged");

    // currentLocale is empty before load() is called.
    check(t.currentLocale().isEmpty(),
          "currentLocale() empty before load()");

    // Load a synthetic file path: must fail (file doesn't exist).
    check(!t.load(QStringLiteral("does-not-exist-locale")),
          "load() returns false on missing file");

    // Verify tr() still returns key after a failed load.
    check(t.tr(QStringLiteral("anything")) == QStringLiteral("anything"),
          "tr() returns key after failed load()");

    // Locale after a failed load: empty (we don't update currentLocale
    // unless the load succeeded).
    check(t.currentLocale().isEmpty(),
          "currentLocale stays empty after failed load()");

    if (failures == 0)
        std::printf("\nmonster-core-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nmonster-core-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}