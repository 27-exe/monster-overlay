#include "icon.h"

#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QPainter>
#include <QResource>
#include <QSet>
#include <QSvgRenderer>

namespace {

QHash<QString, QByteArray> &cache()
{
    static QHash<QString, QByteArray> c;
    return c;
}

bool isPng(const QByteArray &data)
{
    // PNG magic: 89 50 4E 47 0D 0A 1A 0A
    return data.size() >= 8
        && static_cast<unsigned char>(data[0]) == 0x89
        && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

} // namespace

namespace mhw {

bool Icon::load(const QString &path)
{
    if (cache().contains(path))
        return true;
    // QFile::open on a Qt resource path can fail with OpenError on
    // Qt 6.11 (Wayland); fall back to QResource::data() which goes
    // through the resource engine directly.  Both still hit the same
    // underlying qrc payload.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        cache()[path] = f.readAll();
        return true;
    }
    QResource res(path);
    if (res.isValid()) {
        const auto *raw = res.data();
        if (raw && res.size() > 0)
            cache()[path] = QByteArray(reinterpret_cast<const char *>(raw),
                                       static_cast<int>(res.size()));
    }
    return !cache()[path].isEmpty();
}

QPixmap Icon::renderRect(const QString &path, const QSize &target,
                         const QColor &tint)
{
    if (target.width() <= 0 || target.height() <= 0 || !load(path))
        return {};
    const QByteArray &data = cache()[path];
    QPixmap pix;
    if (isPng(data)) {
        const QImage source = QImage::fromData(data, "PNG");
        if (source.isNull())
            return {};
        // CSS object-fit: cover: scale until both dimensions cover the
        // destination, then crop the centered excess. This preserves the
        // original PNG colors and alpha without the square render() path.
        const QImage scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
        const QRect crop((scaled.width() - target.width()) / 2,
                         (scaled.height() - target.height()) / 2,
                         target.width(), target.height());
        pix = QPixmap::fromImage(scaled.copy(crop));
    } else {
        QSvgRenderer renderer(data);
        if (!renderer.isValid())
            return {};
        pix = QPixmap(target);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    }
    if (tint.isValid()) {
        QPainter painter(&pix);
        painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        painter.fillRect(pix.rect(), tint);
    }
    return pix;
}

QPixmap Icon::render(const QString &path, int size, const QColor &tint)
{
    load(path);
    const QByteArray &data = cache()[path];
    if (data.isEmpty())
        return {};

    QPixmap pix;
    if (isPng(data)) {
        QImage image = QImage::fromData(data, "PNG");
        if (image.isNull())
            return {};
        // Keep straight alpha while filtering. Scaling a premultiplied
        // PNG with transparent artwork can darken its RGB channels into
        // a silhouette; the monster portraits are especially sensitive
        // to that because most of the canvas is translucent.
        image = image.convertToFormat(QImage::Format_RGBA8888);
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pix = QPixmap::fromImage(image, Qt::NoFormatConversion);
    } else {
        // SVG path: render onto a transparent surface. The host panel
        // is WA_TranslucentBackground so the qpa layer composites
        // alpha properly — no white backdrop needed.
        QSvgRenderer renderer(data);
        if (!renderer.isValid())
            return {};
        pix = QPixmap(size, size);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p);
        p.end();
        if (pix.isNull())
            return {};
    }
    if (tint.isValid()) {
        // Tint: replace RGB while preserving the alpha mask.  Old code
        // drew the tint first (SourceIn onto a transparent buffer,
        // which yields full transparency) and then tried to re-introduce
        // the SVG alpha — the two modes cancelled each other out and
        // produced an invisible result.
        QPainter tp(&pix);
        tp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        tp.fillRect(pix.rect(), tint);
        tp.end();
        return pix;
    }
    return pix;
}

QString Icon::weaponPath(int weaponId, int rank)
{
    // HunterPie Core/Game/Enums/Weapon.cs ordering (HunterPie V2).
    static const char *kDirs[] = {
        "Great_Sword",       //  0 Greatsword
        "Sword_&_Shield",    //  1 SwordAndShield
        "Dual_Blades",       //  2 DualBlades
        "Long_Sword",        //  3 Longsword
        "Hammer",            //  4 Hammer
        "Hunting_Horn",      //  5 HuntingHorn
        "Lance",             //  6 Lance
        "Gunlance",          //  7 GunLance
        "Switch_Axe",        //  8 SwitchAxe
        "Charge_Blade",      //  9 ChargeBlade
        "Insect_Glaive",     // 10 InsectGlaive
        "Bow",               // 11 Bow
        "Heavy_Bowgun",      // 12 HeavyBowgun
        "Light_Bowgun",      // 13 LightBowgun
    };
    if (weaponId < 0 || weaponId > 13)
        return {};
    const int r = std::clamp(rank, 1, 12);
    // File naming in qrc: <Weapon>_Rank_<NN>.svg  (uppercase 'Rank').
    return QStringLiteral(":/icons/Weapons/%1/%2_Rank_%3.svg")
        .arg(QString::fromLatin1(kDirs[weaponId]))
        .arg(QString::fromLatin1(kDirs[weaponId]))
        .arg(r, 2, 10, QLatin1Char('0'));
}

QString Icon::mantlePath(int toolType)
{
    // SpecializedToolType enum (0-21) → in-game item ID → SVG file.
    // Missing entries (no SVG asset) return empty → caller skips icon.
    static const int kToolToItem[] = {
        118, //  0 GhillieMantle      隐身衣装
        119, //  1 TemporalMantle     转身衣装
        120, //  2 HealthBooster      治愈烟筒 (Tools/)
        121, //  3 RocksteadyMantle   不动衣装
        122, //  4 ChallengerMantle   挑衅衣装
        123, //  5 VitalityMantle     体力衣装
        124, //  6 FireproofMantle    耐火衣装
        125, //  7 WaterproofMantle   耐水衣装
        126, //  8 IceproofMantle     耐冰衣装
        127, //  9 ThunderproofMantle 耐雷衣装
        128, // 10 DragonproofMantle  耐龙衣装
        129, // 11 CleanserBooster    净化烟筒 (Tools/)
        130, // 12 GliderMantle       滑空衣装
        131, // 13 EvasionMantle      回避衣装
        132, // 14 ImpactMantle       冲击衣装
        133, // 15 ApothecaryMantle   疗愈衣装
        134, // 16 ImmunityMantle     免疫衣装
        135, // 17 AffinityBooster    达人烟筒 (Tools/)
        136, // 18 BanditMantle       强盗衣装
        999, // 19 AssassinsHood      暗杀者头巾
        -1,  // 20 MendingMantle      (no SVG)
        -1,  // 21 CorruptedMantle    (no SVG)
    };
    if (toolType < 0 || toolType >= 22)
        return {};
    const int itemId = kToolToItem[toolType];
    if (itemId < 0)
        return {};
    // Boosters (烟筒) live under Tools/, mantles (衣装) under Mantles/.
    const char *dir = (itemId == 120 || itemId == 129 || itemId == 135) ? "Tools" : "Mantles";
    return QStringLiteral(":/icons/%1/item_id_%2.svg").arg(QLatin1String(dir)).arg(itemId);
}

QString Icon::monsterPath(int monsterId, GameId game)
{
    if (game == GameId::Rise) {
        static const QSet<int> kRiseKnown = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
            79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98,
            107, 108, 109, 110, 111, 112, 113, 114, 115
        };
        if (kRiseKnown.contains(monsterId))
            return QStringLiteral(":/icons/Monsters/Rise_%1.png").arg(monsterId, 2, 10, QLatin1Char('0'));
        return QStringLiteral(":/icons/Monsters/Unknown.png");
    }
    // ID 91 (金狮子) shares icon with ID 92
    if (monsterId == 91) monsterId = 92;
    static const QSet<int> kKnown = {
        0, 1, 4, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 51,
        61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
        75, 76, 77, 78, 79, 80, 81,
        87, 88, 89, 90, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101
    };
    if (kKnown.contains(monsterId))
        return QStringLiteral(":/icons/Monsters/World_%1.png").arg(monsterId, 2, 10, QLatin1Char('0'));
    return QStringLiteral(":/icons/Monsters/Unknown.png");
}

} // namespace mhw