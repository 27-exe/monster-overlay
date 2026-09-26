#include "ui/viewmodel/rise_reframework_status.h"

// Rise console card — REFramework status-line derivation.
//
// The body below is the block MOVED verbatim out of
// ControlPanel::refreshRiseReframeworkStatus() (control_panel.cpp, HEAD
// 91901e3). What changed is only the plumbing: the two private reads it did
// (`status`, `riseGameDir_`) became fields of RiseReframeworkStatusInput, and
// `gameRunning` / the console's operation flags became the remaining input
// fields. Every condition, every i18n key and the append order are the
// original — the derivation is preserved by construction rather than by
// re-derivation.
//
// Namespace note: control_panel.cpp kept `mh::tr()` in a namespace above its
// anonymous one so buildRiseStatusLines could resolve it there; the builder
// now lives in its own translation unit, so the same alias is declared again
// below (inline → ODR-safe, exactly as panel_monster.cpp does).

#include "core/string_table.h"

#include <QString>
#include <QStringList>

namespace {

// Same definition as control_panel.cpp's `mh::tr`, kept local because this
// translation unit is the builder that fills the localized status lines.
inline QString trVm(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}

} // namespace

namespace mhw {

QStringList riseReframeworkStatusLines(const RiseReframeworkStatusInput &input)
{
    // Build the status as readable lines rather than appending whatever the
    // installer happened to put in `detail`. `status.detail` is a technical
    // summary meant for diagnostics ("core managed; Lua current; manifest
    // valid"); showing it raw next to the path made the card read like a log.
    // Derive a plain-language state from the fields, and keep the raw detail
    // as a clearly-labelled secondary line.
    QStringList lines;
    const mhw::RiseReFrameworkManager::Status status = input.status;
    const bool corePresent = status.core == mhw::RiseReFrameworkManager::CoreState::Managed
                             || status.core == mhw::RiseReFrameworkManager::CoreState::External;
    if (input.gameDir.isEmpty()) {
        lines.append(trVm(QStringLiteral("console.reframework.not_found")));
    } else if (!status.gameDirValid) {
        lines.append(trVm(QStringLiteral("console.reframework.game_invalid"))
                         .arg(input.gameDir));
    } else if (!corePresent) {
        lines.append(trVm(QStringLiteral("console.reframework.game_found"))
                         .arg(input.gameDir));
        lines.append(trVm(QStringLiteral("console.reframework.state_core_missing")));
    } else {
        lines.append(trVm(QStringLiteral("console.reframework.game_found"))
                         .arg(input.gameDir));
        const bool manifestBroken =
            status.manifest == mhw::RiseReFrameworkManager::ManifestState::Invalid;
        const bool installIncomplete =
            manifestBroken
            || status.lua == mhw::RiseReFrameworkManager::LuaState::Modified;
        if (installIncomplete) {
            lines.append(trVm(QStringLiteral(
                "console.reframework.state_needs_repair")));
        } else if (status.lua == mhw::RiseReFrameworkManager::LuaState::Missing) {
            lines.append(trVm(QStringLiteral("console.reframework.state_lua_missing")));
        } else {
            lines.append(trVm(QStringLiteral("console.reframework.state_ready")));
        }
    }
    if (input.gameRunning)
        lines.append(trVm(QStringLiteral("console.reframework.game_running")));
    if (input.operationPending)
        lines.append(trVm(QStringLiteral("console.reframework.pending")));
    if (input.hasResult) {
        lines.append(trVm(input.resultOk
                                ? QStringLiteral("console.reframework.success")
                                : QStringLiteral("console.reframework.failure"))
                         .arg(input.resultDetail));
    }
    // The raw diagnostic string goes last and only when it adds something the
    // state lines above do not already say.
    if (!status.detail.isEmpty())
        lines.append(trVm(QStringLiteral("console.reframework.detail_raw"))
                         .arg(status.detail));
    return lines;
}

} // namespace mhw
