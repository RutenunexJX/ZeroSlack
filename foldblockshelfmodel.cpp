#include "foldblockshelfmodel.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString originToString(FoldShelfOriginKind origin)
{
    return origin == FoldShelfOriginKind::Moved
        ? QStringLiteral("moved")
        : QStringLiteral("copied");
}

FoldShelfOriginKind originFromString(const QString& origin)
{
    return origin == QStringLiteral("moved")
        ? FoldShelfOriginKind::Moved
        : FoldShelfOriginKind::Copied;
}
}

FoldBlockShelfModel::FoldBlockShelfModel(QObject* parent)
    : QObject(parent)
{
}

QList<FoldShelfItem> FoldBlockShelfModel::items() const
{
    return shelfItems;
}

FoldShelfItem FoldBlockShelfModel::item(const QString& id) const
{
    for (const FoldShelfItem& item : shelfItems) {
        if (item.id == id)
            return item;
    }
    return {};
}

QString FoldBlockShelfModel::addItem(FoldShelfItem item)
{
    if (item.id.isEmpty())
        item.id = QStringLiteral("fold_shelf_%1").arg(nextId++);
    if (item.alias.isEmpty())
        item.alias = item.id;
    item.lineCount = item.text.count(QLatin1Char('\n'));
    if (!item.text.isEmpty() && !item.text.endsWith(QLatin1Char('\n')))
        item.lineCount += 1;
    shelfItems.append(item);
    emit changed();
    return item.id;
}

bool FoldBlockShelfModel::consumeItem(const QString& id)
{
    for (FoldShelfItem& item : shelfItems) {
        if (item.id != id)
            continue;
        item.consumed = true;
        emit changed();
        return true;
    }
    return false;
}

bool FoldBlockShelfModel::removeItem(const QString& id)
{
    for (int i = 0; i < shelfItems.size(); ++i) {
        if (shelfItems.at(i).id != id)
            continue;
        shelfItems.removeAt(i);
        emit changed();
        return true;
    }
    return false;
}

void FoldBlockShelfModel::clear()
{
    if (shelfItems.isEmpty())
        return;
    shelfItems.clear();
    emit changed();
}

QString foldShelfBlockMimeType()
{
    return QStringLiteral("application/x-zeroslack-fold-block");
}

QString foldShelfItemMimeType()
{
    return QStringLiteral("application/x-zeroslack-fold-shelf-item");
}

QByteArray encodeFoldShelfItem(const FoldShelfItem& item)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("alias"), item.alias);
    object.insert(QStringLiteral("text"), item.text);
    object.insert(QStringLiteral("sourceFile"), item.sourceFile);
    object.insert(QStringLiteral("sourceModule"), item.sourceModule);
    object.insert(QStringLiteral("sourceStartLine"), item.sourceStartLine);
    object.insert(QStringLiteral("sourceEndLine"), item.sourceEndLine);
    object.insert(QStringLiteral("lineCount"), item.lineCount);
    object.insert(QStringLiteral("originKind"), originToString(item.originKind));
    object.insert(QStringLiteral("consumed"), item.consumed);
    object.insert(QStringLiteral("stale"), item.stale);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

FoldShelfItem decodeFoldShelfItem(const QByteArray& payload)
{
    FoldShelfItem item;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
        return item;

    const QJsonObject object = document.object();
    item.id = object.value(QStringLiteral("id")).toString();
    item.alias = object.value(QStringLiteral("alias")).toString();
    item.text = object.value(QStringLiteral("text")).toString();
    item.sourceFile = object.value(QStringLiteral("sourceFile")).toString();
    item.sourceModule = object.value(QStringLiteral("sourceModule")).toString();
    item.sourceStartLine = object.value(QStringLiteral("sourceStartLine")).toInt(-1);
    item.sourceEndLine = object.value(QStringLiteral("sourceEndLine")).toInt(-1);
    item.lineCount = object.value(QStringLiteral("lineCount")).toInt(0);
    item.originKind = originFromString(
        object.value(QStringLiteral("originKind")).toString());
    item.consumed = object.value(QStringLiteral("consumed")).toBool(false);
    item.stale = object.value(QStringLiteral("stale")).toBool(false);
    return item;
}
