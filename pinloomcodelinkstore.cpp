#include "pinloomcodelinkstore.h"

#include "editorfileidentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace {
constexpr int kContextLength = 96;
const QString kSchema = QStringLiteral("ZeroSlack.PinloomCodeLinks");

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

QString textHash(const QString& text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

QPair<int, int> lineColumnAt(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    int line = 1;
    int lineStart = 0;
    for (int i = 0; i < bounded; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++line;
            lineStart = i + 1;
        }
    }
    return {line, bounded - lineStart + 1};
}

int positionAtLineColumn(const QString& text, int line, int column)
{
    if (line < 1 || column < 1)
        return -1;
    int currentLine = 1;
    int position = 0;
    while (currentLine < line && position < text.size()) {
        const int newline = text.indexOf(QLatin1Char('\n'), position);
        if (newline < 0)
            return -1;
        position = newline + 1;
        ++currentLine;
    }
    const int lineEnd = text.indexOf(QLatin1Char('\n'), position);
    const int maximum = lineEnd < 0 ? text.size() : lineEnd;
    const int result = position + column - 1;
    return result <= maximum ? result : -1;
}

int matchingPrefixLength(const QString& expected, const QString& actual)
{
    const int maximum = qMin(expected.size(), actual.size());
    int count = 0;
    while (count < maximum && expected.at(count) == actual.at(count))
        ++count;
    return count;
}

int matchingSuffixLength(const QString& expected, const QString& actual)
{
    const int maximum = qMin(expected.size(), actual.size());
    int count = 0;
    while (count < maximum
           && expected.at(expected.size() - 1 - count)
                  == actual.at(actual.size() - 1 - count)) {
        ++count;
    }
    return count;
}

QJsonObject sourceToJson(const PinloomSourceSelection& source)
{
    return QJsonObject::fromVariantMap(source.toVariantMap());
}

PinloomCodeLinkRecord recordFromJson(const QJsonObject& object)
{
    PinloomCodeLinkRecord record;
    record.id = object.value(QStringLiteral("id")).toString().trimmed();
    record.title = object.value(QStringLiteral("title")).toString();
    record.uri = QUrl(object.value(QStringLiteral("uri")).toString());
    record.identity = object.value(QStringLiteral("identity"))
                          .toObject().toVariantMap();
    record.source = PinloomSourceSelection::fromVariantMap(
        object.value(QStringLiteral("source")).toObject().toVariantMap());
    record.createdAtUtc =
        object.value(QStringLiteral("createdAtUtc")).toString();
    return record;
}

QJsonObject recordToJson(const PinloomCodeLinkRecord& record)
{
    return {
        {QStringLiteral("id"), record.id},
        {QStringLiteral("title"), record.title},
        {QStringLiteral("uri"),
         record.uri.toString(QUrl::FullyEncoded)},
        {QStringLiteral("identity"),
         QJsonObject::fromVariantMap(record.identity)},
        {QStringLiteral("source"), sourceToJson(record.source)},
        {QStringLiteral("createdAtUtc"), record.createdAtUtc},
    };
}

bool recordMatchesFile(const PinloomCodeLinkRecord& record,
                       const QString& workspaceRoot,
                       const QString& filePath)
{
    const QString normalizedFile = normalizedPath(filePath);
    if (!workspaceRoot.isEmpty()) {
        const QString relative = normalizedPath(
            QDir(workspaceRoot).relativeFilePath(normalizedFile));
        if (!relative.startsWith(QStringLiteral("../"))
            && relative != QStringLiteral("..")) {
            return relative.compare(record.source.relativeFilePath,
                                    Qt::CaseInsensitive)
                == 0;
        }
    }
    return EditorFileIdentity::same(
        normalizedFile, record.source.absoluteFilePath);
}

ResolvedPinloomCodeLink resolveRecord(const PinloomCodeLinkRecord& record,
                                      const QString& text)
{
    ResolvedPinloomCodeLink result;
    result.record = record;
    const QString& needle = record.source.selectedText;
    if (needle.isEmpty())
        return result;

    const int storedStart = positionAtLineColumn(
        text, record.source.startLine, record.source.startColumn);
    if (storedStart >= 0
        && text.mid(storedStart, needle.size()) == needle) {
        result.resolution = PinloomCodeLinkResolution::Exact;
        result.startPosition = storedStart;
        result.endPosition = storedStart + needle.size();
    } else {
        struct Candidate {
            int position = -1;
            int contextScore = 0;
            int distance = 0;
        };
        QList<Candidate> candidates;
        int from = 0;
        while (from <= text.size()) {
            const int position = text.indexOf(needle, from);
            if (position < 0)
                break;
            const QString prefix = text.mid(
                qMax(0, position - kContextLength),
                qMin(kContextLength, position));
            const int suffixStart = position + needle.size();
            const QString suffix = text.mid(suffixStart, kContextLength);
            Candidate candidate;
            candidate.position = position;
            candidate.contextScore =
                matchingSuffixLength(record.source.prefixContext, prefix)
                + matchingPrefixLength(record.source.suffixContext, suffix);
            candidate.distance = storedStart < 0
                ? std::numeric_limits<int>::max()
                : qAbs(position - storedStart);
            candidates.append(candidate);
            from = position + qMax(1, needle.size());
        }

        if (candidates.isEmpty())
            return result;
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& left, const Candidate& right) {
            if (left.contextScore != right.contextScore)
                return left.contextScore > right.contextScore;
            return left.distance < right.distance;
        });
        if (candidates.size() > 1
            && candidates.at(0).contextScore
                   == candidates.at(1).contextScore
            && candidates.at(0).distance
                   == candidates.at(1).distance) {
            result.resolution = PinloomCodeLinkResolution::Ambiguous;
            return result;
        }
        result.resolution = PinloomCodeLinkResolution::Moved;
        result.startPosition = candidates.first().position;
        result.endPosition = result.startPosition + needle.size();
    }

    const QPair<int, int> start = lineColumnAt(text, result.startPosition);
    const QPair<int, int> end = lineColumnAt(text, result.endPosition);
    result.firstLine = start.first - 1;
    result.lastLine = end.first - 1;
    return result;
}
}

bool PinloomSourceSelection::isValid() const
{
    const QString relative = normalizedPath(relativeFilePath);
    return !workspaceRoot.trimmed().isEmpty()
        && !relative.isEmpty()
        && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative)
        && !absoluteFilePath.trimmed().isEmpty()
        && !selectedText.isEmpty()
        && startPosition >= 0
        && endPosition > startPosition
        && startLine > 0
        && startColumn > 0
        && endLine >= startLine
        && endColumn > 0;
}

QString PinloomSourceSelection::suggestedTitle() const
{
    const QStringList rows = selectedText.split(QLatin1Char('\n'));
    for (const QString& row : rows) {
        const QString compact = row.simplified();
        if (!compact.isEmpty())
            return compact.left(72);
    }
    return QFileInfo(absoluteFilePath).fileName()
        + QStringLiteral(":%1").arg(startLine);
}

QVariantMap PinloomSourceSelection::toVariantMap() const
{
    return {
        {QStringLiteral("workspaceRoot"), workspaceRoot},
        {QStringLiteral("relativeFilePath"), relativeFilePath},
        {QStringLiteral("absoluteFilePath"), absoluteFilePath},
        {QStringLiteral("moduleName"), moduleName},
        {QStringLiteral("selectedText"), selectedText},
        {QStringLiteral("selectedTextHash"), selectedTextHash},
        {QStringLiteral("prefixContext"), prefixContext},
        {QStringLiteral("suffixContext"), suffixContext},
        {QStringLiteral("startPosition"), startPosition},
        {QStringLiteral("endPosition"), endPosition},
        {QStringLiteral("startLine"), startLine},
        {QStringLiteral("startColumn"), startColumn},
        {QStringLiteral("endLine"), endLine},
        {QStringLiteral("endColumn"), endColumn},
    };
}

PinloomSourceSelection PinloomSourceSelection::fromVariantMap(
    const QVariantMap& map)
{
    PinloomSourceSelection source;
    source.workspaceRoot = map.value(QStringLiteral("workspaceRoot")).toString();
    source.relativeFilePath =
        map.value(QStringLiteral("relativeFilePath")).toString();
    source.absoluteFilePath =
        map.value(QStringLiteral("absoluteFilePath")).toString();
    source.moduleName = map.value(QStringLiteral("moduleName")).toString();
    source.selectedText = map.value(QStringLiteral("selectedText")).toString();
    source.selectedTextHash =
        map.value(QStringLiteral("selectedTextHash")).toString();
    source.prefixContext =
        map.value(QStringLiteral("prefixContext")).toString();
    source.suffixContext =
        map.value(QStringLiteral("suffixContext")).toString();
    source.startPosition = map.value(QStringLiteral("startPosition"), -1).toInt();
    source.endPosition = map.value(QStringLiteral("endPosition"), -1).toInt();
    source.startLine = map.value(QStringLiteral("startLine")).toInt();
    source.startColumn = map.value(QStringLiteral("startColumn")).toInt();
    source.endLine = map.value(QStringLiteral("endLine")).toInt();
    source.endColumn = map.value(QStringLiteral("endColumn")).toInt();
    return source;
}

PinloomSourceSelection PinloomSourceSelection::fromDocumentSelection(
    const QString& workspaceRootValue,
    const QString& filePath,
    const QString& moduleNameValue,
    const QString& documentText,
    int selectionStart,
    int selectionEnd)
{
    PinloomSourceSelection source;
    source.workspaceRoot = normalizedPath(workspaceRootValue);
    source.absoluteFilePath = normalizedPath(filePath);
    source.moduleName = moduleNameValue.trimmed();
    source.startPosition = qBound(0, selectionStart, documentText.size());
    source.endPosition = qBound(
        source.startPosition, selectionEnd, documentText.size());
    if (!source.workspaceRoot.isEmpty()) {
        source.relativeFilePath = normalizedPath(
            QDir(source.workspaceRoot).relativeFilePath(
                source.absoluteFilePath));
    }
    source.selectedText = documentText.mid(
        source.startPosition,
        source.endPosition - source.startPosition);
    source.selectedTextHash = textHash(source.selectedText);
    source.prefixContext = documentText.mid(
        qMax(0, source.startPosition - kContextLength),
        qMin(kContextLength, source.startPosition));
    source.suffixContext = documentText.mid(
        source.endPosition, kContextLength);
    const QPair<int, int> start =
        lineColumnAt(documentText, source.startPosition);
    const QPair<int, int> end =
        lineColumnAt(documentText, source.endPosition);
    source.startLine = start.first;
    source.startColumn = start.second;
    source.endLine = end.first;
    source.endColumn = end.second;
    return source;
}

bool PinloomCodeLinkRecord::isValid() const
{
    return !id.trimmed().isEmpty()
        && uri.isValid()
        && uri.scheme() == QStringLiteral("pinloom")
        && source.isValid();
}

void PinloomCodeLinkStore::setWorkspaceRoot(
    const QString& workspaceRootValue)
{
    const QString normalized = normalizedPath(workspaceRootValue);
    if (root == normalized)
        return;
    root = normalized;
    linkRecords.clear();
    load();
}

QString PinloomCodeLinkStore::workspaceRoot() const
{
    return root;
}

QString PinloomCodeLinkStore::storagePath() const
{
    return root.isEmpty()
        ? QString()
        : QDir(root).filePath(
              QStringLiteral(".zeroslack/pinloom-links.json"));
}

bool PinloomCodeLinkStore::addLink(
    const PinloomSourceSelection& source,
    const QUrl& uri,
    const QString& title,
    const QVariantMap& identity,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!source.isValid()
        || !uri.isValid()
        || uri.scheme() != QStringLiteral("pinloom")) {
        if (failureReason)
            *failureReason = QStringLiteral("The source selection or Pinloom link is invalid.");
        return false;
    }
    if (root.isEmpty()
        || normalizedPath(source.workspaceRoot)
               .compare(root, Qt::CaseInsensitive) != 0) {
        if (failureReason)
            *failureReason = QStringLiteral("The source selection is outside the active workspace.");
        return false;
    }

    const auto duplicate = std::find_if(
        linkRecords.cbegin(), linkRecords.cend(),
        [&source, &uri](const PinloomCodeLinkRecord& candidate) {
            return candidate.uri == uri
                && candidate.source.relativeFilePath.compare(
                       source.relativeFilePath,
                       Qt::CaseInsensitive) == 0
                && candidate.source.selectedTextHash
                       == source.selectedTextHash
                && candidate.source.startLine == source.startLine
                && candidate.source.startColumn == source.startColumn;
        });
    if (duplicate != linkRecords.cend())
        return true;

    PinloomCodeLinkRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.title = title.trimmed().isEmpty()
        ? source.suggestedTitle()
        : title.trimmed();
    record.uri = uri;
    record.identity = identity;
    record.source = source;
    record.createdAtUtc =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    linkRecords.append(record);
    if (save(failureReason))
        return true;
    linkRecords.removeLast();
    return false;
}

QList<ResolvedPinloomCodeLink> PinloomCodeLinkStore::linksForDocument(
    const QString& filePath,
    const QString& documentText) const
{
    QList<ResolvedPinloomCodeLink> result;
    for (const PinloomCodeLinkRecord& record : linkRecords) {
        if (recordMatchesFile(record, root, filePath))
            result.append(resolveRecord(record, documentText));
    }
    return result;
}

QList<ResolvedPinloomCodeLink> PinloomCodeLinkStore::linksAtPosition(
    const QString& filePath,
    const QString& documentText,
    int position) const
{
    QList<ResolvedPinloomCodeLink> result;
    const QList<ResolvedPinloomCodeLink> links =
        linksForDocument(filePath, documentText);
    for (const ResolvedPinloomCodeLink& link : links) {
        if (link.available()
            && position >= link.startPosition
            && position < link.endPosition) {
            result.append(link);
        }
    }
    return result;
}

QList<PinloomCodeLinkRecord> PinloomCodeLinkStore::records() const
{
    return linkRecords;
}

bool PinloomCodeLinkStore::load(QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    linkRecords.clear();
    const QString path = storagePath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (failureReason)
            *failureReason = QStringLiteral("Pinloom code-link file is invalid JSON.");
        return false;
    }
    const QJsonObject rootObject = document.object();
    if (rootObject.value(QStringLiteral("schema")).toString() != kSchema
        || rootObject.value(QStringLiteral("version")).toInt() != kVersion) {
        if (failureReason)
            *failureReason = QStringLiteral("Pinloom code-link schema is unsupported.");
        return false;
    }
    const QJsonArray links = rootObject.value(QStringLiteral("links")).toArray();
    for (const QJsonValue& value : links) {
        if (!value.isObject())
            continue;
        const PinloomCodeLinkRecord record = recordFromJson(value.toObject());
        if (record.isValid())
            linkRecords.append(record);
    }
    return true;
}

bool PinloomCodeLinkStore::save(QString* failureReason) const
{
    if (failureReason)
        failureReason->clear();
    const QString path = storagePath();
    if (path.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("No workspace is active.");
        return false;
    }
    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (failureReason)
            *failureReason = QStringLiteral("Unable to create the ZeroSlack workspace metadata directory.");
        return false;
    }
    QJsonArray links;
    for (const PinloomCodeLinkRecord& record : linkRecords)
        links.append(recordToJson(record));
    const QJsonObject rootObject{
        {QStringLiteral("schema"), kSchema},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("links"), links},
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(rootObject).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (failureReason)
            *failureReason = file.errorString();
        return false;
    }
    return true;
}
