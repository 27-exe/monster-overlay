// Pure-logic tests for mhw::riseReframeworkStatusLines() — the REFramework
// status-machine derivation that used to live inline in
// ControlPanel::refreshRiseReframeworkStatus(). The derivation read a
// RiseReFrameworkManager::Status plus the console's operation flags and
// produced the card's readable lines; because it sat inside a method that
// also toggles button enabled-states, none of its six branches was
// reachable from a test.
//
// Every case drives the pure function and asserts a CONCRETE line (the
// resolved localization, not just non-emptiness), including the position of
// the trailing detail line. These are characterization tests of the moved
// code: the expected strings are the values the unmodified panel produced.
// Where the derivation's behaviour is surprising (see the Conflict case), the
// case says so explicitly rather than endorsing it.

#include "ui/viewmodel/rise_reframework_status.h"

#include "core/rise_reframework_manager.h"
#include "core/string_table.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int failures = 0;

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, \
                         #actual, #expected);                                   \
            return false;                                                      \
        }                                                                      \
    } while (false)

using mhw::RiseReframeworkStatusInput;
using mhw::RiseReFrameworkManager;
using CoreState = RiseReFrameworkManager::CoreState;
using LuaState = RiseReFrameworkManager::LuaState;
using ManifestState = RiseReFrameworkManager::ManifestState;

// The same StringTable the console resolves through, so an assertion on the
// resolved text pins the i18n key that was used, not just "some string".
QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}

// A status snapshot with everything valid/ready; individual cases override
// the one field the branch under test reacts to.
RiseReFrameworkManager::Status readyStatus()
{
    RiseReFrameworkManager::Status s;
    s.gameDirValid = true;
    s.core = CoreState::Managed;
    s.lua = LuaState::Current;
    s.manifest = ManifestState::Valid;
    s.detail = QStringLiteral("core managed; lua current; manifest valid");
    return s;
}

RiseReframeworkStatusInput readyInput()
{
    RiseReframeworkStatusInput in;
    in.gameDir = QStringLiteral("/home/hunter/.steam/steam/steamapps/common/MHRISE");
    in.status = readyStatus();
    return in;
}

// Whole-list equality. The branch under test must produce exactly these
// lines, in this order: a line added or dropped below is a visible change to
// what the card renders.
bool linesAre(const QStringList &actual, const QStringList &expected)
{
    if (actual.size() != expected.size()) {
        std::fprintf(stderr, "FAIL %s:%d: %d lines, expected %d\n",
                     __FILE__, __LINE__,
                     static_cast<int>(actual.size()),
                     static_cast<int>(expected.size()));
        std::fprintf(stderr, "  actual:\n");
        for (const QString &l : actual)
            std::fprintf(stderr, "    %s\n", qPrintable(l));
        std::fprintf(stderr, "  expected:\n");
        for (const QString &l : expected)
            std::fprintf(stderr, "    %s\n", qPrintable(l));
        return false;
    }
    for (int i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::fprintf(stderr, "FAIL %s:%d: line %d\n  actual:   %s\n"
                                 "  expected: %s\n",
                         __FILE__, __LINE__, i,
                         qPrintable(actual[i]), qPrintable(expected[i]));
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Branch 1 — Steam has no Rise install: single line, no path, no state line.
// ---------------------------------------------------------------------------
bool noRiseInstallReportedWhenTheLocatorHasNoGameDir()
{
    RiseReframeworkStatusInput in;
    in.gameDir.clear();
    in.status = RiseReFrameworkManager::Status{};   // all defaults: invalid
    in.status.detail = QStringLiteral("no game directory");

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.not_found")),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(QStringLiteral("no game directory")),
    });
}

// ---------------------------------------------------------------------------
// Branch 2 — a locator result that the manager rejects as invalid.
// ---------------------------------------------------------------------------
bool invalidGameDirIsReportedWithThePath()
{
    RiseReframeworkStatusInput in;
    in.gameDir = QStringLiteral("/mnt/games/MHRISE");
    in.status.gameDirValid = false;
    in.status.core = CoreState::Absent;
    in.status.detail = QStringLiteral("missing MonsterHunterRise.exe");

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_invalid"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(QStringLiteral("missing MonsterHunterRise.exe")),
    });
}

// ---------------------------------------------------------------------------
// Branch 3 — valid game directory, no REFramework core at all.
// An Absent core and a Conflict core are BOTH "not present" here.
// ---------------------------------------------------------------------------
bool coreMissingWhenNoCoreIsPresent()
{
    for (CoreState core : {CoreState::Absent, CoreState::Conflict}) {
        RiseReframeworkStatusInput in = readyInput();
        in.status.core = core;
        in.status.lua = LuaState::Missing;
        in.status.manifest = ManifestState::Missing;
        in.status.detail.clear();

        const QStringList lines = mhw::riseReframeworkStatusLines(in);
        if (!linesAre(lines, QStringList{
                tr(QStringLiteral("console.reframework.game_found"))
                    .arg(in.gameDir),
                tr(QStringLiteral("console.reframework.state_core_missing")),
            }))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Branch 4 — valid directory + core, Lua gone: overlay damage unavailable.
// ---------------------------------------------------------------------------
bool luaMissingWhenTheOverlayLuaIsAbsent()
{
    RiseReframeworkStatusInput in = readyInput();
    in.status.lua = LuaState::Missing;

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_lua_missing")),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(in.status.detail),
    });
}

// ---------------------------------------------------------------------------
// Branch 5 — everything in place: the only state line is "ready".
// ---------------------------------------------------------------------------
bool readyWhenCoreLuaAndManifestAreAllUsable()
{
    RiseReframeworkStatusInput in = readyInput();

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_ready")),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(in.status.detail),
    });
}

// ---------------------------------------------------------------------------
// Branch 6 — the repair/needs-repair path pins every trigger the derivation
// recognizes: an invalid manifest OR a user-modified project Lua. Both must
// collapse onto the same "install incomplete" line, and neither may fall
// through to the lua-missing or ready branch.
// ---------------------------------------------------------------------------
bool needsRepairWhenManifestIsInvalidOrLuaIsModified()
{
    {
        RiseReframeworkStatusInput in = readyInput();
        in.status.manifest = ManifestState::Invalid;
        const QStringList lines = mhw::riseReframeworkStatusLines(in);
        if (!linesAre(lines, QStringList{
                tr(QStringLiteral("console.reframework.game_found"))
                    .arg(in.gameDir),
                tr(QStringLiteral("console.reframework.state_needs_repair")),
                tr(QStringLiteral("console.reframework.detail_raw"))
                    .arg(in.status.detail),
            }))
            return false;
    }
    {
        RiseReframeworkStatusInput in = readyInput();
        in.status.lua = LuaState::Modified;
        const QStringList lines = mhw::riseReframeworkStatusLines(in);
        if (!linesAre(lines, QStringList{
                tr(QStringLiteral("console.reframework.game_found"))
                    .arg(in.gameDir),
                tr(QStringLiteral("console.reframework.state_needs_repair")),
                tr(QStringLiteral("console.reframework.detail_raw"))
                    .arg(in.status.detail),
            }))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The three trailing flags are independent of the state line and keep the
// order the original code appended them in: game_running, pending, then the
// worker result, then the raw detail LAST.
// ---------------------------------------------------------------------------
bool trailingFlagsKeepTheirOrder()
{
    RiseReframeworkStatusInput in = readyInput();
    in.gameRunning = true;
    in.operationPending = true;
    in.hasResult = true;
    in.resultOk = false;
    in.resultDetail = QStringLiteral("installer exited 3");

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_ready")),
        tr(QStringLiteral("console.reframework.game_running")),
        tr(QStringLiteral("console.reframework.pending")),
        tr(QStringLiteral("console.reframework.failure"))
            .arg(QStringLiteral("installer exited 3")),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(in.status.detail),
    });
}

// A successful worker run reports success with its detail; nothing else
// about the state lines changes.
bool successfulResultIsReportedWithItsDetail()
{
    RiseReframeworkStatusInput in = readyInput();
    in.hasResult = true;
    in.resultOk = true;
    in.resultDetail = QStringLiteral("lua installed");

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_ready")),
        tr(QStringLiteral("console.reframework.success"))
            .arg(QStringLiteral("lua installed")),
        tr(QStringLiteral("console.reframework.detail_raw"))
            .arg(in.status.detail),
    });
}

// The slot exists to be appended only when it adds something; an empty
// status.detail must not produce an empty trailing line.
bool emptyDetailAppendsNothing()
{
    RiseReframeworkStatusInput in = readyInput();
    in.status.detail.clear();

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_ready")),
    });
}

// An EXTERNAL (foreign) core still counts as present, so the card reports a
// real state line rather than "not installed". Characterization test: the
// console's own button gates treat External like Managed, and this presumes
// the same reading of the status.
bool externalCoreCountsAsPresent()
{
    RiseReframeworkStatusInput in = readyInput();
    in.status.core = CoreState::External;
    in.status.detail.clear();

    const QStringList lines = mhw::riseReframeworkStatusLines(in);
    return linesAre(lines, QStringList{
        tr(QStringLiteral("console.reframework.game_found"))
            .arg(in.gameDir),
        tr(QStringLiteral("console.reframework.state_ready")),
    });
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The lines under test are localized; load a real locale so the
    // assertions compare against translated text rather than the
    // key-fallback path (which would make every case trivially fail).
    if (!mhw::StringTable::instance().load(QStringLiteral("zh-CN"))) {
        std::fprintf(stderr,
                     "FATAL: zh-CN locale failed to load; cannot assert on "
                     "resolved status lines\n");
        return 2;
    }

    struct { const char *name; bool (*fn)(); } cases[] = {
        {"no game dir -> not_found (+ detail line last)", noRiseInstallReportedWhenTheLocatorHasNoGameDir},
        {"invalid game dir -> game_invalid with the path", invalidGameDirIsReportedWithThePath},
        {"no core (Absent / Conflict) -> state_core_missing", coreMissingWhenNoCoreIsPresent},
        {"core present, lua missing -> state_lua_missing",  luaMissingWhenTheOverlayLuaIsAbsent},
        {"all ready -> state_ready",                         readyWhenCoreLuaAndManifestAreAllUsable},
        {"invalid manifest / modified lua -> needs_repair",  needsRepairWhenManifestIsInvalidOrLuaIsModified},
        {"trailing flags keep game_running/pending/result order", trailingFlagsKeepTheirOrder},
        {"successful worker result -> success line",         successfulResultIsReportedWithItsDetail},
        {"empty detail appends no line",                     emptyDetailAppendsNothing},
        {"external core counts as present",                  externalCoreCountsAsPresent},
    };

    for (const auto &c : cases) {
        const bool ok = c.fn();
        std::fprintf(ok ? stdout : stderr, "%s: %s\n",
                     ok ? "PASS" : "FAIL", c.name);
        if (!ok)
            ++failures;
    }
    if (failures > 0) {
        std::fprintf(stderr, "%d of %zu cases FAILED\n",
                     failures, sizeof(cases) / sizeof(cases[0]));
        return 1;
    }
    std::fprintf(stdout, "all %zu cases passed\n",
                 sizeof(cases) / sizeof(cases[0]));
    return 0;
}
