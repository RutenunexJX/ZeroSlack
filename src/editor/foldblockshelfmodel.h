#ifndef FOLDBLOCKSHELFMODEL_H
#define FOLDBLOCKSHELFMODEL_H

#include "zeroslackexport.h"

#include <QObject>
#include <QList>
#include <QString>

class FoldShelfPersistenceService;

enum class FoldShelfOriginKind {
    Moved,
    Copied
};

struct FoldShelfItem {
    QString id;
    QString alias;
    QString text;
    QString sourceFile;
    QString sourceModule;
    int sourceStartLine = -1;
    int sourceEndLine = -1;
    int lineCount = 0;
    FoldShelfOriginKind originKind = FoldShelfOriginKind::Copied;
    bool consumed = false;
    bool stale = false;
};

class ZEROSLACK_API FoldBlockShelfModel : public QObject
{
    Q_OBJECT

public:
    explicit FoldBlockShelfModel(QObject* parent = nullptr);

    void setPersistenceService(FoldShelfPersistenceService* service);
    void setWorkspaceRoot(const QString& rootPath);
    QString workspaceRoot() const;
    QList<FoldShelfItem> items() const;
    FoldShelfItem item(const QString& id) const;
    QString addItem(FoldShelfItem item);
    bool consumeItem(const QString& id);
    bool markItemStale(const QString& id);
    bool renameItem(const QString& id, const QString& alias);
    QList<FoldShelfItem> itemsMatching(const QString& query) const;
    int removeConsumedOrStaleItems();
    bool removeItem(const QString& id);
    void clear();

signals:
    void changed();

private:
    QList<FoldShelfItem> shelfItems;
    int nextId = 1;
    QString persistenceWorkspaceRoot;
    FoldShelfPersistenceService* persistence = nullptr;

    void loadPersistedItems();
    void persistItems() const;
    void refreshNextId();
};

QString foldShelfBlockMimeType();
QString foldShelfItemMimeType();
QByteArray encodeFoldShelfItem(const FoldShelfItem& item);
FoldShelfItem decodeFoldShelfItem(const QByteArray& payload);

#endif // FOLDBLOCKSHELFMODEL_H
