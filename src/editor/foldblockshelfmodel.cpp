#include "foldblockshelfmodel.h"

#include "foldshelfpersistenceservice.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

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

int lineCountForText(const QString& text)
{
    if (text.isEmpty())
        return 0;
    int count = text.count(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n')))
        ++count;
    return count;
}

void normalizeShelfItem(FoldShelfItem* item)
{
    if (!item)
        return;
    item->id = item->id.trimmed();
    item->alias = item->alias.trimmed();
    item->sourceModule = item->sourceModule.trimmed();
    item->sourceFile =
        FoldShelfPersistenceService::normalizedSourceFile(item->sourceFile);
    item->lineCount = lineCountForText(item->text);
    if (item->alias.isEmpty())
        item->alias = item->id;
}

bool containsAllTerms(const QString& haystack, const QStringList& terms)
{
    for (const QString& term : terms) {
        if (!haystack.contains(term, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

QString searchableTextForItem(const FoldShelfItem& item)
{
    const QString origin = item.originKind == FoldShelfOriginKind::Moved
        ? QStringLiteral("moved")
        : QStringLiteral("copied");
    QStringList fields;
    fields << item.id
           << item.alias
           << item.sourceFile
           << item.sourceModule
           << item.text
           << origin;
    if (item.consumed)
        fields << QStringLiteral("consumed");
    if (item.stale)
        fields << QStringLiteral("stale");
    return fields.join(QLatin1Char('\n'));
}
}

FoldBlockShelfModel::FoldBlockShelfModel(QObject* parent)
    : QObject(parent)
{
}

void FoldBlockShelfModel::setPersistenceService(
    FoldShelfPersistenceService* service)
{
    if (persistence == service)
        return;
    persistence = service;
    loadPersistedItems();
}

void FoldBlockShelfModel::setWorkspaceRoot(const QString& rootPath)
{
    const QString normalizedRoot =
        FoldShelfPersistenceService::normalizedWorkspaceRoot(rootPath);
    if (persistenceWorkspaceRoot == normalizedRoot)
        return;
    persistenceWorkspaceRoot = normalizedRoot;
    loadPersistedItems();
}

QString FoldBlockShelfModel::workspaceRoot() const
{
    return persistenceWorkspaceRoot;
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
    normalizeShelfItem(&item);
    shelfItems.append(item);
    persistItems();
    emit changed();
    return item.id;
}

bool FoldBlockShelfModel::consumeItem(const QString& id)
{
    for (FoldShelfItem& item : shelfItems) {
        if (item.id != id)
            continue;
        item.consumed = true;
        persistItems();
        emit changed();
        return true;
    }
    return false;
}

bool FoldBlockShelfModel::markItemStale(const QString& id)
{
    for (FoldShelfItem& item : shelfItems) {
        if (item.id != id)
            continue;
        item.stale = true;
        persistItems();
        emit changed();
        return true;
    }
    return false;
}

bool FoldBlockShelfModel::renameItem(const QString& id, const QString& alias)
{
    const QString cleanAlias = alias.trimmed();
    if (id.trimmed().isEmpty() || cleanAlias.isEmpty())
        return false;

    for (FoldShelfItem& item : shelfItems) {
        if (item.id != id)
            continue;
        if (item.alias == cleanAlias)
            return true;
        item.alias = cleanAlias;
        persistItems();
        emit changed();
        return true;
    }
    return false;
}

QList<FoldShelfItem> FoldBlockShelfModel::itemsMatching(
    const QString& query) const
{
    const QString cleanQuery = query.trimmed();
    if (cleanQuery.isEmpty())
        return shelfItems;

    const QStringList terms =
        cleanQuery.split(QRegularExpression(QStringLiteral("\\s+")),
                         Qt::SkipEmptyParts);
    if (terms.isEmpty())
        return shelfItems;

    QList<FoldShelfItem> result;
    for (const FoldShelfItem& item : shelfItems) {
        if (containsAllTerms(searchableTextForItem(item), terms))
            result.append(item);
    }
    return result;
}

int FoldBlockShelfModel::removeConsumedOrStaleItems()
{
    int removed = 0;
    for (int i = shelfItems.size() - 1; i >= 0; --i) {
        const FoldShelfItem& item = shelfItems.at(i);
        if (!item.consumed && !item.stale)
            continue;
        shelfItems.removeAt(i);
        ++removed;
    }

    if (removed > 0) {
        persistItems();
        emit changed();
    }
    return removed;
}

bool FoldBlockShelfModel::removeItem(const QString& id)
{
    for (int i = 0; i < shelfItems.size(); ++i) {
        if (shelfItems.at(i).id != id)
            continue;
        shelfItems.removeAt(i);
        persistItems();
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
    persistItems();
    emit changed();
}

void FoldBlockShelfModel::loadPersistedItems()
{
    if (!persistence)
        return;

    shelfItems = persistence->loadItems(persistenceWorkspaceRoot);
    refreshNextId();
    emit changed();
}

void FoldBlockShelfModel::persistItems() const
{
    if (persistence)
        persistence->saveItems(persistenceWorkspaceRoot, shelfItems);
}

void FoldBlockShelfModel::refreshNextId()
{
    int next = 1;
    const QRegularExpression idPattern(QStringLiteral("^fold_shelf_(\\d+)$"));
    for (const FoldShelfItem& item : std::as_const(shelfItems)) {
        const QRegularExpressionMatch match = idPattern.match(item.id);
        if (!match.hasMatch())
            continue;
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (ok)
            next = qMax(next, value + 1);
    }
    nextId = next;
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
