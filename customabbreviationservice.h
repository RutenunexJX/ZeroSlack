#ifndef CUSTOMABBREVIATIONSERVICE_H
#define CUSTOMABBREVIATIONSERVICE_H

#include "completiontypes.h"

#include <QList>
#include <QString>
#include <memory>

struct CustomAbbreviationRecord {
    QString id;
    QString abbreviation;
    QString commandToken;
    QString label;
    QString description;
};

struct CustomAbbreviationIssue {
    QString id;
    QString field;
    QString reason;
};

struct CustomAbbreviationSaveReport {
    bool valid = false;
    QList<CustomAbbreviationRecord> records;
    QList<CustomAbbreviationIssue> issues;
    QString failureReason;
};

struct CustomAbbreviationResolution {
    bool matched = false;
    CustomAbbreviationRecord record;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
};

class CustomAbbreviationService
{
public:
    static CustomAbbreviationService* getInstance();

    explicit CustomAbbreviationService(
        const QString& settingsFilePath = QString());
    ~CustomAbbreviationService();

    QString storageLocation() const;
    QList<CustomAbbreviationRecord> records() const;
    QList<CustomAbbreviationRecord> matchingRecords(
        const QString& prefix) const;
    CustomAbbreviationResolution resolveForIntent(
        const QString& abbreviation,
        InlineCommandIntent intent) const;

    CustomAbbreviationSaveReport validateRecords(
        const QList<CustomAbbreviationRecord>& records) const;
    CustomAbbreviationSaveReport setRecords(
        const QList<CustomAbbreviationRecord>& records) const;
    CustomAbbreviationSaveReport addOrUpdateRecord(
        const CustomAbbreviationRecord& record) const;
    bool removeRecord(const QString& id) const;
    void clear() const;

private:
    QString settingsFilePath;

    static std::unique_ptr<CustomAbbreviationService> instance;
};

#endif // CUSTOMABBREVIATIONSERVICE_H
