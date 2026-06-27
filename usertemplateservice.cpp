#include "usertemplateservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>

#include <algorithm>
#include <memory>

std::unique_ptr<UserTemplateService> UserTemplateService::instance = nullptr;

namespace {
constexpr const char* kUserTemplateGroup = "userTemplates";
constexpr const char* kUserTemplateItems = "items";
constexpr const char* kUserTemplateId = "id";
constexpr const char* kUserTemplateCommandToken = "commandToken";
constexpr const char* kUserTemplateLabel = "label";
constexpr const char* kUserTemplateDescription = "description";
constexpr const char* kUserTemplateInsertText = "insertText";
constexpr const char* kUserTemplateSelectionStart = "selectionStart";
constexpr const char* kUserTemplateSelectionLength = "selectionLength";
constexpr const char* kUserTemplateSlots = "slots";

std::unique_ptr<QSettings> makeSettings(const QString& settingsFilePath)
{
    if (!settingsFilePath.isEmpty())
        return std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("ZeroSlack"),
                                       QStringLiteral("ZeroSlack"));
}

QString normalizedTemplateId(const UserTemplateRecord& record)
{
    QString id = record.id.trimmed();
    if (!id.isEmpty())
        return id;

    QString token = record.commandToken.trimmed();
    if (token.startsWith(QStringLiteral(";;")))
        token.remove(0, 2);
    return token.trimmed().toLower();
}

QString normalizedCommandToken(const QString& token)
{
    return token.trimmed();
}

bool commandTokenHasWhitespace(const QString& token)
{
    for (const QChar ch : token) {
        if (ch.isSpace())
            return true;
    }
    return false;
}

bool rangeWithinText(int start, int length, const QString& text)
{
    if (start < 0)
        return true;
    return length >= 0 && start + length <= text.size();
}

void appendIssue(UserTemplateSaveReport* report,
                 const QString& id,
                 const QString& field,
                 const QString& reason)
{
    if (!report)
        return;
    report->issues.append(UserTemplateIssue{id, field, reason});
    if (report->failureReason.isEmpty())
        report->failureReason = reason;
}

QJsonArray slotsToJson(const CodeTemplateSlotList& slotList)
{
    QJsonArray array;
    for (const CodeTemplateSlot& slot : slotList) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), slot.name);
        object.insert(QStringLiteral("start"), slot.start);
        object.insert(QStringLiteral("length"), slot.length);
        array.append(object);
    }
    return array;
}

CodeTemplateSlotList slotsFromJson(const QString& jsonText)
{
    CodeTemplateSlotList slotList;
    const QJsonDocument document =
        QJsonDocument::fromJson(jsonText.toUtf8());
    if (!document.isArray())
        return slotList;

    const QJsonArray array = document.array();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        CodeTemplateSlot slot;
        slot.name = object.value(QStringLiteral("name")).toString();
        slot.start = object.value(QStringLiteral("start")).toInt(-1);
        slot.length = object.value(QStringLiteral("length")).toInt(0);
        slotList.append(slot);
    }
    return slotList;
}

QString slotsJsonText(const CodeTemplateSlotList& slotList)
{
    return QString::fromUtf8(
        QJsonDocument(slotsToJson(slotList)).toJson(QJsonDocument::Compact));
}

CodeTemplateItem itemFromRecord(const UserTemplateRecord& record)
{
    CodeTemplateItem item;
    item.commandToken = record.commandToken;
    item.label = record.label;
    item.description = record.description;
    item.defaultValue = record.insertText;
    item.insertText = record.insertText;
    item.selectionStart = record.selectionStart;
    item.selectionLength = record.selectionLength;
    item.templateSlots = record.templateSlots;
    return item;
}

bool tokenMatches(const QString& candidate, const QString& query)
{
    return QString::compare(candidate.trimmed(),
                            query.trimmed(),
                            Qt::CaseInsensitive) == 0;
}
}

UserTemplateService* UserTemplateService::getInstance()
{
    if (!instance)
        instance = std::make_unique<UserTemplateService>();
    return instance.get();
}

UserTemplateService::UserTemplateService(const QString& path)
    : settingsFilePath(path)
{
}

UserTemplateService::~UserTemplateService() = default;

QString UserTemplateService::storageLocation() const
{
    if (!settingsFilePath.isEmpty())
        return settingsFilePath;
    return QStringLiteral("QSettings:ZeroSlack/ZeroSlack/userTemplates");
}

QList<UserTemplateRecord> UserTemplateService::records() const
{
    QList<UserTemplateRecord> result;
    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kUserTemplateGroup));
    const int count =
        settings->beginReadArray(QString::fromLatin1(kUserTemplateItems));
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings->setArrayIndex(i);
        UserTemplateRecord record;
        record.id =
            settings->value(QString::fromLatin1(kUserTemplateId)).toString();
        record.commandToken =
            settings->value(QString::fromLatin1(kUserTemplateCommandToken)).toString();
        record.label =
            settings->value(QString::fromLatin1(kUserTemplateLabel)).toString();
        record.description =
            settings->value(QString::fromLatin1(kUserTemplateDescription)).toString();
        record.insertText =
            settings->value(QString::fromLatin1(kUserTemplateInsertText)).toString();
        record.selectionStart =
            settings->value(QString::fromLatin1(kUserTemplateSelectionStart), -1)
                .toInt();
        record.selectionLength =
            settings->value(QString::fromLatin1(kUserTemplateSelectionLength), 0)
                .toInt();
        record.templateSlots = slotsFromJson(
            settings->value(QString::fromLatin1(kUserTemplateSlots)).toString());
        if (!record.id.trimmed().isEmpty())
            result.append(record);
    }
    settings->endArray();
    settings->endGroup();
    return result;
}

QList<CodeTemplateItem> UserTemplateService::catalog() const
{
    QList<CodeTemplateItem> result;
    const QList<UserTemplateRecord> storedRecords = records();
    result.reserve(storedRecords.size());
    for (const UserTemplateRecord& record : storedRecords)
        result.append(itemFromRecord(record));
    return result;
}

QList<CodeTemplateItem> UserTemplateService::matchingTemplates(
    const QString& commandToken) const
{
    QList<CodeTemplateItem> result;
    const QString normalized = commandToken.trimmed();
    for (const UserTemplateRecord& record : records()) {
        if (normalized.isEmpty()
            || tokenMatches(record.commandToken, normalized)) {
            result.append(itemFromRecord(record));
        }
    }
    return result;
}

CodeTemplateItem UserTemplateService::templateForCommand(
    const QString& commandToken) const
{
    const QList<CodeTemplateItem> matches = matchingTemplates(commandToken);
    return matches.isEmpty() ? CodeTemplateItem() : matches.first();
}

UserTemplateSaveReport UserTemplateService::validateRecords(
    const QList<UserTemplateRecord>& inputRecords) const
{
    UserTemplateSaveReport report;
    QSet<QString> seenIds;
    report.records.reserve(inputRecords.size());

    int slotCounter = 0;
    for (const UserTemplateRecord& input : inputRecords) {
        UserTemplateRecord record = input;
        record.id = normalizedTemplateId(record);
        record.commandToken = normalizedCommandToken(record.commandToken);
        record.label = record.label.trimmed();
        record.description = record.description.trimmed();

        if (record.id.isEmpty()) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("id"),
                        QStringLiteral("Template id is required."));
        }
        const QString lowerId = record.id.toLower();
        if (!lowerId.isEmpty() && seenIds.contains(lowerId)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("id"),
                        QStringLiteral("Template id must be unique."));
        }
        seenIds.insert(lowerId);

        if (!record.commandToken.startsWith(QStringLiteral(";;"))
            || record.commandToken.size() <= 2
            || commandTokenHasWhitespace(record.commandToken)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("commandToken"),
                        QStringLiteral("Template command token must be a compact ;; token."));
        }
        if (record.commandToken.startsWith(QStringLiteral(";:"))) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("commandToken"),
                        QStringLiteral(";: is reserved and inactive."));
        }
        if (record.insertText.isEmpty()) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("insertText"),
                        QStringLiteral("Template text is required."));
        }
        if (!rangeWithinText(record.selectionStart,
                             record.selectionLength,
                             record.insertText)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("selection"),
                        QStringLiteral("Selection range must stay inside template text."));
        }
        if (record.selectionStart < 0)
            record.selectionLength = 0;

        CodeTemplateSlotList normalizedSlots;
        normalizedSlots.reserve(record.templateSlots.size());
        for (CodeTemplateSlot slot : record.templateSlots) {
            ++slotCounter;
            slot.name = slot.name.trimmed();
            if (slot.name.isEmpty())
                slot.name = QStringLiteral("slot%1").arg(slotCounter);
            if (!rangeWithinText(slot.start, slot.length, record.insertText)) {
                appendIssue(&report,
                            record.id,
                            QStringLiteral("templateSlots"),
                            QStringLiteral("Template slot ranges must stay inside template text."));
                continue;
            }
            normalizedSlots.append(slot);
        }
        record.templateSlots = normalizedSlots;

        if (record.label.isEmpty())
            record.label = record.commandToken;
        if (record.description.isEmpty())
            record.description = QStringLiteral("User template");

        report.records.append(record);
    }

    report.valid = report.issues.isEmpty();
    return report;
}

UserTemplateSaveReport UserTemplateService::setRecords(
    const QList<UserTemplateRecord>& inputRecords) const
{
    UserTemplateSaveReport report = validateRecords(inputRecords);
    if (!report.valid)
        return report;

    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kUserTemplateGroup));
    settings->remove(QString());
    settings->beginWriteArray(QString::fromLatin1(kUserTemplateItems));
    for (int i = 0; i < report.records.size(); ++i) {
        settings->setArrayIndex(i);
        const UserTemplateRecord& record = report.records.at(i);
        settings->setValue(QString::fromLatin1(kUserTemplateId), record.id);
        settings->setValue(QString::fromLatin1(kUserTemplateCommandToken),
                          record.commandToken);
        settings->setValue(QString::fromLatin1(kUserTemplateLabel), record.label);
        settings->setValue(QString::fromLatin1(kUserTemplateDescription),
                          record.description);
        settings->setValue(QString::fromLatin1(kUserTemplateInsertText),
                          record.insertText);
        settings->setValue(QString::fromLatin1(kUserTemplateSelectionStart),
                          record.selectionStart);
        settings->setValue(QString::fromLatin1(kUserTemplateSelectionLength),
                          record.selectionLength);
        settings->setValue(QString::fromLatin1(kUserTemplateSlots),
                          slotsJsonText(record.templateSlots));
    }
    settings->endArray();
    settings->endGroup();
    settings->sync();
    return report;
}

UserTemplateSaveReport UserTemplateService::addOrUpdateRecord(
    const UserTemplateRecord& record) const
{
    QList<UserTemplateRecord> nextRecords = records();
    const QString targetId = normalizedTemplateId(record).toLower();
    bool updated = false;
    for (UserTemplateRecord& existing : nextRecords) {
        if (existing.id.toLower() == targetId) {
            existing = record;
            updated = true;
            break;
        }
    }
    if (!updated)
        nextRecords.append(record);
    return setRecords(nextRecords);
}

bool UserTemplateService::removeRecord(const QString& id) const
{
    const QString targetId = id.trimmed().toLower();
    if (targetId.isEmpty())
        return false;

    QList<UserTemplateRecord> nextRecords;
    bool removed = false;
    for (const UserTemplateRecord& record : records()) {
        if (record.id.toLower() == targetId) {
            removed = true;
            continue;
        }
        nextRecords.append(record);
    }
    if (!removed)
        return false;

    return setRecords(nextRecords).valid;
}

void UserTemplateService::clear() const
{
    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kUserTemplateGroup));
    settings->remove(QString());
    settings->endGroup();
    settings->sync();
}
