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
class StringTable {
public:
    static StringTable& instance();

    // Load translations from Qt resources. Called once at startup.
    // Returns true on success; false on missing/malformed file. The
    // singleton continues serving empty entries (tr() returns keys).
    bool load(const QString& locale = QStringLiteral("zh-CN"));

    // Locale code currently active (e.g. "zh-CN", "en-US"). Empty
    // before load() is called.
    [[nodiscard]] QString currentLocale() const { return currentLocale_; }

    // Look up `key`. Missing key returns `key` unchanged so the UI
    // shows the dot-path rather than blank.
    [[nodiscard]] QString tr(const QString& key) const;

private:
    StringTable() = default;

    QHash<QString, QString> entries_;
    QString currentLocale_;
};

// Returns the localized mantle name from zh-CN.json.
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