#ifndef USERTEMPLATESERVICE_H
#define USERTEMPLATESERVICE_H

#include "completiontypes.h"

#include <QList>
#include <QString>
#include <memory>

struct UserTemplateRecord {
    QString id;
    QString commandToken;
    QString label;
    QString description;
    QString insertText;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
    QVariantMap command;
    QString storageSource;
    QVariantMap extraFields;
};

struct UserTemplateIssue {
    QString source;
    QString id;
    QString field;
    QString reason;
};

struct UserTemplateLoadReport {
    bool valid = false;
    QList<UserTemplateRecord> records;
    QList<UserTemplateIssue> issues;
    QString failureReason;
};

using UserTemplateSaveReport = UserTemplateLoadReport;

class UserTemplateService
{
public:
    static UserTemplateService* getInstance();

    explicit UserTemplateService(
        const QString& globalTemplateFilePath = QString(),
        const QString& workspaceTemplateFilePath = QString());
    ~UserTemplateService();

    QString storageLocation() const;
    QString globalTemplateLocation() const;
    QString workspaceTemplateLocation() const;
    void setGlobalTemplateFilePath(const QString& filePath);
    void setWorkspaceRoot(const QString& workspaceRoot);
    void setWorkspaceTemplateFilePath(const QString& filePath);
    UserTemplateLoadReport reload() const;
    QList<UserTemplateRecord> records() const;
    QList<UserTemplateRecord> recordsForFile(const QString& fileName, bool simulationOnly = false) const;
    QList<CodeTemplateItem> catalogForFile(const QString& fileName, bool simulationOnly = false) const;
    UserTemplateSaveReport saveRecord(const UserTemplateRecord& record, bool workspace, quint64 expectedRevision = 0) const;
    UserTemplateSaveReport deleteRecord(const QString& id, bool workspace, quint64 expectedRevision = 0) const;
    UserTemplateSaveReport migrateXipsCommands(const QString& metadataPath, const QVariantMap& asset = {}) const;
    static QList<UserTemplateRecord> simulationBuiltins();
    QVariantMap signalRoles() const;
    QString setSignalRoles(const QVariantMap& roles) const;
    QList<CodeTemplateItem> catalog() const;
    quint64 catalogRevision() const;
    quint64 fileReadsForTesting() const { return fileReads; }
    QList<CodeTemplateItem> matchingTemplates(
        const QString& commandToken) const;
    CodeTemplateItem templateForCommand(const QString& commandToken) const;

    UserTemplateSaveReport validateRecords(
        const QList<UserTemplateRecord>& records) const;
    UserTemplateSaveReport setRecords(
        const QList<UserTemplateRecord>& records) const;
    UserTemplateSaveReport addOrUpdateRecord(
        const UserTemplateRecord& record) const;
    bool removeRecord(const QString& id) const;
    void clear() const;

private:
    QString globalTemplateFilePath;
    QString workspaceTemplateFilePath;
    mutable QString cachedFileVersion;
    mutable UserTemplateLoadReport cachedReport;
    mutable QList<CodeTemplateItem> cachedCatalog;
    mutable bool cacheValid = false;
    mutable quint64 revision = 0;
    mutable quint64 fileReads = 0;
    void ensureLoaded() const;

    static std::unique_ptr<UserTemplateService> instance;
};

#endif // USERTEMPLATESERVICE_H
