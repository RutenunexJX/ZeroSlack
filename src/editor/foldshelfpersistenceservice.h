#ifndef FOLDSHELFPERSISTENCESERVICE_H
#define FOLDSHELFPERSISTENCESERVICE_H

#include "foldblockshelfmodel.h"

#include <QList>
#include <QString>
#include <memory>

class FoldShelfPersistenceService
{
public:
    static FoldShelfPersistenceService* getInstance();

    explicit FoldShelfPersistenceService(
        const QString& settingsFilePath = QString());
    ~FoldShelfPersistenceService();

    QString storageLocation() const;
    QList<FoldShelfItem> loadItems(const QString& workspaceRoot) const;
    bool saveItems(const QString& workspaceRoot,
                   const QList<FoldShelfItem>& items) const;

    static QString normalizedWorkspaceRoot(const QString& workspaceRoot);
    static QString normalizedSourceFile(const QString& sourceFile);

private:
    QString settingsFilePath;

    static std::unique_ptr<FoldShelfPersistenceService> instance;
};

#endif // FOLDSHELFPERSISTENCESERVICE_H
