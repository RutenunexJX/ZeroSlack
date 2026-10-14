#include "usertemplateservice.h"
#include <QSaveFile>
#include <QLockFile>
#include <QDateTime>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "codetemplateservice.h"
#include "templateinputservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCryptographicHash>

#include <algorithm>
#include <memory>

std::unique_ptr<UserTemplateService> UserTemplateService::instance = nullptr;

namespace {
QString templateFileVersion(const QString& path)
{
    const QFileInfo info(path);
    QString version = path + QLatin1Char('|') + QString::number(info.size())
        + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch())
        + QLatin1Char('|') + QString::number(info.metadataChangeTime().toMSecsSinceEpoch());
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        FILE_BASIC_INFO basic{};
        BY_HANDLE_FILE_INFORMATION identity{};
        if (GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)))
            version += QStringLiteral("|%1|%2").arg(basic.ChangeTime.QuadPart).arg(basic.LastWriteTime.QuadPart);
        if (GetFileInformationByHandle(handle, &identity))
            version += QStringLiteral("|%1|%2|%3").arg(identity.dwVolumeSerialNumber)
                .arg(identity.nFileIndexHigh).arg(identity.nFileIndexLow);
        CloseHandle(handle);
    }
#endif
    return version;
}
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
    const auto isolated = qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    if (!isolated.isEmpty()) return QDir(isolated).filePath(QString::fromLatin1(kUserTemplateFileName));
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
    return length >= 0 && start <= text.size() && length <= text.size() - start;
}

bool slotRangeWithinText(int start, int length, const QString& text)
{
    return start >= 0 && rangeWithinText(start, length, text);
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
        object.insert(QStringLiteral("tabStop"), slot.tabStop);
        object.insert(QStringLiteral("visibleWhenEmpty"), slot.visibleWhenEmpty);
        if (!slot.input.isEmpty()) object.insert(QStringLiteral("input"), QJsonObject::fromVariantMap(slot.input));
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
        slot.tabStop = object.value(QStringLiteral("tabStop")).toInt(-1);
        slot.visibleWhenEmpty = object.value(QStringLiteral("visibleWhenEmpty")).toBool();
        slot.input = object.value(QStringLiteral("input")).toObject().toVariantMap();
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
    item.command = record.command;
    item.command.insert(QStringLiteral("id"), record.id);
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
    record.extraFields = object.toVariantMap();
    record.command = object.value(QStringLiteral("simulation")).toObject().toVariantMap();
    record.storageSource = source;
    record.id = object.value(QStringLiteral("id")).toString();
    record.commandToken =
        object.value(QString::fromLatin1(kUserTemplateCommand)).toString();
    if (record.commandToken.trimmed().isEmpty()) {
        record.commandToken =
            object.value(QString::fromLatin1(kUserTemplateCommandToken))
                .toString();
    }
    record.commandToken = normalizedCommandToken(record.commandToken);
    record.id = normalizedTemplateId(record);
    record.label = object.value(QStringLiteral("label")).toString(record.commandToken);
    record.description =
        object.value(QString::fromLatin1(kUserTemplateDescription)).toString();
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
    if (record.label.isEmpty()) record.label = record.commandToken;
    if (!record.command.value(QStringLiteral("file")).toString().isEmpty())
        record.command.insert(QStringLiteral("file"), normalizePath(record.command.value(QStringLiteral("file")).toString()));
    const QString issueCommand = record.commandToken.isEmpty()
        ? record.id
        : record.commandToken;

    const QString lowerId = record.id.toCaseFolded();
    const QString lowerCommand = record.commandToken.toCaseFolded() + QLatin1Char('\n')
        + normalizePath(record.command.value(QStringLiteral("file")).toString()).toCaseFolded();
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

    const bool simulation = QRegularExpression(QStringLiteral("^@[A-Za-z_][A-Za-z0-9_.:-]*$")).match(record.commandToken).hasMatch();
    if ((!simulation && (!record.commandToken.startsWith(QStringLiteral(";;")) || record.commandToken.size() <= 2))
        || commandTokenHasWhitespace(record.commandToken)) {
        appendIssue(report,
                    source,
                    issueCommand,
                    QStringLiteral("command"),
                    QStringLiteral("Use an @name simulation command or a compact legacy ;; token."));
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
    if (record.insertText.isEmpty() && record.command.value(QStringLiteral("symbol")).toString().isEmpty()
        && record.command.value(QStringLiteral("kind")).toString() != QStringLiteral("print")
        && !record.command.value(QStringLiteral("disabled")).toBool()) {
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
    auto configuredInputs = record.templateSlots;
    const auto inputMap = record.command.value(QStringLiteral("inputs")).toMap();
    for (auto it = inputMap.cbegin(); it != inputMap.cend(); ++it) {
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(it.key()).hasMatch())
            appendIssue(report, source, issueCommand, QStringLiteral("inputs"), QStringLiteral("Input names must be identifiers."));
        configuredInputs.append({it.key(), 0, 0, -1, true, it.value().toMap()});
    }
    const auto dependencyIssue = TemplateInputService::dependencyError(configuredInputs);
    if (!dependencyIssue.isEmpty()) appendIssue(report, source, issueCommand, QStringLiteral("inputs"), dependencyIssue);
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
            validRecord.commandToken.trimmed().toCaseFolded() + QLatin1Char('\n')
            + validRecord.command.value(QStringLiteral("file")).toString().toCaseFolded();
        const auto existing =
            std::find_if(report->records.begin(),
                         report->records.end(),
                         [&lowerCommand](const UserTemplateRecord& existingRecord) {
                             return existingRecord.commandToken.trimmed().toCaseFolded() + QLatin1Char('\n')
                                 + existingRecord.command.value(QStringLiteral("file")).toString().toCaseFolded()
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
    QJsonObject object = QJsonObject::fromVariantMap(record.extraFields);
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("label"), record.label);
    object.insert(QStringLiteral("selectionStart"), record.selectionStart);
    object.insert(QStringLiteral("selectionLength"), record.selectionLength);
    if (!record.command.isEmpty()) object.insert(QStringLiteral("simulation"), QJsonObject::fromVariantMap(record.command));
    else object.remove(QStringLiteral("simulation"));
    object.insert(QString::fromLatin1(kUserTemplateCommand), record.commandToken);
    object.insert(QString::fromLatin1(kUserTemplateDescription),
                  record.description);
    object.insert(QString::fromLatin1(kUserTemplateBody), record.insertText);
    object.remove(QString::fromLatin1(kUserTemplateSlots));
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
    QFile previous(filePath);
    if (previous.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(previous.readAll()).object();
    previous.close();
    root.insert(QString::fromLatin1(kUserTemplateTemplates), array);
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (failureReason)
            *failureReason = QStringLiteral("Failed to write user template file.");
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (failureReason) *failureReason = QStringLiteral("Failed to commit user templates atomically: %1").arg(file.errorString());
        return false;
    }
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
    cacheValid = false;
}

void UserTemplateService::setWorkspaceRoot(const QString& workspaceRoot)
{
    workspaceTemplateFilePath = workspaceTemplatePathForRoot(workspaceRoot);
    cacheValid = false;
}

void UserTemplateService::setWorkspaceTemplateFilePath(const QString& filePath)
{
    workspaceTemplateFilePath = normalizePath(filePath);
    cacheValid = false;
}

UserTemplateLoadReport UserTemplateService::reload() const
{
    const auto before = templateFileVersion(globalTemplateFilePath) + QLatin1Char('\n') + templateFileVersion(workspaceTemplateFilePath);
    fileReads += QFileInfo(globalTemplateFilePath).isFile() + QFileInfo(workspaceTemplateFilePath).isFile();
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
    cachedReport = report;
    cachedCatalog.clear();
    for (const auto& record : report.records) cachedCatalog.append(itemFromRecord(record));
    cachedFileVersion = before;
    cacheValid = before == templateFileVersion(globalTemplateFilePath) + QLatin1Char('\n') + templateFileVersion(workspaceTemplateFilePath);
    ++revision;
    return report;
}

void UserTemplateService::ensureLoaded() const
{
    const auto current = templateFileVersion(globalTemplateFilePath) + QLatin1Char('\n') + templateFileVersion(workspaceTemplateFilePath);
    if (!cacheValid || cachedFileVersion != current) reload();
}

quint64 UserTemplateService::catalogRevision() const { ensureLoaded(); return revision; }

QList<UserTemplateRecord> UserTemplateService::records() const
{
    ensureLoaded();
    return cachedReport.records;
}

QList<CodeTemplateItem> UserTemplateService::catalog() const
{
    ensureLoaded();
    return cachedCatalog;
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
    cacheValid = false;
    return report;
}

UserTemplateSaveReport UserTemplateService::addOrUpdateRecord(
    const UserTemplateRecord& record) const
{
    UserTemplateLoadReport loaded;
    QList<UserTemplateRecord> nextRecords =
        readTemplateFile(globalTemplateFilePath,
                         UserTemplateScope::Global,
                         &loaded);
    if (!loaded.issues.isEmpty()) return loaded;
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
    UserTemplateLoadReport loaded;
    const QList<UserTemplateRecord> currentRecords =
        readTemplateFile(globalTemplateFilePath,
                         UserTemplateScope::Global,
                         &loaded);
    if (!loaded.issues.isEmpty()) return false;
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
    cacheValid = false;
    if (!globalTemplateFilePath.isEmpty())
        QFile::remove(globalTemplateFilePath);
    if (!workspaceTemplateFilePath.isEmpty())
        QFile::remove(workspaceTemplateFilePath);
}

QList<UserTemplateRecord> UserTemplateService::recordsForFile(const QString& fileName, bool simulationOnly) const
{
    auto result = simulationBuiltins();
    QHash<QString, int> priorities;
    for (const auto& record : result) priorities.insert(record.commandToken.toCaseFolded(), 0);
    const auto file = normalizePath(fileName);
    for (const auto& record : records()) {
        const auto local = record.command.value(QStringLiteral("file")).toString();
        if (!local.isEmpty() && normalizePath(local).compare(file, Qt::CaseInsensitive) != 0) continue;
        if (simulationOnly && !record.commandToken.startsWith(QLatin1Char('@'))) continue;
        const int priority = !local.isEmpty() ? 3 : record.storageSource == workspaceTemplateFilePath ? 2 : 1;
        const auto key = record.commandToken.toCaseFolded();
        if (priorities.contains(key) && priorities.value(key) > priority) continue;
        auto it = std::find_if(result.begin(), result.end(), [&](const auto& previous) { return previous.commandToken.toCaseFolded() == key; });
        if (it == result.end()) result.append(record); else *it = record;
        priorities.insert(key, priority);
    }
    return result;
}

QList<CodeTemplateItem> UserTemplateService::catalogForFile(const QString& fileName, bool simulationOnly) const
{
    QList<CodeTemplateItem> result;
    for (const auto& record : recordsForFile(fileName, simulationOnly))
        if (!record.command.value(QStringLiteral("disabled")).toBool()) result << itemFromRecord(record);
    return result;
}

UserTemplateSaveReport UserTemplateService::saveRecord(const UserTemplateRecord& record, bool workspace, quint64 expectedRevision) const
{
    const auto path = workspace ? workspaceTemplateFilePath : globalTemplateFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    UserTemplateLoadReport loaded;
    if (!lock.tryLock(0)) { loaded.failureReason = QStringLiteral("Another operation is updating Templates. Retry after it finishes."); return loaded; }
    if (expectedRevision && catalogRevision() != expectedRevision) {
        loaded.failureReason = QStringLiteral("Templates changed before this write. Reopen the manager; the current configuration was retained.");
        return loaded;
    }
    auto next = readTemplateFile(path, workspace ? UserTemplateScope::Workspace : UserTemplateScope::Global, &loaded);
    if (!loaded.issues.isEmpty()) return loaded;
    const auto id = normalizedTemplateId(record);
    auto found = std::find_if(next.begin(), next.end(), [&](const auto& previous) { return previous.id == id; });
    if (found == next.end()) next << record; else *found = record;
    auto report = validateRecordsForSave(next, path);
    if (report.valid && !writeRecordsToJsonFile(path, report.records, &report.failureReason)) report.valid = false;
    if (report.valid) cacheValid = false;
    return report;
}

UserTemplateSaveReport UserTemplateService::deleteRecord(const QString& id, bool workspace, quint64 expectedRevision) const
{
    const auto path = workspace ? workspaceTemplateFilePath : globalTemplateFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    UserTemplateLoadReport loaded;
    if (!lock.tryLock(0)) { loaded.failureReason = QStringLiteral("Another operation is updating Templates. Retry after it finishes."); return loaded; }
    if (expectedRevision && catalogRevision() != expectedRevision) {
        loaded.failureReason = QStringLiteral("Templates changed before this write. Reopen the manager; the current configuration was retained.");
        return loaded;
    }
    auto next = readTemplateFile(path, workspace ? UserTemplateScope::Workspace : UserTemplateScope::Global, &loaded);
    if (!loaded.issues.isEmpty()) return loaded;
    next.erase(std::remove_if(next.begin(), next.end(), [&](const auto& record) { return record.id == id; }), next.end());
    auto report = validateRecordsForSave(next, path);
    if (report.valid && !writeRecordsToJsonFile(path, report.records, &report.failureReason)) report.valid = false;
    if (report.valid) cacheValid = false;
    return report;
}

QList<UserTemplateRecord> UserTemplateService::simulationBuiltins()
{
    QList<UserTemplateRecord> result;
    const QStringList tokens{QStringLiteral("reset"), QStringLiteral("ramp"), QStringLiteral("random"), QStringLiteral("random_window")};
    const QStringList symbols{QStringLiteral("tb_reset_package::reset_driver::run"), QStringLiteral("tb_sequence_package::ramp_driver::run"),
        QStringLiteral("tb_random_package::random_driver::run"), QStringLiteral("tb_random_package::random_driver::window")};
    const QStringList descriptions{QStringLiteral("Assert and release a reset on selected clock edges"), QStringLiteral("Drive an incrementing sequence"),
        QStringLiteral("Drive a bounded random sequence"), QStringLiteral("Drive random values within an absolute time window")};
    for (int i = 0; i < tokens.size(); ++i) {
        UserTemplateRecord record;
        record.id = QStringLiteral("builtin:") + tokens[i]; record.commandToken = QLatin1Char('@') + tokens[i]; record.label = record.commandToken;
        record.description = descriptions[i]; record.storageSource = QStringLiteral("builtin");
        record.command = {{QStringLiteral("kind"), QStringLiteral("driver")}, {QStringLiteral("symbol"), symbols[i]},
            {QStringLiteral("builtinAsset"), QStringLiteral("tb-behaviors")},
            {QStringLiteral("summary"), i == 0 ? QStringLiteral("reset · cycles") : i == 1 ? QStringLiteral("signal · range · step") : i == 2 ? QStringLiteral("signal · range · cycles") : QStringLiteral("signal · range · time")}};
        result << record;
    }
    UserTemplateRecord print;
    print.id = QStringLiteral("builtin:print"); print.commandToken = print.label = QStringLiteral("@print"); print.storageSource = QStringLiteral("builtin");
    print.description = QStringLiteral("Emit a structured simulation event with runtime values");
    print.command = {{QStringLiteral("kind"), QStringLiteral("print")}, {QStringLiteral("summary"), QStringLiteral("message · values · condition")}};
    result << print;
    const auto container = [&](const QString& token, const QString& description, const QString& text, const QString& content, const QString& placement, int start, int length) {
        UserTemplateRecord record;
        record.id = QStringLiteral("builtin:") + token; record.commandToken = record.label = QLatin1Char('@') + token;
        record.description = description; record.storageSource = QStringLiteral("builtin"); record.insertText = text;
        record.command = {{"kind", "snippet"}, {"content", content}, {"placement", placement}, {"summary", "body · mixed code and child templates"}};
        record.templateSlots << CodeTemplateSlot{"body", start, length, 1, true, {{"kind", "Code"}, {"content", content == "declarations" && token == "definitions" ? "declarations" : "statements"}, {"allowEmpty", true}}};
        result << record;
    };
    container("definitions", "Module declaration region", "\n  \n", "declarations", "module", 1, 2);
    container("process", "Simulation process with a code container", "initial begin\n  \nend\n", "process", "module", 14, 2);
    container("repeat", "Repeat a simulation statement sequence", "repeat (3) begin\n  \nend\n", "statements", "statement", 17, 2);
    result.last().templateSlots.first().tabStop = 2;
    result.last().templateSlots.prepend(CodeTemplateSlot{"count", 8, 1, 1, true, {{"kind", "Value"}, {"integer", true}, {"minimum", "0"}}});
    return result;
}

UserTemplateSaveReport UserTemplateService::migrateXipsCommands(const QString& metadataPath, const QVariantMap& asset) const
{
    UserTemplateSaveReport report;
    QFile input(metadataPath);
    if (!input.open(QIODevice::ReadOnly)) { report.failureReason = input.errorString(); return report; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
    const auto commands = document.object().value(QStringLiteral("commands"));
    if (parse.error != QJsonParseError::NoError || !commands.isArray()) { report.failureReason = QStringLiteral("Invalid xIPs command metadata; original data was preserved."); return report; }
    const auto path = workspaceTemplateFilePath.isEmpty() ? globalTemplateFilePath : workspaceTemplateFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) { report.failureReason = QStringLiteral("Another operation is updating Templates."); return report; }
    auto next = readTemplateFile(path, UserTemplateScope::Workspace, &report);
    if (!report.issues.isEmpty()) return report;
    const auto root = QFileInfo(metadataPath).absolutePath();
    const auto origin = normalizePath(metadataPath);
    const auto identity = asset.value(QStringLiteral("assetId")).toString().isEmpty() ? origin
        : asset.value(QStringLiteral("library")).toString() + QLatin1Char('|') + asset.value(QStringLiteral("assetId")).toString();
    bool changed = false;
    const auto safeSource = [&](const QString& relative) {
        if (relative.isEmpty() || QDir::isAbsolutePath(relative)) return QString();
        const auto path = normalizePath(QDir(root).filePath(relative));
        const auto canonical = QFileInfo(path).canonicalFilePath();
        const auto base = QFileInfo(root).canonicalFilePath() + QLatin1Char('/');
        return canonical.startsWith(base, Qt::CaseInsensitive) && QFileInfo(path).isFile() ? path : QString();
    };
    for (const auto& value : commands.toArray()) {
        const auto entry = value.toObject();
        const auto mapping = entry.value(QStringLiteral("id")).toString();
        const auto symbol = entry.value(QStringLiteral("symbol")).toString();
        if (mapping.isEmpty() || symbol.isEmpty()) { report.failureReason = QStringLiteral("A legacy command has no mapping identity or symbol; no migration was committed."); return report; }
        const auto id = QStringLiteral("xips:") + QString::fromLatin1(QCryptographicHash::hash((identity + QLatin1Char('|') + mapping).toUtf8(), QCryptographicHash::Sha256).toHex());
        if (std::any_of(next.cbegin(), next.cend(), [&](const auto& existing) { return existing.id == id; })) continue;
        UserTemplateRecord record;
        record.id = id; record.label = entry.value(QStringLiteral("name")).toString();
        record.description = entry.value(QStringLiteral("description")).toString();
        QString token = record.label.trimmed();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.:-]*$")).match(token).hasMatch()) token = QStringLiteral("command_") + mapping.left(8);
        record.commandToken = QLatin1Char('@') + token;
        QVariantList files;
        for (const auto& file : entry.value(QStringLiteral("files")).toArray()) {
            const auto resolved = safeSource(file.toString());
            if (resolved.isEmpty()) { report.failureReason = QStringLiteral("A legacy dependency is missing or leaves its asset directory; no migration was committed."); return report; }
            files << resolved;
        }
        const auto source = safeSource(entry.value(QStringLiteral("file")).toString());
        if (source.isEmpty() || !files.contains(source)) { report.failureReason = QStringLiteral("The legacy declaration source is missing from its compilation inputs."); return report; }
        QStringList includes;
        for (const auto& value : entry.value(QStringLiteral("includeDirectories")).toArray()) {
            const auto relative = value.toString();
            const auto resolved = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
            const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
            if (relative.isEmpty() || QDir::isAbsolutePath(relative) || !QFileInfo(resolved).isDir()
                || (resolved != canonicalRoot && !resolved.startsWith(canonicalRoot + '/', Qt::CaseInsensitive))) {
                report.failureReason = QStringLiteral("A legacy include folder is missing or outside its asset directory."); return report;
            }
            includes << resolved;
        }
        record.command = {{QStringLiteral("kind"), QStringLiteral("call")}, {QStringLiteral("symbol"), symbol},
            {QStringLiteral("includeDirectories"), includes},
            {QStringLiteral("sourceFile"), source},
            {QStringLiteral("compileOrder"), files}, {QStringLiteral("asset"), asset},
            {QStringLiteral("aliases"), entry.value(QStringLiteral("aliases")).toVariant()},
            {QStringLiteral("migrationSource"), origin}, {QStringLiteral("migrationId"), mapping},
            {QStringLiteral("legacyMapping"), entry.toVariantMap()}};
        if (document.object().value(QStringLiteral("origin")).toString() == QStringLiteral("builtin")) {
            for (const auto& builtin : simulationBuiltins()) if (builtin.command.value(QStringLiteral("symbol")).toString() == symbol) {
                record.commandToken = builtin.commandToken; record.label = builtin.label; record.description = builtin.description;
                record.command.insert(QStringLiteral("kind"), QStringLiteral("driver"));
                record.command.insert(QStringLiteral("summary"), builtin.command.value(QStringLiteral("summary")));
            }
        }
        next << record; changed = true;
    }
    if (!changed) { report.valid = true; report.records = next; return report; }
    report = validateRecordsForSave(next, path);
    if (report.valid && !writeRecordsToJsonFile(path, report.records, &report.failureReason)) report.valid = false;
    if (report.valid) cacheValid = false;
    return report;
}

QVariantMap UserTemplateService::signalRoles() const
{
    QVariantMap roles;
    for (const auto& path : {globalTemplateFilePath, workspaceTemplateFilePath}) {
        if (path.isEmpty()) continue;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const auto values = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("signalRoles")).toObject().toVariantMap();
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                if (it.value().isNull()) roles.remove(it.key()); else roles.insert(it.key(), it.value());
            }
        }
    }
    return roles;
}
QString UserTemplateService::setSignalRoles(const QVariantMap& roles) const
{
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value().toString() != QStringLiteral("Clock") && it.value().toString() != QStringLiteral("Reset")) return QStringLiteral("Role must be Clock or Reset.");
    const auto path = workspaceTemplateFilePath.isEmpty() ? globalTemplateFilePath : workspaceTemplateFilePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return QStringLiteral("Another operation is updating Templates.");
    QJsonObject root;
    if (QFileInfo::exists(path)) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return file.errorString();
        QJsonParseError error; const auto data = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) return QStringLiteral("The template file is invalid; its contents were preserved.");
        root = data.isArray() ? QJsonObject{{QStringLiteral("templates"), data.array()}} : data.object();
    }
    if (!root.contains(QStringLiteral("templates"))) root.insert(QStringLiteral("templates"), QJsonArray{});
    auto storedRoles = QJsonObject::fromVariantMap(roles);
    if (!workspaceTemplateFilePath.isEmpty()) {
        QFile global(globalTemplateFilePath);
        if (global.open(QIODevice::ReadOnly)) {
            const auto inherited = QJsonDocument::fromJson(global.readAll()).object().value(QStringLiteral("signalRoles")).toObject();
            for (auto it = inherited.begin(); it != inherited.end(); ++it)
                if (!roles.contains(it.key())) storedRoles.insert(it.key(), QJsonValue::Null);
        }
    }
    root.insert(QStringLiteral("signalRoles"), storedRoles);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return QStringLiteral("Cannot create the template folder.");
    QSaveFile file(path); file.setDirectWriteFallback(false); const auto bytes = QJsonDocument(root).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return file.errorString();
    cacheValid = false;
    return {};
}
