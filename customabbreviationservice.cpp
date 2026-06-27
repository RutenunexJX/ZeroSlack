#include "customabbreviationservice.h"

#include <QSettings>
#include <QSet>

#include <memory>

std::unique_ptr<CustomAbbreviationService> CustomAbbreviationService::instance =
    nullptr;

namespace {
constexpr const char* kCustomAbbreviationGroup = "customAbbreviations";
constexpr const char* kCustomAbbreviationItems = "items";
constexpr const char* kCustomAbbreviationId = "id";
constexpr const char* kCustomAbbreviationText = "abbreviation";
constexpr const char* kCustomAbbreviationCommandToken = "commandToken";
constexpr const char* kCustomAbbreviationLabel = "label";
constexpr const char* kCustomAbbreviationDescription = "description";

std::unique_ptr<QSettings> makeSettings(const QString& settingsFilePath)
{
    if (!settingsFilePath.isEmpty())
        return std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("ZeroSlack"),
                                       QStringLiteral("ZeroSlack"));
}

QString normalizedId(const CustomAbbreviationRecord& record)
{
    QString id = record.id.trimmed();
    if (!id.isEmpty())
        return id;
    return record.abbreviation.trimmed().toLower();
}

QString normalizedAbbreviation(const QString& abbreviation)
{
    return abbreviation.trimmed();
}

QString normalizedCommandToken(const QString& token)
{
    return token.trimmed();
}

bool hasWhitespace(const QString& text)
{
    for (const QChar ch : text) {
        if (ch.isSpace())
            return true;
    }
    return false;
}

bool hasCommandSurfaceMarker(const QString& text)
{
    return text.contains(QLatin1Char(';'))
        || text.contains(QLatin1Char('`'));
}

bool isSemanticCommandToken(const QString& token)
{
    return token.startsWith(QLatin1Char(';'))
        && !token.startsWith(QStringLiteral(";;"))
        && !token.startsWith(QStringLiteral(";:"))
        && token.size() > 1
        && !hasWhitespace(token);
}

bool isTemplateCommandToken(const QString& token)
{
    return token.startsWith(QStringLiteral(";;"))
        && !token.startsWith(QStringLiteral(";:"))
        && token.size() > 2
        && !hasWhitespace(token);
}

bool isSupportedCommandToken(const QString& token)
{
    return isSemanticCommandToken(token) || isTemplateCommandToken(token);
}

InlineCommandIntent intentForCommandToken(const QString& token)
{
    if (isTemplateCommandToken(token))
        return InlineCommandIntent::CodeTemplate;
    return InlineCommandIntent::SemanticCompletion;
}

bool tokenMatchesIntent(const QString& token, InlineCommandIntent intent)
{
    switch (intent) {
    case InlineCommandIntent::SemanticCompletion:
        return isSemanticCommandToken(token);
    case InlineCommandIntent::CodeTemplate:
        return isTemplateCommandToken(token);
    case InlineCommandIntent::EditorAction:
        return false;
    }
    return false;
}

void appendIssue(CustomAbbreviationSaveReport* report,
                 const QString& id,
                 const QString& field,
                 const QString& reason)
{
    if (!report)
        return;
    report->issues.append(CustomAbbreviationIssue{id, field, reason});
    if (report->failureReason.isEmpty())
        report->failureReason = reason;
}

bool abbreviationMatches(const QString& candidate, const QString& query)
{
    return QString::compare(candidate.trimmed(),
                            query.trimmed(),
                            Qt::CaseInsensitive) == 0;
}

bool abbreviationStartsWith(const QString& candidate, const QString& prefix)
{
    const QString normalizedPrefix = prefix.trimmed();
    return normalizedPrefix.isEmpty()
        || candidate.trimmed().startsWith(normalizedPrefix,
                                          Qt::CaseInsensitive);
}
}

CustomAbbreviationService* CustomAbbreviationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<CustomAbbreviationService>();
    return instance.get();
}

CustomAbbreviationService::CustomAbbreviationService(const QString& path)
    : settingsFilePath(path)
{
}

CustomAbbreviationService::~CustomAbbreviationService() = default;

QString CustomAbbreviationService::storageLocation() const
{
    if (!settingsFilePath.isEmpty())
        return settingsFilePath;
    return QStringLiteral("QSettings:ZeroSlack/ZeroSlack/customAbbreviations");
}

QList<CustomAbbreviationRecord> CustomAbbreviationService::records() const
{
    QList<CustomAbbreviationRecord> result;
    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kCustomAbbreviationGroup));
    const int count =
        settings->beginReadArray(QString::fromLatin1(kCustomAbbreviationItems));
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings->setArrayIndex(i);
        CustomAbbreviationRecord record;
        record.id =
            settings->value(QString::fromLatin1(kCustomAbbreviationId)).toString();
        record.abbreviation =
            settings->value(QString::fromLatin1(kCustomAbbreviationText)).toString();
        record.commandToken =
            settings->value(QString::fromLatin1(kCustomAbbreviationCommandToken)).toString();
        record.label =
            settings->value(QString::fromLatin1(kCustomAbbreviationLabel)).toString();
        record.description =
            settings->value(QString::fromLatin1(kCustomAbbreviationDescription)).toString();
        if (!record.id.trimmed().isEmpty())
            result.append(record);
    }
    settings->endArray();
    settings->endGroup();
    return result;
}

QList<CustomAbbreviationRecord> CustomAbbreviationService::matchingRecords(
    const QString& prefix) const
{
    QList<CustomAbbreviationRecord> result;
    for (const CustomAbbreviationRecord& record : records()) {
        if (abbreviationStartsWith(record.abbreviation, prefix)
            && isSupportedCommandToken(record.commandToken)) {
            result.append(record);
        }
    }
    return result;
}

QList<CustomAbbreviationRecord>
CustomAbbreviationService::matchingRecordsForIntent(
    const QString& prefix,
    InlineCommandIntent intent) const
{
    QList<CustomAbbreviationRecord> result;
    for (const CustomAbbreviationRecord& record : records()) {
        if (abbreviationStartsWith(record.abbreviation, prefix)
            && tokenMatchesIntent(record.commandToken, intent)) {
            result.append(record);
        }
    }
    return result;
}

CustomAbbreviationResolution CustomAbbreviationService::resolve(
    const QString& abbreviation) const
{
    for (const CustomAbbreviationRecord& record : records()) {
        if (!abbreviationMatches(record.abbreviation, abbreviation)
            || !isSupportedCommandToken(record.commandToken)) {
            continue;
        }

        CustomAbbreviationResolution resolution;
        resolution.matched = true;
        resolution.record = record;
        resolution.intent = intentForCommandToken(record.commandToken);
        return resolution;
    }
    return {};
}

CustomAbbreviationResolution CustomAbbreviationService::resolveForIntent(
    const QString& abbreviation,
    InlineCommandIntent intent) const
{
    for (const CustomAbbreviationRecord& record : records()) {
        if (!abbreviationMatches(record.abbreviation, abbreviation)
            || !tokenMatchesIntent(record.commandToken, intent)) {
            continue;
        }

        CustomAbbreviationResolution resolution;
        resolution.matched = true;
        resolution.record = record;
        resolution.intent = intent;
        return resolution;
    }
    return {};
}

CustomAbbreviationSaveReport CustomAbbreviationService::validateRecords(
    const QList<CustomAbbreviationRecord>& inputRecords) const
{
    CustomAbbreviationSaveReport report;
    QSet<QString> seenIds;
    QSet<QString> seenAbbreviations;
    report.records.reserve(inputRecords.size());

    for (const CustomAbbreviationRecord& input : inputRecords) {
        CustomAbbreviationRecord record = input;
        record.id = normalizedId(record);
        record.abbreviation = normalizedAbbreviation(record.abbreviation);
        record.commandToken = normalizedCommandToken(record.commandToken);
        record.label = record.label.trimmed();
        record.description = record.description.trimmed();

        if (record.id.isEmpty()) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("id"),
                        QStringLiteral("Abbreviation id is required."));
        }

        const QString lowerId = record.id.toLower();
        if (!lowerId.isEmpty() && seenIds.contains(lowerId)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("id"),
                        QStringLiteral("Abbreviation id must be unique."));
        }
        seenIds.insert(lowerId);

        if (record.abbreviation.isEmpty()
            || hasWhitespace(record.abbreviation)
            || hasCommandSurfaceMarker(record.abbreviation)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("abbreviation"),
                        QStringLiteral("Abbreviation must be a compact non-command token."));
        }

        const QString lowerAbbreviation = record.abbreviation.toLower();
        if (!lowerAbbreviation.isEmpty()
            && seenAbbreviations.contains(lowerAbbreviation)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("abbreviation"),
                        QStringLiteral("Abbreviation must be unique."));
        }
        seenAbbreviations.insert(lowerAbbreviation);

        if (!isSemanticCommandToken(record.commandToken)
            && !isTemplateCommandToken(record.commandToken)) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("commandToken"),
                        QStringLiteral("Command token must be a compact ; or ;; command token."));
        }
        if (record.commandToken.startsWith(QStringLiteral(";:"))) {
            appendIssue(&report,
                        record.id,
                        QStringLiteral("commandToken"),
                        QStringLiteral(";: is reserved and inactive."));
        }

        if (record.label.isEmpty())
            record.label = record.abbreviation;
        if (record.description.isEmpty())
            record.description = QStringLiteral("Custom abbreviation");

        report.records.append(record);
    }

    report.valid = report.issues.isEmpty();
    return report;
}

CustomAbbreviationSaveReport CustomAbbreviationService::setRecords(
    const QList<CustomAbbreviationRecord>& inputRecords) const
{
    CustomAbbreviationSaveReport report = validateRecords(inputRecords);
    if (!report.valid)
        return report;

    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kCustomAbbreviationGroup));
    settings->remove(QString());
    settings->beginWriteArray(QString::fromLatin1(kCustomAbbreviationItems));
    for (int i = 0; i < report.records.size(); ++i) {
        settings->setArrayIndex(i);
        const CustomAbbreviationRecord& record = report.records.at(i);
        settings->setValue(QString::fromLatin1(kCustomAbbreviationId),
                           record.id);
        settings->setValue(QString::fromLatin1(kCustomAbbreviationText),
                           record.abbreviation);
        settings->setValue(QString::fromLatin1(kCustomAbbreviationCommandToken),
                           record.commandToken);
        settings->setValue(QString::fromLatin1(kCustomAbbreviationLabel),
                           record.label);
        settings->setValue(QString::fromLatin1(kCustomAbbreviationDescription),
                           record.description);
    }
    settings->endArray();
    settings->endGroup();
    settings->sync();
    return report;
}

CustomAbbreviationSaveReport CustomAbbreviationService::addOrUpdateRecord(
    const CustomAbbreviationRecord& record) const
{
    QList<CustomAbbreviationRecord> nextRecords = records();
    const QString targetId = normalizedId(record).toLower();
    bool updated = false;
    for (CustomAbbreviationRecord& existing : nextRecords) {
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

bool CustomAbbreviationService::removeRecord(const QString& id) const
{
    const QString targetId = id.trimmed().toLower();
    if (targetId.isEmpty())
        return false;

    QList<CustomAbbreviationRecord> nextRecords;
    bool removed = false;
    for (const CustomAbbreviationRecord& record : records()) {
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

void CustomAbbreviationService::clear() const
{
    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    settings->beginGroup(QString::fromLatin1(kCustomAbbreviationGroup));
    settings->remove(QString());
    settings->endGroup();
    settings->sync();
}
