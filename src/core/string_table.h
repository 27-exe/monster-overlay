#pragma once

#include <QHash>
#include <QString>

namespace mhw {

// Locale-aware string lookup. Loads JSON files from Qt resources at
// startup, replaces all hardcoded UI literals with `mh::tr("ui.xyz")`
// calls in the overlay layer.
//
// Schema:
//   mh::tr("ui.buildup") -> "怒气 %1%"
//   mh::tr("ui.zone.astera") -> "阿斯特拉"
//
// Lookup is a single-shot QHash lookup; tr() is safe to call from hot
// paths (overlay render runs at 1-4 Hz, so even 1k calls/sec is fine).
//
// Resolution order: requested locale -> key string fallback.
// Missing keys fall back to the key string itself so missing entries
// are visible during development rather than silently rendering blank.
//
// Since v0.9 each locale is a DIRECTORY of domain files under the qrc:
//   :/i18n/<locale>/overlay.json    overlay panel chrome strings
//   :/i18n/<locale>/console.json    control-console strings
// load() merges every *.json found in the directory into one flat table,
// so domains can be edited independently; later files win on a key clash
// (namespaces are disjoint in practice). Adding a new domain later only
// means dropping another <domain>.json into each locale directory.
//
// Runtime language switching: load() may be called again at any time with
// a different locale; the table is swapped in place. Callers that cached
// strings (widget labels, window titles) must re-query after a reload —
// the console does this through ControlPanel::retranslateUi(), the
// overlay through its conf-watch loop (see src/core/locale_sync.h).
class StringTable {
public:
    static StringTable& instance();

    // Load translations from Qt resources. Returns true on success;
    // false when the locale directory is missing or contains no readable
    // file. On failure the PREVIOUS table stays in place (empty before the
    // first successful load), and currentLocale() is not updated.
    bool load(const QString& locale = QStringLiteral("zh-CN"));

    // Locale code currently active (e.g. "zh-CN", "en-US"). Empty
    // before the first successful load().
    [[nodiscard]] QString currentLocale() const { return currentLocale_; }

    // True when the active locale is one of the English variants.
    // This is the single switch used by the data-name tables (monsters /
    // parts / zones / abnormalities) to pick their English column. An
    // empty locale (before load) reads as false -> Chinese default, which
    // keeps the test binaries' behaviour identical to the pre-i18n code.
    [[nodiscard]] bool isEnglish() const { return currentLocale_.startsWith(QStringLiteral("en")); }

    // Look up `key`. Missing key returns `key` unchanged so the UI
    // shows the dot-path rather than blank.
    [[nodiscard]] QString tr(const QString& key) const;

private:
    StringTable() = default;

    QHash<QString, QString> entries_;
    QString currentLocale_;
};

// Returns the localized mantle name from the overlay JSON.
// Falls back to "Mantle #<id>" if the key is missing.
inline QString mantleName(int id)
{
    const QString key = QStringLiteral("mantle.%1").arg(id);
    const QString val = StringTable::instance().tr(key);
    if (val != key)
        return val;
    return QStringLiteral("Mantle #%1").arg(id);
}

} // namespace mhw
