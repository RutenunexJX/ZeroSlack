#include "saferenameservice.h"

#include "definitionservice.h"
#include "semanticindexsnapshot.h"
#include "svtokenutils.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace {

QString normalizedSafeRenameFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

struct LineInfo {
    int line = -1;
    int column = -1;
    int lineStartPosition = -1;
    QString lineText;
};

LineInfo lineInfoAtPosition(const QString& text, int position)
{
    LineInfo info;
    const int bounded = qBound(0, position, text.size());
    int lineStart = 0;
    int lineNumber = 1;
    for (int i = 0; i < bounded; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++lineNumber;
            lineStart = i + 1;
        }
    }

    int lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
    if (lineEnd < 0)
        lineEnd = text.size();

    info.line = lineNumber;
    info.column = bounded - lineStart;
    info.lineStartPosition = lineStart;
    info.lineText = text.mid(lineStart, lineEnd - lineStart);
    return info;
}

bool isIdentifierCodePosition(const QString& text, int position)
{
    const LineInfo info = lineInfoAtPosition(text, position);
    if (info.column < 0 || info.column >= info.lineText.size())
        return false;

    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < info.column; ++i) {
        const QChar ch = info.lineText.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('/')
            && i + 1 < info.lineText.size()
            && info.lineText.at(i + 1) == QLatin1Char('/')) {
            return false;
        }
    }
    return !inString;
}

QList<int> identifierOccurrencePositions(const QString& text,
                                         const QString& symbolName)
{
    QList<int> positions;
    int pos = 0;
    while ((pos = SvTokenUtils::indexOfWord(
                text, symbolName, pos, true)) >= 0) {
        if (isIdentifierCodePosition(text, pos))
            positions.append(pos);
        pos += qMax(1, symbolName.size());
    }
    return positions;
}

QHash<QString, QString> availableFileContents(
    SemanticIndex* index,
    const SafeRenamePlanQuery& query)
{
    QHash<QString, QString> contents;
    auto insertContent = [&contents](const QString& fileName,
                                     const QString& text) {
        const QString normalized = normalizedSafeRenameFileName(fileName);
        if (normalized.isEmpty())
            return;
        contents.insert(normalized, text);
    };

    if (index) {
        if (const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
                index->snapshot()) {
            const QHash<QString, QString> snapshotContents =
                snapshot->fileContents();
            for (auto it = snapshotContents.constBegin();
                 it != snapshotContents.constEnd();
                 ++it) {
                insertContent(it.key(), it.value());
            }
        }

        QSet<QString> seen;
        for (const SemanticSymbolRecord& record : index->getSymbolRecords()) {
            const QString normalized =
                normalizedSafeRenameFileName(record.location.fileName);
            if (normalized.isEmpty() || seen.contains(normalized))
                continue;
            seen.insert(normalized);
            if (contents.contains(normalized))
                continue;
            const QString content =
                index->getCachedFileContent(record.location.fileName);
            if (!content.isEmpty())
                contents.insert(normalized, content);
        }
    }

    for (auto it = query.openFileContents.constBegin();
         it != query.openFileContents.constEnd();
         ++it) {
        insertContent(it.key(), it.value());
    }
    if (!query.fileName.isEmpty() || !query.documentText.isEmpty())
        insertContent(query.fileName, query.documentText);
    return contents;
}

DefinitionQuery definitionQueryForOccurrence(
    SemanticIndex* index,
    const QString& fileName,
    const QString& content,
    const QString& symbolName,
    int position,
    const SafeRenamePlanQuery& rootQuery)
{
    const LineInfo info = lineInfoAtPosition(content, position);
    DefinitionQuery query;
    query.symbolName = symbolName;
    query.fileName = fileName;
    query.cursorLine = info.line;
    query.cursorColumn = info.column;
    query.linePrefixBeforeCursor =
        info.lineText.left(qMax(0, info.column));

    const QString rootFile = normalizedSafeRenameFileName(rootQuery.fileName);
    const QString currentFile = normalizedSafeRenameFileName(fileName);
    if (!rootQuery.moduleName.isEmpty() && rootFile == currentFile)
        query.moduleName = rootQuery.moduleName;
    else if (index)
        query.moduleName = index->currentModuleAt(fileName, position);
    return query;
}

bool sameStableKey(const SymbolStableKey& lhs, const SymbolStableKey& rhs)
{
    return lhs.isValid() && rhs.isValid() && lhs == rhs;
}

bool definitionMatchesSubject(const DefinitionResult& result,
                              const SymbolStableKey& subjectStableKey)
{
    if (!result.found)
        return false;
    if (sameStableKey(result.symbolStableKey, subjectStableKey))
        return true;
    return sameStableKey(result.symbolRecord.stableKey, subjectStableKey);
}

QString contentForRecord(const QHash<QString, QString>& contents,
                         const SemanticSymbolRecord& record)
{
    return contents.value(
        normalizedSafeRenameFileName(record.location.fileName));
}

int lineStartPosition(const QString& text, int oneBasedLine)
{
    if (oneBasedLine <= 1)
        return 0;
    int line = 1;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++line;
            if (line == oneBasedLine)
                return i + 1;
        }
    }
    return -1;
}

int recordNamePositionInContent(const SemanticSymbolRecord& record,
                                const QString& content)
{
    if (record.name.isEmpty() || content.isEmpty())
        return -1;

    if (record.location.position >= 0
        && record.location.position + record.name.size() <= content.size()
        && SvTokenUtils::isWordAt(
            content, record.name, record.location.position, true)) {
        return record.location.position;
    }

    const int start = lineStartPosition(content, record.location.startLine);
    if (start < 0)
        return -1;
    int end = content.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
        end = content.size();
    const QString line = content.mid(start, end - start);

    const int preferred = qMax(0, record.location.startColumn - 1);
    int pos = SvTokenUtils::indexOfWord(line, record.name, preferred, true);
    if (pos >= 0)
        return start + pos;
    pos = SvTokenUtils::indexOfWord(line, record.name, 0, true);
    return pos >= 0 ? start + pos : -1;
}

void appendEdit(QList<SafeRenameTextEdit>* edits,
                const QString& fileName,
                int position,
                const QString& oldName,
                const QString& newName)
{
    if (!edits || position < 0 || oldName.isEmpty())
        return;

    SafeRenameTextEdit edit;
    edit.fileName = fileName;
    edit.startPosition = position;
    edit.length = oldName.size();
    edit.oldText = oldName;
    edit.newText = newName;
    if (edit.isValid())
        edits->append(edit);
}

QList<SafeRenameFileEdits> groupEditsByFile(
    const QList<SafeRenameTextEdit>& edits)
{
    QList<SafeRenameFileEdits> result;
    QHash<QString, int> indexByFile;
    QSet<QString> seenRanges;

    for (const SafeRenameTextEdit& edit : edits) {
        if (!edit.isValid())
            continue;
        const QString normalized =
            normalizedSafeRenameFileName(edit.fileName);
        const QString dedupeKey =
            QStringLiteral("%1:%2:%3")
                .arg(normalized)
                .arg(edit.startPosition)
                .arg(edit.length);
        if (seenRanges.contains(dedupeKey))
            continue;
        seenRanges.insert(dedupeKey);

        if (!indexByFile.contains(normalized)) {
            SafeRenameFileEdits fileEdits;
            fileEdits.fileName = edit.fileName;
            indexByFile.insert(normalized, result.size());
            result.append(fileEdits);
        }
        result[indexByFile.value(normalized)].edits.append(edit);
    }

    for (SafeRenameFileEdits& fileEdits : result) {
        std::sort(fileEdits.edits.begin(),
                  fileEdits.edits.end(),
                  [](const SafeRenameTextEdit& lhs,
                     const SafeRenameTextEdit& rhs) {
            return lhs.startPosition < rhs.startPosition;
        });
    }
    return result;
}

QList<SemanticSymbolRecord> conflictingDefinitionsForName(
    SemanticIndex* index,
    const QString& newName,
    const SymbolStableKey& subjectStableKey,
    const SafeRenamePlanQuery& query)
{
    QList<SemanticSymbolRecord> conflicts;
    if (!index || newName.isEmpty())
        return conflicts;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    const QList<SemanticSymbolRecord> candidates =
        index->findDefinitionRecords(newName, context);
    for (const SemanticSymbolRecord& candidate : candidates) {
        if (sameStableKey(candidate.stableKey, subjectStableKey))
            continue;
        conflicts.append(candidate);
    }
    return conflicts;
}

QString statusMessage(SafeRenamePlanStatus status)
{
    switch (status) {
    case SafeRenamePlanStatus::Ready:
        return QStringLiteral("Rename plan ready.");
    case SafeRenamePlanStatus::InvalidSymbol:
        return QStringLiteral("Select a valid SystemVerilog identifier.");
    case SafeRenamePlanStatus::InvalidNewName:
        return QStringLiteral("Enter a valid SystemVerilog identifier.");
    case SafeRenamePlanStatus::NoChange:
        return QStringLiteral("The new name is unchanged.");
    case SafeRenamePlanStatus::DefinitionNotFound:
        return QStringLiteral("No definition was found for the selected symbol.");
    case SafeRenamePlanStatus::ConflictingDefinition:
        return QStringLiteral("The new name conflicts with an existing definition.");
    case SafeRenamePlanStatus::MissingFileContent:
        return QStringLiteral("One or more referenced files are not available for rename.");
    case SafeRenamePlanStatus::AmbiguousEditRange:
        return QStringLiteral("A rename location could not be mapped safely.");
    }
    return QStringLiteral("Rename unavailable.");
}

} // namespace

int SafeRenamePlan::editCount() const
{
    int count = 0;
    for (const SafeRenameFileEdits& edits : fileEdits)
        count += edits.edits.size();
    return count;
}

SafeRenameService::SafeRenameService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

void SafeRenameService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SafeRenamePlan SafeRenameService::createRenamePlan(
    const SafeRenamePlanQuery& query) const
{
    SafeRenamePlan plan;
    plan.symbolName = query.symbolName.trimmed();
    plan.newName = query.newName.trimmed();

    if (!isValidIdentifier(plan.symbolName)) {
        plan.status = SafeRenamePlanStatus::InvalidSymbol;
        plan.message = statusMessage(plan.status);
        return plan;
    }
    if (!isValidIdentifier(plan.newName)) {
        plan.status = SafeRenamePlanStatus::InvalidNewName;
        plan.message = statusMessage(plan.status);
        return plan;
    }
    if (plan.symbolName == plan.newName) {
        plan.status = SafeRenamePlanStatus::NoChange;
        plan.message = statusMessage(plan.status);
        return plan;
    }

    SemanticIndex* idx = semanticIndex();
    DefinitionService definitionService(idx);
    DefinitionQuery subjectQuery =
        definitionQueryForOccurrence(idx,
                                     query.fileName,
                                     query.documentText,
                                     plan.symbolName,
                                     qMax(0, query.cursorPosition),
                                     query);
    const DefinitionResult subject = definitionService.resolveDefinition(subjectQuery);
    if (!subject.found || !subject.symbolStableKey.isValid()) {
        plan.status = SafeRenamePlanStatus::DefinitionNotFound;
        plan.message = statusMessage(plan.status);
        return plan;
    }

    plan.subjectRecord = subject.symbolRecord;
    plan.subjectStableKey = subject.symbolStableKey;

    plan.conflictingDefinitions =
        conflictingDefinitionsForName(idx, plan.newName, plan.subjectStableKey, query);
    if (!query.forceConflicts && !plan.conflictingDefinitions.isEmpty()) {
        plan.status = SafeRenamePlanStatus::ConflictingDefinition;
        plan.message = statusMessage(plan.status);
        return plan;
    }

    const QHash<QString, QString> contents =
        availableFileContents(idx, query);
    if (!plan.subjectRecord.location.fileName.isEmpty()
        && contentForRecord(contents, plan.subjectRecord).isEmpty()) {
        plan.status = SafeRenamePlanStatus::MissingFileContent;
        plan.message = statusMessage(plan.status);
        return plan;
    }

    QList<SafeRenameTextEdit> edits;
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
        const QString fileName = it.key();
        const QString content = it.value();
        const QList<int> positions =
            identifierOccurrencePositions(content, plan.symbolName);
        for (int position : positions) {
            const DefinitionQuery occurrenceQuery =
                definitionQueryForOccurrence(idx,
                                             fileName,
                                             content,
                                             plan.symbolName,
                                             position,
                                             query);
            const DefinitionResult result =
                definitionService.resolveDefinition(occurrenceQuery);
            if (!definitionMatchesSubject(result, plan.subjectStableKey))
                continue;
            appendEdit(&edits, fileName, position, plan.symbolName, plan.newName);
        }
    }

    const QString subjectContent = contentForRecord(contents, plan.subjectRecord);
    const int subjectPosition =
        recordNamePositionInContent(plan.subjectRecord, subjectContent);
    if (subjectPosition < 0) {
        plan.status = SafeRenamePlanStatus::AmbiguousEditRange;
        plan.message = statusMessage(plan.status);
        return plan;
    }
    appendEdit(&edits,
               plan.subjectRecord.location.fileName,
               subjectPosition,
               plan.symbolName,
               plan.newName);

    plan.fileEdits = groupEditsByFile(edits);
    if (plan.editCount() <= 0) {
        plan.status = SafeRenamePlanStatus::AmbiguousEditRange;
        plan.message = statusMessage(plan.status);
        return plan;
    }

    plan.status = SafeRenamePlanStatus::Ready;
    plan.message = statusMessage(plan.status);
    return plan;
}

bool SafeRenameService::isValidIdentifier(const QString& text)
{
    return SvTokenUtils::isIdentifier(text, true);
}

SemanticIndex* SafeRenameService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
