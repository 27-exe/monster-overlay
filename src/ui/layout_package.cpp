// SPDX-License-Identifier: Apache-2.0
//
// LayoutPackage — see the header for the format and the reasoning. This file
// is the two halves (parse / serialize) plus the registry helpers they share.
//
// THE SHAPE OF THE ERROR MESSAGES. Every refusal names the path it refused
// (`contains.scopes.world/mixed[2]`), because an import that says
// "invalid file" sends the user hunting through a document they may not have
// written. The messages are English literals: the console's copy layer owns
// user-facing sentences, and a codec error is a diagnostic, not UI copy.

#include "ui/layout_package.h"

#include "ui/console_fidelity_windows.h"
#include "ui/window_layout_plan.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>
#include <QStandardPaths>

#include <cmath>

namespace mhw {
namespace {

// The four mother panels, registry order. `dps` is the damage panel's
// settingsKey and `pet-damage` the pets one — the names panels.ini uses,
// not the names the rail shows.
QStringList motherPanels()
{
    return {QStringLiteral("player"), QStringLiteral("monster"),
            QStringLiteral("dps"), QStringLiteral("pet-damage")};
}

// The eleven windows the split tables carve out of three of the mothers.
// Read from window_layout_plan.h so a row added upstream lands here too
// without a second list to remember. The pets panel has no split table.
QMap<QString, QStringList> subWindowsByParent()
{
    QMap<QString, QStringList> out;
    for (const WindowLayoutRow &row : kWindowLayoutRows) {
        const QString parent =
            row.parentSlot == kLayoutParentPlayer ? QStringLiteral("player")
            : row.parentSlot == kLayoutParentMonster
                ? QStringLiteral("monster")
                : QStringLiteral("dps");
        out[parent].append(QString::fromLatin1(row.settingsKey));
    }
    return out;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// A value must be exactly the type the field says it is. Qt's toInt() and
// friends are coercing: they turn `true` into 1 and `"12"` into 12, which
// would let a hand-edited file smuggle the wrong shape past an import and
// land a number the user never typed.
bool isIntLike(const QJsonValue &v) { return v.isDouble(); }

bool parseInt(const QJsonValue &v, const QString &path,
              std::optional<int> &out, QString *error)
{
    if (!isIntLike(v)) {
        setError(error, QStringLiteral("%1: expected a number").arg(path));
        return false;
    }
    out = v.toInt();
    return true;
}

bool parseDouble(const QJsonValue &v, const QString &path,
                 std::optional<double> &out, QString *error)
{
    if (!isIntLike(v)) {
        setError(error, QStringLiteral("%1: expected a number").arg(path));
        return false;
    }
    out = v.toDouble();
    return true;
}

// Serializing: round the float noise an INI round-trip leaves behind
// (scale=0.7000000000000001) so the file is readable and diffable.
double round4(double v)
{
    return std::round(v * 10000.0) / 10000.0;
}

}  // namespace

// ─── registry helpers ────────────────────────────────────────────────────

QStringList layoutPackagePanelsFor(bool rise)
{
    QStringList out = motherPanels();
    if (!rise)
        out.removeAll(QStringLiteral("pet-damage"));
    return out;
}

QStringList layoutPackagePanelOrder(bool rise)
{
    return layoutPackagePanelsFor(rise);
}

QStringList layoutPackageWindowsFor(const QString &panel, const QString &mode)
{
    QStringList out{panel};
    if (mode != QStringLiteral("single"))
        out += subWindowsByParent().value(panel);
    return out;
}

bool layoutPackageScopeValid(const QString &scope, bool *rise, int *mode)
{
    const int slash = scope.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == scope.size() - 1)
        return false;
    const QString game = scope.left(slash);
    const QString layout = scope.mid(slash + 1);
    if (game != QStringLiteral("world") && game != QStringLiteral("rise"))
        return false;
    if (layout != QStringLiteral("single")
        && layout != QStringLiteral("split")
        && layout != QStringLiteral("mixed"))
        return false;
    if (rise)
        *rise = game == QStringLiteral("rise");
    if (mode)
        *mode = layout == QStringLiteral("single") ? 0
               : layout == QStringLiteral("split") ? 1 : 2;
    return true;
}

// ─── row / global ────────────────────────────────────────────────────────

bool LayoutWindowRow::isEmpty() const
{
    return !ml && !mt && !mr && !mb && !minW && !minH && !lockW && !lockH
           && !bgAlpha && !columns && !scale && !opacity && !outputName
           && !visible && extra.isEmpty();
}

bool LayoutWindowRow::operator==(const LayoutWindowRow &other) const
{
    return ml == other.ml && mt == other.mt && mr == other.mr
           && mb == other.mb && minW == other.minW && minH == other.minH
           && lockW == other.lockW && lockH == other.lockH
           && bgAlpha == other.bgAlpha && columns == other.columns
           && scale == other.scale && opacity == other.opacity
           && outputName == other.outputName && visible == other.visible
           && extra == other.extra;
}

bool LayoutGlobalBlock::isEmpty() const
{
    return !maskPlayer && !maskMonster && !maskDamage && !maskPets
           && !locale && !layout && plans.isEmpty() && extra.isEmpty();
}

bool LayoutGlobalBlock::operator==(const LayoutGlobalBlock &other) const
{
    return maskPlayer == other.maskPlayer && maskMonster == other.maskMonster
           && maskDamage == other.maskDamage && maskPets == other.maskPets
           && locale == other.locale && layout == other.layout
           && plans == other.plans && extra == other.extra;
}

// ─── parse ───────────────────────────────────────────────────────────────

namespace {

// A window row: known fields typed, everything else kept in `extra`.
bool parseWindowRow(const QJsonObject &obj, const QString &path,
                    LayoutWindowRow &out, QString *error)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = it.key();
        const QString here = path + QLatin1Char('.') + key;
        if (key == QLatin1String("ml")) {
            if (!parseInt(it.value(), here, out.ml, error)) return false;
        } else if (key == QLatin1String("mt")) {
            if (!parseInt(it.value(), here, out.mt, error)) return false;
        } else if (key == QLatin1String("mr")) {
            if (!parseInt(it.value(), here, out.mr, error)) return false;
        } else if (key == QLatin1String("mb")) {
            if (!parseInt(it.value(), here, out.mb, error)) return false;
        } else if (key == QLatin1String("minW")) {
            if (!parseInt(it.value(), here, out.minW, error)) return false;
        } else if (key == QLatin1String("minH")) {
            if (!parseInt(it.value(), here, out.minH, error)) return false;
        } else if (key == QLatin1String("lockW")) {
            if (!parseInt(it.value(), here, out.lockW, error)) return false;
        } else if (key == QLatin1String("lockH")) {
            if (!parseInt(it.value(), here, out.lockH, error)) return false;
        } else if (key == QLatin1String("bgAlpha")) {
            if (!parseInt(it.value(), here, out.bgAlpha, error)) return false;
        } else if (key == QLatin1String("columns")) {
            if (!parseInt(it.value(), here, out.columns, error)) return false;
        } else if (key == QLatin1String("scale")) {
            if (!parseDouble(it.value(), here, out.scale, error)) return false;
        } else if (key == QLatin1String("opacity")) {
            if (!parseDouble(it.value(), here, out.opacity, error))
                return false;
        } else if (key == QLatin1String("outputName")) {
            if (!it.value().isString()) {
                setError(error, here + QStringLiteral(": expected a string"));
                return false;
            }
            out.outputName = it.value().toString();
        } else if (key == QLatin1String("winVisible")) {
            if (!it.value().isBool()) {
                setError(error, here + QStringLiteral(": expected true/false"));
                return false;
            }
            out.visible = it.value().toBool();
        } else {
            out.extra.insert(key, it.value());   // unknown: kept verbatim
        }
    }
    return true;
}

bool parseGlobalBlock(const QJsonObject &obj, LayoutGlobalBlock &out,
                      QString *error)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = it.key();
        const QString here = QStringLiteral("global.") + key;
        // Exact names, not suffixes: `totalDamage` would otherwise be
        // swallowed as the damage mask and re-emitted in a shape the conf
        // reader would not parse.
        if (key == QLatin1String("player") || key == QLatin1String("monster")
            || key == QLatin1String("damage") || key == QLatin1String("pets")) {
            // The four masks are hex32 strings in the conf; accept the
            // string form only, so a number cannot slip in and be
            // re-emitted in a format the conf reader would not parse.
            if (!it.value().isString()) {
                setError(error, here + QStringLiteral(
                             ": expected a hex32 string"));
                return false;
            }
            bool ok = false;
            const uint32_t mask =
                it.value().toString().toUInt(&ok, 16);
            if (!ok) {
                setError(error, here + QStringLiteral(
                             ": not a hex32 value"));
                return false;
            }
            if (key == QLatin1String("player")) out.maskPlayer = mask;
            else if (key == QLatin1String("monster")) out.maskMonster = mask;
            else if (key == QLatin1String("damage")) out.maskDamage = mask;
            else out.maskPets = mask;
        } else if (key == QLatin1String("locale")
                   || key == QLatin1String("layout")) {
            if (!it.value().isString()) {
                setError(error, here + QStringLiteral(": expected a string"));
                return false;
            }
            (key == QLatin1String("locale") ? out.locale : out.layout) =
                it.value().toString();
        } else if (key == QLatin1String("plans")) {
            if (!it.value().isObject()) {
                setError(error, here + QStringLiteral(": expected an object"));
                return false;
            }
            const QJsonObject plans = it.value().toObject();
            for (auto p = plans.begin(); p != plans.end(); ++p) {
                if (p.key() != QLatin1String("world")
                    && p.key() != QLatin1String("rise")) {
                    setError(error, here + QLatin1Char('.') + p.key()
                                 + QStringLiteral(": unknown game"));
                    return false;
                }
                if (!p.value().isString()) {
                    setError(error, here + QLatin1Char('.') + p.key()
                                 + QStringLiteral(": expected a string"));
                    return false;
                }
                QStringList keys;
                for (const QString &k :
                     p.value().toString().split(QLatin1Char(','),
                                                Qt::SkipEmptyParts))
                    keys.append(k);
                out.plans.insert(p.key(), keys);
            }
        } else {
            out.extra.insert(key, it.value());   // unknown: kept verbatim
        }
    }
    return true;
}

}  // namespace

std::optional<LayoutPackage> LayoutPackage::parse(const QByteArray &json,
                                                 QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("not JSON: %1")
                            .arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        setError(error, QStringLiteral("the file is not a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    LayoutPackage pkg;

    // ── 2/3: kind and schema ──────────────────────────────────────────
    if (!root.value(QLatin1String("kind")).isString()
        || root.value(QLatin1String("kind")).toString() != kKind) {
        setError(error, QStringLiteral(
                     "kind is not \"%1\" — this is not a layout package")
                            .arg(kKind));
        return std::nullopt;
    }
    const QJsonValue schema = root.value(QLatin1String("schema"));
    if (!isIntLike(schema)) {
        setError(error, QStringLiteral("schema: missing or not a number"));
        return std::nullopt;
    }
    if (schema.toInt() > kSchema) {
        setError(error, QStringLiteral(
                     "schema %1 is newer than this build understands (%2)")
                            .arg(schema.toInt())
                            .arg(kSchema));
        return std::nullopt;
    }
    if (schema.toInt() < 1) {
        setError(error, QStringLiteral("schema %1: expected >= 1")
                            .arg(schema.toInt()));
        return std::nullopt;
    }

    // ── 4/5: the header is the contract ───────────────────────────────
    const QJsonValue containsValue = root.value(QLatin1String("contains"));
    if (!containsValue.isObject()) {
        setError(error, QStringLiteral("contains: missing or not an object"));
        return std::nullopt;
    }
    const QJsonObject contains = containsValue.toObject();
    const QJsonValue globalFlag = contains.value(QLatin1String("global"));
    if (!globalFlag.isBool()) {
        setError(error, QStringLiteral("contains.global: expected true/false"));
        return std::nullopt;
    }
    pkg.hasGlobal = globalFlag.toBool();
    const bool hasGlobalBlock =
        root.value(QLatin1String("global")).isObject();
    if (pkg.hasGlobal && !hasGlobalBlock) {
        setError(error, QStringLiteral(
                     "contains.global says the global block is present, but "
                     "there is no global object"));
        return std::nullopt;
    }
    if (!pkg.hasGlobal && hasGlobalBlock) {
        setError(error, QStringLiteral(
                     "contains.global says there is no global block, but a "
                     "global object is present"));
        return std::nullopt;
    }

    const QJsonValue scopesFlag = contains.value(QLatin1String("scopes"));
    if (!scopesFlag.isObject()) {
        setError(error,
                 QStringLiteral("contains.scopes: expected an object"));
        return std::nullopt;
    }
    const QJsonObject header = scopesFlag.toObject();

    // 8 (2 of 4): unknown keys inside `contains`, kept verbatim.
    for (auto it = contains.begin(); it != contains.end(); ++it)
        if (it.key() != QLatin1String("global")
            && it.key() != QLatin1String("scopes"))
            pkg.containsExtra.insert(it.key(), it.value());

    // ── the body ──────────────────────────────────────────────────────
    const QJsonValue scopesValue = root.value(QLatin1String("scopes"));
    if (!scopesValue.isObject()) {
        setError(error, QStringLiteral("scopes: missing or not an object"));
        return std::nullopt;
    }
    const QJsonObject body = scopesValue.toObject();

    // 13: header/body agreement, both directions.
    for (auto it = header.begin(); it != header.end(); ++it) {
        if (!body.contains(it.key())) {
            setError(error, QStringLiteral(
                         "contains.scopes.%1 is declared but scopes has no "
                         "such scope").arg(it.key()));
            return std::nullopt;
        }
    }
    for (auto it = body.begin(); it != body.end(); ++it) {
        if (!header.contains(it.key())) {
            setError(error, QStringLiteral(
                         "scopes.%1 exists but contains.scopes does not "
                         "declare it").arg(it.key()));
            return std::nullopt;
        }
    }

    for (auto it = header.begin(); it != header.end(); ++it) {
        const QString scope = it.key();
        // 6: scope grammar.
        bool rise = false;
        int mode = 0;
        if (!layoutPackageScopeValid(scope, &rise, &mode)) {
            setError(error, QStringLiteral(
                         "contains.scopes.%1: not <world|rise>/<single|"
                         "split|mixed>").arg(scope));
            return std::nullopt;
        }
        if (!it.value().isArray()) {
            setError(error, QStringLiteral(
                         "contains.scopes.%1: expected an array of panel "
                         "groups").arg(scope));
            return std::nullopt;
        }
        const QStringList allowedPanels = layoutPackagePanelsFor(rise);
        QStringList declared;
        const QJsonArray panels = it.value().toArray();
        for (const QJsonValue &p : panels) {
            if (!p.isString()) {
                setError(error, QStringLiteral(
                             "contains.scopes.%1: panel group must be a "
                             "string").arg(scope));
                return std::nullopt;
            }
            const QString panel = p.toString();
            if (!allowedPanels.contains(panel)) {
                setError(error, QStringLiteral(
                             "contains.scopes.%1: %2 is not a panel group of "
                             "this game").arg(scope, panel));
                return std::nullopt;
            }
            declared.append(panel);
        }

        if (!body.value(scope).isObject()) {
            setError(error, QStringLiteral(
                         "scopes.%1: expected an object").arg(scope));
            return std::nullopt;
        }
        const QJsonObject scopeBody = body.value(scope).toObject();

        // 7/12: every window belongs to a declared panel group, and every
        // declared group actually has windows in the body.
        QMap<QString, LayoutWindowRow> rows;
        QSet<QString> groupsWithRows;
        for (auto w = scopeBody.begin(); w != scopeBody.end(); ++w) {
            const QString key = w.key();
            const QString modeName = scope.mid(scope.indexOf(QLatin1Char('/'))
                                               + 1);
            QString owner;
            for (const QString &panel : allowedPanels) {
                if (layoutPackageWindowsFor(panel, modeName)
                        .contains(key)) {
                    owner = panel;
                    break;
                }
            }
            if (owner.isEmpty()) {
                setError(error, QStringLiteral(
                             "scopes.%1.%2: not a window of this mode")
                                .arg(scope, key));
                return std::nullopt;
            }
            if (!declared.contains(owner)) {
                setError(error, QStringLiteral(
                             "scopes.%1.%2 belongs to panel group %3, which "
                             "contains.scopes does not declare")
                                .arg(scope, key, owner));
                return std::nullopt;
            }
            if (!w.value().isObject()) {
                setError(error, QStringLiteral(
                             "scopes.%1.%2: expected an object")
                                .arg(scope, key));
                return std::nullopt;
            }
            LayoutWindowRow row;
            const QString path =
                QStringLiteral("scopes.%1.%2").arg(scope, key);
            if (!parseWindowRow(w.value().toObject(), path, row, error))
                return std::nullopt;
            rows.insert(key, row);
            groupsWithRows.insert(owner);
        }
        for (const QString &panel : declared) {
            if (!groupsWithRows.contains(panel)) {
                setError(error, QStringLiteral(
                             "contains.scopes.%1 declares panel group %2, "
                             "but scopes.%1 has none of its windows")
                                .arg(scope, panel));
                return std::nullopt;
            }
        }

        pkg.containsScopes.insert(scope, declared);
        pkg.scopes.insert(scope, rows);
    }

    if (pkg.hasGlobal) {
        if (!parseGlobalBlock(root.value(QLatin1String("global")).toObject(),
                              pkg.global, error))
            return std::nullopt;
    }

    // 8: unknown top-level keys, kept verbatim.
    static const QStringList kKnown = {QStringLiteral("kind"),
                                       QStringLiteral("schema"),
                                       QStringLiteral("contains"),
                                       QStringLiteral("global"),
                                       QStringLiteral("scopes")};
    for (auto it = root.begin(); it != root.end(); ++it)
        if (!kKnown.contains(it.key()))
            pkg.rootExtra.insert(it.key(), it.value());

    return pkg;
}

// ─── serialize ───────────────────────────────────────────────────────────

namespace {

QJsonObject windowRowToObject(const LayoutWindowRow &row)
{
    QJsonObject o = row.extra;   // unknown keys first, known ones win
    if (row.ml) o.insert(QStringLiteral("ml"), *row.ml);
    if (row.mt) o.insert(QStringLiteral("mt"), *row.mt);
    if (row.mr) o.insert(QStringLiteral("mr"), *row.mr);
    if (row.mb) o.insert(QStringLiteral("mb"), *row.mb);
    if (row.minW) o.insert(QStringLiteral("minW"), *row.minW);
    if (row.minH) o.insert(QStringLiteral("minH"), *row.minH);
    if (row.lockW) o.insert(QStringLiteral("lockW"), *row.lockW);
    if (row.lockH) o.insert(QStringLiteral("lockH"), *row.lockH);
    if (row.bgAlpha) o.insert(QStringLiteral("bgAlpha"), *row.bgAlpha);
    if (row.columns) o.insert(QStringLiteral("columns"), *row.columns);
    if (row.scale)
        o.insert(QStringLiteral("scale"), round4(*row.scale));
    if (row.opacity)
        o.insert(QStringLiteral("opacity"), round4(*row.opacity));
    if (row.outputName)
        o.insert(QStringLiteral("outputName"), *row.outputName);
    if (row.visible)
        o.insert(QStringLiteral("winVisible"), *row.visible);
    return o;
}

QJsonObject globalToObject(const LayoutGlobalBlock &g)
{
    QJsonObject o = g.extra;
    if (g.maskPlayer)
        o.insert(QStringLiteral("player"),
                 QString::number(*g.maskPlayer, 16));
    if (g.maskMonster)
        o.insert(QStringLiteral("monster"),
                 QString::number(*g.maskMonster, 16));
    if (g.maskDamage)
        o.insert(QStringLiteral("damage"),
                 QString::number(*g.maskDamage, 16));
    if (g.maskPets)
        o.insert(QStringLiteral("pets"),
                 QString::number(*g.maskPets, 16));
    if (g.locale) o.insert(QStringLiteral("locale"), *g.locale);
    if (g.layout) o.insert(QStringLiteral("layout"), *g.layout);
    if (!g.plans.isEmpty()) {
        QJsonObject plans;
        for (auto it = g.plans.begin(); it != g.plans.end(); ++it)
            plans.insert(it.key(), it.value().join(QLatin1Char(',')));
        o.insert(QStringLiteral("plans"), plans);
    }
    return o;
}

}  // namespace

QByteArray LayoutPackage::serialize() const
{
    // The header is DERIVED from the body, so the two can never drift in a
    // file this build writes. Parse still verifies both directions: a file
    // may come from elsewhere.
    QJsonObject header;
    for (auto it = scopes.begin(); it != scopes.end(); ++it) {
        QJsonArray panels;
        for (const QString &p : containsScopes.value(it.key()))
            panels.append(p);
        header.insert(it.key(), panels);
    }

    QJsonObject contains = containsExtra;   // unknown keys preserved
    contains.insert(QStringLiteral("global"), hasGlobal);
    contains.insert(QStringLiteral("scopes"), header);

    QJsonObject body;
    for (auto it = scopes.begin(); it != scopes.end(); ++it) {
        QJsonObject scopeBody;
        for (auto w = it.value().begin(); w != it.value().end(); ++w)
            scopeBody.insert(w.key(), windowRowToObject(w.value()));
        body.insert(it.key(), scopeBody);
    }

    QJsonObject root = rootExtra;   // unknown keys preserved
    root.insert(QStringLiteral("kind"), kKind);
    root.insert(QStringLiteral("schema"), kSchema);
    root.insert(QStringLiteral("contains"), contains);
    if (hasGlobal)
        root.insert(QStringLiteral("global"), globalToObject(global));
    root.insert(QStringLiteral("scopes"), body);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// ─── queries ─────────────────────────────────────────────────────────────

bool LayoutPackage::containsScope(const QString &scope) const
{
    return containsScopes.contains(scope);
}

QStringList LayoutPackage::scopeNames() const
{
    return containsScopes.keys();
}

bool LayoutPackage::containsPanel(const QString &scope,
                                  const QString &panel) const
{
    return containsScopes.value(scope).contains(panel);
}

QStringList LayoutPackage::panelsOf(const QString &scope) const
{
    return containsScopes.value(scope);
}

QStringList LayoutPackage::windowsOf(const QString &scope,
                                     const QString &panel) const
{
    const int slash = scope.indexOf(QLatin1Char('/'));
    const QString mode = slash < 0 ? QString() : scope.mid(slash + 1);
    QStringList out;
    for (const QString &key : layoutPackageWindowsFor(panel, mode))
        if (scopes.value(scope).contains(key))
            out.append(key);
    return out;
}

// ─── the disk half ───────────────────────────────────────────────────────

namespace {

// The one field whose on-disk name is not its package name. `winVisible`
// is written as windowVisibleSettingsKey() and its two states are
// "absent == shown" and "false == hidden" (see console_fidelity_windows),
// so the package must speak that polarity rather than invent a third.
QString visibleSettingsKey()
{
    return windowVisibleSettingsKey();
}

bool readIntRow(const QSettings &s, const QString &field,
                std::optional<int> &out)
{
    if (!s.contains(field))
        return false;
    bool ok = false;
    const int v = s.value(field).toInt(&ok);
    if (ok)
        out = v;
    return ok;
}

bool readDoubleRow(const QSettings &s, const QString &field,
                   std::optional<double> &out)
{
    if (!s.contains(field))
        return false;
    bool ok = false;
    const double v = s.value(field).toDouble(&ok);
    if (ok)
        out = v;
    return ok;
}

// Which panel group a window key belongs to, in a given mode. Empty when
// the key is not a window of that mode at all.
QString panelOfWindow(const QString &key, const QString &mode, bool rise)
{
    for (const QString &panel : layoutPackagePanelsFor(rise))
        if (layoutPackageWindowsFor(panel, mode).contains(key))
            return panel;
    return QString();
}

}  // namespace

std::optional<LayoutPackage> layoutPackageFromSettings(QSettings &settings,
                                                      QString *error)
{
    LayoutPackage pkg;
    for (const QString &full : settings.allKeys()) {
        const QStringList parts = full.split(QLatin1Char('/'));
        // "<game>/<mode>/<window>/<field>" — anything deeper is a group we
        // do not model, and anything shallower is not a window row.
        if (parts.size() != 4)
            continue;
        const QString scope =
            parts.at(0) + QLatin1Char('/') + parts.at(1);
        bool rise = false;
        int mode = 0;
        if (!layoutPackageScopeValid(scope, &rise, &mode))
            continue;
        const QString window = parts.at(2);
        const QString field = parts.at(3);
        const QString modeName = parts.at(1);
        if (panelOfWindow(window, modeName, rise).isEmpty())
            continue;   // a row this build does not model: not ours to own

        LayoutWindowRow &row = pkg.scopes[scope][window];
        settings.beginGroup(parts.at(0) + QLatin1Char('/') + parts.at(1)
                            + QLatin1Char('/') + window);
        if (field == QLatin1String("outputName")) {
            row.outputName = settings.value(field).toString();
        } else if (field == QLatin1String("winVisible")) {
            row.visible = settings.value(field).toBool();
        } else if (field == QLatin1String("scale")
                   || field == QLatin1String("opacity")) {
            if (!readDoubleRow(settings, field, field == QLatin1String("scale")
                                                     ? row.scale
                                                     : row.opacity)) {
                settings.endGroup();
                setError(error, QStringLiteral(
                             "%1: not a number").arg(full));
                return std::nullopt;
            }
        } else {
            static const QStringList kIntFields = {
                QStringLiteral("ml"), QStringLiteral("mt"),
                QStringLiteral("mr"), QStringLiteral("mb"),
                QStringLiteral("minW"), QStringLiteral("minH"),
                QStringLiteral("lockW"), QStringLiteral("lockH"),
                QStringLiteral("bgAlpha"), QStringLiteral("columns")};
            if (kIntFields.contains(field)) {
                std::optional<int> *dst =
                    field == QLatin1String("ml") ? &row.ml
                    : field == QLatin1String("mt") ? &row.mt
                    : field == QLatin1String("mr") ? &row.mr
                    : field == QLatin1String("mb") ? &row.mb
                    : field == QLatin1String("minW") ? &row.minW
                    : field == QLatin1String("minH") ? &row.minH
                    : field == QLatin1String("lockW") ? &row.lockW
                    : field == QLatin1String("lockH") ? &row.lockH
                    : field == QLatin1String("bgAlpha") ? &row.bgAlpha
                                                       : &row.columns;
                if (!readIntRow(settings, field, *dst)) {
                    settings.endGroup();
                    setError(error, QStringLiteral(
                                 "%1: not a number").arg(full));
                    return std::nullopt;
                }
            }
            // Any other field in the row is the store's own business and
            // is simply not carried into the package: a package is a
            // description of layout, not a dump of every knob a Panel has.
        }
        settings.endGroup();
    }

    // The header, derived from what was found: scope -> the panel groups
    // whose windows appear in it, in registry order.
    for (auto it = pkg.scopes.begin(); it != pkg.scopes.end(); ++it) {
        const QString scope = it.key();
        bool rise = false;
        int mode = 0;
        if (!layoutPackageScopeValid(scope, &rise, &mode))
            continue;   // a hand-built package may hold an odd scope name
        const QString modeName = scope.mid(scope.indexOf(QLatin1Char('/')) + 1);
        QStringList groups;
        for (const QString &panel : layoutPackagePanelsFor(rise))
            for (const QString &key : layoutPackageWindowsFor(panel, modeName))
                if (it.value().contains(key)) {
                    groups.append(panel);
                    break;
                }
        pkg.containsScopes.insert(scope, groups);
    }
    return pkg;
}

bool layoutPackageToSettings(const LayoutPackage &pkg, QSettings &settings,
                             QString *error)
{
    for (auto it = pkg.scopes.begin(); it != pkg.scopes.end(); ++it) {
        const QString scope = it.key();
        for (auto w = it.value().begin(); w != it.value().end(); ++w) {
            const mhw::LayoutWindowRow &row = w.value();
            settings.beginGroup(scope + QLatin1Char('/') + w.key());
            if (row.ml) settings.setValue(QStringLiteral("ml"), *row.ml);
            if (row.mt) settings.setValue(QStringLiteral("mt"), *row.mt);
            if (row.mr) settings.setValue(QStringLiteral("mr"), *row.mr);
            if (row.mb) settings.setValue(QStringLiteral("mb"), *row.mb);
            if (row.minW) settings.setValue(QStringLiteral("minW"), *row.minW);
            if (row.minH) settings.setValue(QStringLiteral("minH"), *row.minH);
            if (row.lockW)
                settings.setValue(QStringLiteral("lockW"), *row.lockW);
            if (row.lockH)
                settings.setValue(QStringLiteral("lockH"), *row.lockH);
            if (row.bgAlpha)
                settings.setValue(QStringLiteral("bgAlpha"), *row.bgAlpha);
            if (row.columns)
                settings.setValue(QStringLiteral("columns"), *row.columns);
            if (row.scale) settings.setValue(QStringLiteral("scale"), *row.scale);
            if (row.opacity)
                settings.setValue(QStringLiteral("opacity"), *row.opacity);
            if (row.outputName)
                settings.setValue(QStringLiteral("outputName"),
                                  *row.outputName);
            if (row.visible) {
                // The store's polarity, not the package's: absent means
                // shown, so a show REMOVES the row rather than writing
                // true (which is exactly what writeWindowVisible does).
                if (*row.visible)
                    settings.remove(visibleSettingsKey());
                else
                    settings.setValue(visibleSettingsKey(), false);
            }
            settings.endGroup();
        }
    }
    if (error)
        error->clear();
    return true;
}

QString layoutPackageStorePath()
{
    return QStandardPaths::writableLocation(
               QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/monster-overlay/panels.ini");
}

bool seedShippedDefaultIfFresh(QString *error)
{
    // THE CHECK COMES FIRST, and before any QSettings exists: constructing
    // one and syncing is enough to create an empty file, which would make
    // "already there" true for a machine that has never run this.
    const QString path = layoutPackageStorePath();
    if (QFile::exists(path)) {
        if (error)
            error->clear();
        return false;
    }

    QFile packaged(QStringLiteral(":/default-layout.json"));
    if (!packaged.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral(
                "the shipped default layout is not present in this build");
        return false;
    }
    const std::optional<LayoutPackage> pkg =
        LayoutPackage::parse(packaged.readAll(), error);
    if (!pkg)
        return false;

    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("monster-overlay"),
                       QStringLiteral("panels"));
    if (!layoutPackageToSettings(*pkg, settings, error))
        return false;
    settings.sync();
    if (error)
        error->clear();
    return true;
}

}  // namespace mhw
