#include "usertemplateservice.h"

#include "codetemplateservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

std::unique_ptr<UserTemplateService> UserTemplateService::instance = nullptr;

namespace {
constexpr const char* kUserTemplateFileName = "user_templates.json";
constexpr const char* kUserTemplateWorkspaceDir = ".zeroslack";
constexpr const char* kUserTemplateTemplates = "templates";
constexpr const char* kUserTemplateCommand = "command";
constexpr const char* kUserTemplateCommandToken = "commandToken";
constexpr const char* kUserTemplateDescription = "description";
constexpr const char* kUserTemplateBody = "body";
constexpr const char* kUserTemplateInsertText = "insertText";
constexpr const char* kUserTemplateSlots = "slots";
constexpr const char* kUserTemplateSlotName = "name";
constexpr const char* kUserTemplateSlotStart = "start";
constexpr const char* kUserTemplateSlotLength = "length";
constexpr const char* kUserTemplateSelectionStart = "selectionStart";
constexpr const char* kUserTemplateSelectionLength = "selectionLength";

enum class UserTemplateScope {
    Global,
    Workspace
};

QString scopeName(UserTemplateScope scope)
{
    return scope == UserTemplateScope::Workspace
        ? QStringLiteral("workspace")
        : QStringLiteral("global");
}

QString normalizePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString defaultGlobalTemplatePath()
{
    QString configRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configRoot.isEmpty())
        configRoot = QDir::home().absoluteFilePath(QStringLiteral(".zeroslack"));
    return QDir(configRoot).absoluteFilePath(
        QString::fromLatin1(kUserTemplateFileName));
}

QString workspaceTemplatePathForRoot(const QString& workspaceRoot)
{
    const QString normalizedRoot = normalizePath(workspaceRoot);
    if (normalizedRoot.isEmpty())
        return QString();
    return QDir(normalizedRoot).absoluteFilePath(
        QStringLiteral("%1/%2")
            .arg(QString::fromLatin1(kUserTemplateWorkspaceDir),
                 QString::fromLatin1(kUserTemplateFileName)));
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

bool slotRangeWithinText(int start, int length, const QString& text)
{
    return start >= 0 && length >= 0 && start + length <= text.size();
}

QSet<QString> builtInTemplateTokens()
{
    QSet<QString> result;
    for (const CodeTemplateItem& item : CodeTemplateService::getInstance()->catalog())
        result.insert(item.commandToken.trimmed().toCaseFolded());
    return result;
}

QSet<QString> reservedTemplateTokens()
{
    return {
        QStringLiteral(";;h"),
        QStringLiteral(";;pk"),
    };
}

bool isBuiltInTemplateToken(const QString& token)
{
    return builtInTemplateTokens().contains(token.trimmed().toCaseFolded());
}

bool isReservedTemplateToken(const QString& token)
{
    return reservedTemplateTokens().contains(token.trimmed().toCaseFolded());
}

void appendIssue(UserTemplateLoadReport* report,
                 const QString& source,
                 const QString& id,
                 const QString& field,
                 const QString& reason)
{
    if (!report)
        return;
    report->issues.append(UserTemplateIssue{source, id, field, reason});
    if (report->failureReason.isEmpty())
        report->failureReason = reason;
}

QJsonArray slotsToJson(const CodeTemplateSlotList& slotList)
{
    QJsonArray array;
    for (const CodeTemplateSlot& slot : slotList) {
        QJsonObject object;
        object.insert(QString::fromLatin1(kUserTemplateSlotName), slot.name);
        object.insert(QString::fromLatin1(kUserTemplateSlotStart), slot.start);
        object.insert(QString::fromLatin1(kUserTemplateSlotLength), slot.length);
        array.append(object);
    }
    return array;
}

CodeTemplateSlotList slotsFromJson(const QJsonValue& value,
                                   const QString& insertText,
                                   const QString& source,
                                   const QString& id,
                                   UserTemplateLoadReport* report)
{
    CodeTemplateSlotList slotList;
    if (value.isUndefined() || value.isNull())
        return slotList;
    if (!value.isArray()) {
        appendIssue(report,
                    source,
                    id,
                    QString::fromLatin1(kUserTemplateSlots),
                    QStringLiteral("Template slots must be an array."));
        return slotList;
    }

    const QJsonArray array = value.toArray();
    slotList.reserve(array.size());
    for (int i = 0; i < array.size(); ++i) {
        const QJsonValue slotValue = array.at(i);
        if (!slotValue.isObject()) {
            appendIssue(report,
                        source,
                        id,
                        QString::fromLatin1(kUserTemplateSlots),
                        QStringLiteral("Template slot entries must be objects."));
            continue;
        }

        const QJsonObject object = slotValue.toObject();
        CodeTemplateSlot slot;
        slot.name =
            object.value(QString::fromLatin1(kUserTemplateSlotName))
                .toString()
                .trimmed();
        if (slot.name.isEmpty())
            slot.name = QStringLiteral("slot%1").arg(i + 1);

        const QJsonValue startValue =
            object.value(QString::fromLatin1(kUserTemplateSlotStart));
        const QJsonValue lengthValue =
            object.value(QString::fromLatin1(kUserTemplateSlotLength));
        if (!startValue.isDouble() || !lengthValue.isDouble()) {
            appendIssue(report,
                        source,
                        id,
                        QString::fromLatin1(kUserTemplateSlots),
                        QStringLiteral("Template slot start and length are required."));
            continue;
        }
        slot.start = startValue.toInt(-1);
        slot.length = lengthValue.toInt(-1);
        if (!slotRangeWithinText(slot.start, slot.length, insertText)) {
            appendIssue(report,
                        source,
                        id,
                        QString::fromLatin1(kUserTemplateSlots),
                        QStringLiteral("Template slot ranges must stay inside template text."));
            continue;
        }
        slotList.append(slot);
    }
    return slotList;
}

CodeTemplateItem itemFromRecord(const UserTemplateRecord& record)
{
    CodeTemplateItem item;
    item.commandToken = record.commandToken;
    item.label = record.label.isEmpty() ? record.commandToken : record.label;
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

UserTemplateRecord recordFromJsonObject(const QJsonObject& object,
                                        const QString& source,
                                        UserTemplateLoadReport* report)
{
    UserTemplateRecord record;
    record.commandToken =
        object.value(QString::fromLatin1(kUserTemplateCommand)).toString();
    if (record.commandToken.trimmed().isEmpty()) {
        record.commandToken =
            object.value(QString::fromLatin1(kUserTemplateCommandToken))
                .toString();
    }
    record.commandToken = normalizedCommandToken(record.commandToken);
    record.id = normalizedTemplateId(record);
    record.label = record.commandToken;
    record.description =
        object.value(QString::fromLatin1(kUserTemplateDescription))
            .toString()
            .trimmed();
    record.insertText =
        object.value(QString::fromLatin1(kUserTemplateBody)).toString();
    if (record.insertText.isEmpty()) {
        record.insertText =
            object.value(QString::fromLatin1(kUserTemplateInsertText))
                .toString();
    }
    record.selectionStart =
        object.value(QString::fromLatin1(kUserTemplateSelectionStart)).toInt(-1);
    record.selectionLength =
        object.value(QString::fromLatin1(kUserTemplateSelectionLength)).toInt(0);
    record.templateSlots =
        slotsFromJson(object.value(QString::fromLatin1(kUserTemplateSlots)),
                      record.insertText,
                      source,
                      record.commandToken,
                      report);

    if (record.selectionStart < 0 && !record.templateSlots.isEmpty()) {
        record.selectionStart = record.templateSlots.first().start;
        record.selectionLength = record.templateSlots.first().length;
    }
    if (record.description.isEmpty())
        record.description = QStringLiteral("User template");
    return record;
}

QJsonArray templateArrayFromDocument(const QJsonDocument& document,
                                     const QString& source,
                                     UserTemplateLoadReport* report)
{
    if (document.isArray())
        return document.array();
    if (document.isObject()) {
        const QJsonValue templates =
            document.object().value(QString::fromLatin1(kUserTemplateTemplates));
        if (templates.isArray())
            return templates.toArray();
    }

    appendIssue(report,
                source,
                QString(),
                QString::fromLatin1(kUserTemplateTemplates),
                QStringLiteral("User template JSON must be an array or contain a templates array."));
    return {};
}

QList<UserTemplateRecord> readTemplateFile(const QString& filePath,
                                           UserTemplateScope scope,
                                           UserTemplateLoadReport* report)
{
    QList<UserTemplateRecord> records;
    if (filePath.trimmed().isEmpty())
        return records;
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists())
        return records;
    if (!fileInfo.isFile()) {
        appendIssue(report,
                    filePath,
                    QString(),
                    scopeName(scope),
                    QStringLiteral("User template path is not a file."));
        return records;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendIssue(report,
                    filePath,
                    QString(),
                    scopeName(scope),
                    QStringLiteral("Failed to read user template file."));
        return records;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        appendIssue(report,
                    filePath,
                    QString(),
                    scopeName(scope),
                    QStringLiteral("Invalid user template JSON: %1.")
                        .arg(parseError.errorString()));
        return records;
    }

    const QJsonArray array = templateArrayFromDocument(document, filePath, report);
    records.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            appendIssue(report,
                        filePath,
                        QString(),
                        QString::fromLatin1(kUserTemplateTemplates),
                        QStringLiteral("User template entries must be objects."));
            continue;
        }
        UserTemplateLoadReport recordReport;
        UserTemplateRecord record =
            recordFromJsonObject(value.toObject(), filePath, &recordReport);
        if (!recordReport.issues.isEmpty()) {
            if (report) {
                report->issues.append(recordReport.issues);
                if (report->failureReason.isEmpty())
                    report->failureReason = recordReport.failureReason;
            }
            continue;
        }
        records.append(record);
    }
    return records;
}

bool appendValidRecord(UserTemplateLoadReport* report,
                       const UserTemplateRecord& input,
                       const QString& source,
                       QSet<QString>* seenIds,
                       QSet<QString>* seenCommands,
                       bool strictDuplicates)
{
    if (!report || !seenIds || !seenCommands)
        return false;

    UserTemplateRecord record = input;
    record.id = normalizedTemplateId(record);
    record.commandToken = normalizedCommandToken(record.commandToken);
    record.label = record.commandToken;
    record.description = record.description.trimmed();
    const QString issueCommand = record.commandToken.isEmpty()
        ? record.id
        : record.commandToken;

    const QString lowerId = record.id.toCaseFolded();
    const QString lowerCommand = record.commandToken.toCaseFolded();
    const int issueCountBefore = report->issues.size();

    if (record.id.isEmpty()) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("id"),
                    QStringLiteral("Template id is required."));
    }
    if (!lowerId.isEmpty() && seenIds->contains(lowerId)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("id"),
                    QStringLiteral("Template id must be unique."));
    }

    if (!record.commandToken.startsWith(QStringLiteral(";;"))
        || record.commandToken.size() <= 2
        || commandTokenHasWhitespace(record.commandToken)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral("Template command token must be a compact ;; token."));
    }
    if (record.commandToken.startsWith(QStringLiteral(";:"))) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral(";: is reserved and inactive."));
    }
    if (isBuiltInTemplateToken(record.commandToken)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral("User templates cannot override built-in ;;cmd templates."));
    }
    if (isReservedTemplateToken(record.commandToken)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral("This template command is reserved for a suspended built-in slot."));
    }
    if (strictDuplicates && !lowerCommand.isEmpty()
        && seenCommands->contains(lowerCommand)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral("Template command token must be unique."));
    }
    if (record.insertText.isEmpty()) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("body"),
                    QStringLiteral("Template text is required."));
    }
    if (!rangeWithinText(record.selectionStart,
                         record.selectionLength,
                         record.insertText)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("selection"),
                    QStringLiteral("Selection range must stay inside template text."));
    }
    if (record.selectionStart < 0)
        record.selectionLength = 0;

    CodeTemplateSlotList normalizedSlots;
    normalizedSlots.reserve(record.templateSlots.size());
    int slotCounter = 0;
    for (CodeTemplateSlot slot : record.templateSlots) {
        ++slotCounter;
        slot.name = slot.name.trimmed();
        if (slot.name.isEmpty())
            slot.name = QStringLiteral("slot%1").arg(slotCounter);
        if (!slotRangeWithinText(slot.start, slot.length, record.insertText)) {
            appendIssue(report,
                        source,
                        issueCommand,
                        QStringLiteral("slots"),
                        QStringLiteral("Template slot ranges must stay inside template text."));
            continue;
        }
        normalizedSlots.append(slot);
    }
    record.templateSlots = normalizedSlots;
    if (record.selectionStart < 0 && !record.templateSlots.isEmpty()) {
        record.selectionStart = record.templateSlots.first().start;
        record.selectionLength = record.templateSlots.first().length;
    }
    if (record.description.isEmpty())
        record.description = QStringLiteral("User template");

    if (report->issues.size() != issueCountBefore)
        return false;

    seenIds->insert(lowerId);
    seenCommands->insert(lowerCommand);
    report->records.append(record);
    return true;
}

UserTemplateLoadReport validateRecordsForSave(
    const QList<UserTemplateRecord>& inputRecords,
    const QString& source)
{
    UserTemplateLoadReport report;
    report.records.reserve(inputRecords.size());
    QSet<QString> seenIds;
    QSet<QString> seenCommands;
    for (const UserTemplateRecord& record : inputRecords) {
        appendValidRecord(&report,
                          record,
                          source,
                          &seenIds,
                          &seenCommands,
                          true);
    }
    report.valid = report.issues.isEmpty();
    return report;
}

void mergeLoadedRecords(UserTemplateLoadReport* report,
                        const QList<UserTemplateRecord>& records,
                        const QString& source,
                        UserTemplateScope scope)
{
    if (!report)
        return;

    QSet<QString> seenIdsInFile;
    QSet<QString> seenCommandsInFile;
    for (const UserTemplateRecord& record : records) {
        UserTemplateLoadReport candidateReport;
        QSet<QString> candidateIds = seenIdsInFile;
        QSet<QString> candidateCommands = seenCommandsInFile;
        if (!appendValidRecord(&candidateReport,
                               record,
                               source,
                               &candidateIds,
                               &candidateCommands,
                               true)) {
            report->issues.append(candidateReport.issues);
            if (report->failureReason.isEmpty())
                report->failureReason = candidateReport.failureReason;
            continue;
        }

        seenIdsInFile = candidateIds;
        seenCommandsInFile = candidateCommands;

        UserTemplateRecord validRecord = candidateReport.records.first();
        const QString lowerCommand =
            validRecord.commandToken.trimmed().toCaseFolded();
        const auto existing =
            std::find_if(report->records.begin(),
                         report->records.end(),
                         [&lowerCommand](const UserTemplateRecord& existingRecord) {
                             return existingRecord.commandToken.trimmed().toCaseFolded()
                                 == lowerCommand;
                         });
        if (existing != report->records.end()) {
            if (scope == UserTemplateScope::Workspace) {
                *existing = validRecord;
            } else {
                appendIssue(report,
                            source,
                            validRecord.id,
                            QStringLiteral("command"),
                            QStringLiteral("Duplicate global user template command ignored."));
            }
            continue;
        }
        report->records.append(validRecord);
    }
}

QJsonObject recordToJsonObject(const UserTemplateRecord& record)
{
    QJsonObject object;
    object.insert(QString::fromLatin1(kUserTemplateCommand), record.commandToken);
    object.insert(QString::fromLatin1(kUserTemplateDescription),
                  record.description);
    object.insert(QString::fromLatin1(kUserTemplateBody), record.insertText);
    if (!record.templateSlots.isEmpty()) {
        object.insert(QString::fromLatin1(kUserTemplateSlots),
                      slotsToJson(record.templateSlots));
    }
    return object;
}

bool writeRecordsToJsonFile(const QString& filePath,
                            const QList<UserTemplateRecord>& records,
                            QString* failureReason)
{
    if (filePath.trimmed().isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No user template file path is configured.");
        return false;
    }

    const QFileInfo fileInfo(filePath);
    QDir parentDir = fileInfo.dir();
    if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
        if (failureReason)
            *failureReason = QStringLiteral("Failed to create user template directory.");
        return false;
    }

    QJsonArray array;
    for (const UserTemplateRecord& record : records)
        array.append(recordToJsonObject(record));

    QJsonObject root;
    root.insert(QString::fromLatin1(kUserTemplateTemplates), array);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (failureReason)
            *failureReason = QStringLiteral("Failed to write user template file.");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
}

UserTemplateService* UserTemplateService::getInstance()
{
    if (!instance)
        instance = std::make_unique<UserTemplateService>();
    return instance.get();
}

UserTemplateService::UserTemplateService(const QString& globalPath,
                                         const QString& workspacePath)
    : globalTemplateFilePath(globalPath.trimmed().isEmpty()
                                 ? defaultGlobalTemplatePath()
                                 : normalizePath(globalPath))
    , workspaceTemplateFilePath(normalizePath(workspacePath))
{
}

UserTemplateService::~UserTemplateService() = default;

QString UserTemplateService::storageLocation() const
{
    if (workspaceTemplateFilePath.isEmpty())
        return globalTemplateFilePath;
    return QStringLiteral("global=%1; workspace=%2")
        .arg(globalTemplateFilePath, workspaceTemplateFilePath);
}

QString UserTemplateService::globalTemplateLocation() const
{
    return globalTemplateFilePath;
}

QString UserTemplateService::workspaceTemplateLocation() const
{
    return workspaceTemplateFilePath;
}

void UserTemplateService::setGlobalTemplateFilePath(const QString& filePath)
{
    globalTemplateFilePath = filePath.trimmed().isEmpty()
        ? defaultGlobalTemplatePath()
        : normalizePath(filePath);
}

void UserTemplateService::setWorkspaceRoot(const QString& workspaceRoot)
{
    workspaceTemplateFilePath = workspaceTemplatePathForRoot(workspaceRoot);
}

void UserTemplateService::setWorkspaceTemplateFilePath(const QString& filePath)
{
    workspaceTemplateFilePath = normalizePath(filePath);
}

UserTemplateLoadReport UserTemplateService::reload() const
{
    UserTemplateLoadReport report;
    mergeLoadedRecords(&report,
                       readTemplateFile(globalTemplateFilePath,
                                        UserTemplateScope::Global,
                                        &report),
                       globalTemplateFilePath,
                       UserTemplateScope::Global);
    mergeLoadedRecords(&report,
                       readTemplateFile(workspaceTemplateFilePath,
                                        UserTemplateScope::Workspace,
                                        &report),
                       workspaceTemplateFilePath,
                       UserTemplateScope::Workspace);
    report.valid = report.issues.isEmpty();
    return report;
}

QList<UserTemplateRecord> UserTemplateService::records() const
{
    return reload().records;
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
    return validateRecordsForSave(inputRecords, globalTemplateFilePath);
}

UserTemplateSaveReport UserTemplateService::setRecords(
    const QList<UserTemplateRecord>& inputRecords) const
{
    UserTemplateSaveReport report = validateRecords(inputRecords);
    if (!report.valid)
        return report;

    if (!writeRecordsToJsonFile(globalTemplateFilePath,
                                report.records,
                                &report.failureReason)) {
        report.valid = false;
        appendIssue(&report,
                    globalTemplateFilePath,
                    QString(),
                    QStringLiteral("storage"),
                    report.failureReason);
        return report;
    }
    return report;
}

UserTemplateSaveReport UserTemplateService::addOrUpdateRecord(
    const UserTemplateRecord& record) const
{
    QList<UserTemplateRecord> nextRecords =
        readTemplateFile(globalTemplateFilePath,
                         UserTemplateScope::Global,
                         nullptr);
    const QString targetId = normalizedTemplateId(record).toLower();
    bool updated = false;
    for (UserTemplateRecord& existing : nextRecords) {
        if (normalizedTemplateId(existing).toLower() == targetId) {
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
    const QList<UserTemplateRecord> currentRecords =
        readTemplateFile(globalTemplateFilePath,
                         UserTemplateScope::Global,
                         nullptr);
    for (const UserTemplateRecord& record : currentRecords) {
        if (normalizedTemplateId(record).toLower() == targetId) {
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
    if (!globalTemplateFilePath.isEmpty())
        QFile::remove(globalTemplateFilePath);
    if (!workspaceTemplateFilePath.isEmpty())
        QFile::remove(workspaceTemplateFilePath);
}
