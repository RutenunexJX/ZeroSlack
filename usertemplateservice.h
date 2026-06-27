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
};

struct UserTemplateIssue {
    QString id;
    QString field;
    QString reason;
};

struct UserTemplateSaveReport {
    bool valid = false;
    QList<UserTemplateRecord> records;
    QList<UserTemplateIssue> issues;
    QString failureReason;
};

class UserTemplateService
{
public:
    static UserTemplateService* getInstance();

    explicit UserTemplateService(const QString& settingsFilePath = QString());
    ~UserTemplateService();

    QString storageLocation() const;
    QList<UserTemplateRecord> records() const;
    QList<CodeTemplateItem> catalog() const;
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
    QString settingsFilePath;

    static std::unique_ptr<UserTemplateService> instance;
};

#endif // USERTEMPLATESERVICE_H
