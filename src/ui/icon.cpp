#include "icon.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace {

QHash<QString, QByteArray> &cache()
{
    static QHash<QString, QByteArray> c;
    return c;
}

} // namespace

namespace mhw {

bool Icon::load(const QString &path)
{
    if (cache().contains(path))
        return true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    cache()[path] = f.readAll();
    return true;
}

QPixmap Icon::render(const QString &path, int size, const QColor &tint)
{
    load(path);
    const QByteArray &data = cache()[path];
    if (data.isEmpty())
        return {};

    QSvgRenderer renderer(data);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p);
    p.end();

    if (tint.isValid()) {
        QPixmap tinted(size, size);
        tinted.fill(Qt::transparent);
        QPainter tp(&tinted);
        tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tp.fillRect(tinted.rect(), tint);
        tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        tp.drawPixmap(0, 0, pix);
        tp.end();
        return tinted;
    }
    return pix;
}

QString Icon::weaponPath(int weaponId, int rank)
{
    // HunterPie SpecializedToolType / weapon enum → directory name.
    static const char *kDirs[] = {
        "Great_Sword", "Long_Sword", "Sword_&_Shield", "Dual_Blades",
        "Hammer", "Hunting_Horn", "Lance", "Gunlance",
        "Switch_Axe", "Charge_Blade", "Insect_Glaive",
        "Bow", "Light_Bowgun", "Heavy_Bowgun",
    };
    if (weaponId < 0 || weaponId > 13)
        return {};
    const int r = std::clamp(rank, 1, 12);
    return QStringLiteral(":/icons/Weapons/%1/%2_Rank_%3.svg")
        .arg(QString::fromLatin1(kDirs[weaponId]))
        .arg(QString::fromLatin1(kDirs[weaponId]))
        .arg(r, 2, 10, QLatin1Char('0'));
}

QString Icon::mantlePath(int itemId)
{
    return QStringLiteral(":/icons/Mantles/item_id_%1.svg").arg(itemId);
}

} // namespace mhw