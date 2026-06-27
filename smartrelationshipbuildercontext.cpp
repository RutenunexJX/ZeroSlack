#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QSet>

#include <utility>

namespace {

QString rootNameForAccessPathText(const QString& accessPath)
{
    const int dotIndex = accessPath.indexOf(QLatin1Char('.'));
    return dotIndex < 0 ? accessPath : accessPath.left(dotIndex);
}

bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('$') || ch == QLatin1Char('_')
        || ch.isLetter();
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

void skipSpaces(const QString& text, int* pos)
{
    while (pos && *pos < text.size() && text.at(*pos).isSpace())
        ++(*pos);
}

QString readIdentifier(const QString& text, int* pos)
{
    if (!pos || *pos >= text.size())
        return QString();

    if (text.at(*pos) == QLatin1Char('\\')) {
        const int start = *pos;
        ++(*pos);
        while (*pos < text.size() && !text.at(*pos).isSpace())
            ++(*pos);
        return text.mid(start, *pos - start).trimmed();
    }

    if (!isIdentifierStart(text.at(*pos)))
        return QString();

    const int start = *pos;
    ++(*pos);
    while (*pos < text.size() && isIdentifierPart(text.at(*pos)))
        ++(*pos);
    return text.mid(start, *pos - start);
}

void skipSelectSuffixes(const QString& text, int* pos)
{
    if (!pos)
        return;

    bool skipped = true;
    while (skipped) {
        skipped = false;
        skipSpaces(text, pos);
        if (*pos >= text.size() || text.at(*pos) != QLatin1Char('['))
            continue;

        int depth = 0;
        while (*pos < text.size()) {
            const QChar ch = text.at(*pos);
            if (ch == QLatin1Char('['))
                ++depth;
            else if (ch == QLatin1Char(']')) {
                --depth;
                ++(*pos);
                if (depth <= 0)
                    break;
                continue;
            }
            ++(*pos);
        }
        skipped = true;
    }
}

QString readAccessPath(const QString& text, int* pos)
{
    if (!pos)
        return QString();

    skipSpaces(text, pos);
    QString root = readIdentifier(text, pos);
    if (root.isEmpty())
        return QString();

    QString accessPath = root;
    while (*pos < text.size()) {
        skipSelectSuffixes(text, pos);
        skipSpaces(text, pos);
        if (*pos >= text.size() || text.at(*pos) != QLatin1Char('.'))
            break;

        ++(*pos);
        skipSpaces(text, pos);
        QString member = readIdentifier(text, pos);
        if (member.isEmpty())
            break;
        accessPath += QLatin1Char('.');
        accessPath += member;
    }
    return accessPath;
}

bool ignoredAccessRoot(const QString& root)
{
    static const QSet<QString> ignored = {
        QStringLiteral("assign"),
        QStringLiteral("begin"),
        QStringLiteral("end"),
        QStringLiteral("if"),
        QStringLiteral("else"),
        QStringLiteral("case"),
        QStringLiteral("endcase"),
        QStringLiteral("for"),
        QStringLiteral("while"),
        QStringLiteral("posedge"),
        QStringLiteral("negedge"),
        QStringLiteral("or"),
        QStringLiteral("and")
    };
    return root.isEmpty() || ignored.contains(root);
}

QStringList accessPathsFromText(const QString& text)
{
    QStringList paths;
    QSet<QString> seen;
    int pos = 0;
    while (pos < text.size()) {
        if (!isIdentifierStart(text.at(pos)) && text.at(pos) != QLatin1Char('\\')) {
            ++pos;
            continue;
        }
        if (pos > 0
            && (text.at(pos - 1) == QLatin1Char('\'')
                || text.at(pos - 1) == QLatin1Char('.')
                || isIdentifierPart(text.at(pos - 1)))) {
            ++pos;
            continue;
        }

        int end = pos;
        const QString accessPath = readAccessPath(text, &end);
        if (end <= pos) {
            ++pos;
            continue;
        }
        pos = end;

        const QString root = rootNameForAccessPathText(accessPath);
        if (ignoredAccessRoot(root) || seen.contains(accessPath))
            continue;

        seen.insert(accessPath);
        paths.append(accessPath);
    }
    return paths;
}

QStringList rootsForAccessPaths(const QStringList& accessPaths)
{
    QStringList roots;
    QSet<QString> seen;
    for (const QString& accessPath : accessPaths) {
        const QString root = rootNameForAccessPathText(accessPath);
        if (ignoredAccessRoot(root) || seen.contains(root))
            continue;
        seen.insert(root);
        roots.append(root);
    }
    return roots;
}

QString stripCommentsFromLine(const QString& line, bool* inBlockComment)
{
    QString result;
    for (int i = 0; i < line.size(); ++i) {
        if (inBlockComment && *inBlockComment) {
            const int end = line.indexOf(QStringLiteral("*/"), i);
            if (end < 0)
                return result;
            *inBlockComment = false;
            i = end + 1;
            continue;
        }

        if (i + 1 < line.size()
            && line.at(i) == QLatin1Char('/')
            && line.at(i + 1) == QLatin1Char('/')) {
            return result;
        }
        if (i + 1 < line.size()
            && line.at(i) == QLatin1Char('/')
            && line.at(i + 1) == QLatin1Char('*')) {
            if (inBlockComment)
                *inBlockComment = true;
            ++i;
            continue;
        }
        result.append(line.at(i));
    }
    return result;
}

bool startsWithKeyword(const QString& text, const QString& keyword)
{
    if (!text.startsWith(keyword))
        return false;
    if (text.size() == keyword.size())
        return true;
    const QChar next = text.at(keyword.size());
    return !isIdentifierPart(next);
}

bool isControlStatementStart(const QString& text)
{
    static const QStringList controls = {
        QStringLiteral("if"),
        QStringLiteral("for"),
        QStringLiteral("while"),
        QStringLiteral("case"),
        QStringLiteral("module"),
        QStringLiteral("always")
    };
    for (const QString& control : controls) {
        if (startsWithKeyword(text, control))
            return true;
    }
    return false;
}

int findAssignmentOperator(const QString& text, int* opLength)
{
    int squareDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('[')) {
            ++squareDepth;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            squareDepth = qMax(0, squareDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            parenDepth = qMax(0, parenDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            braceDepth = qMax(0, braceDepth - 1);
            continue;
        }
        if (squareDepth > 0 || parenDepth > 0 || braceDepth > 0)
            continue;

        if (ch == QLatin1Char('<') && i + 1 < text.size()
            && text.at(i + 1) == QLatin1Char('=')) {
            if (opLength)
                *opLength = 2;
            return i;
        }
        if (ch != QLatin1Char('='))
            continue;

        const QChar prev = i > 0 ? text.at(i - 1) : QChar();
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        if (prev == QLatin1Char('=') || prev == QLatin1Char('!')
            || prev == QLatin1Char('<') || prev == QLatin1Char('>')
            || next == QLatin1Char('=')) {
            continue;
        }
        if (opLength)
            *opLength = 1;
        return i;
    }
    return -1;
}

bool appendTextAssignment(RelationshipExtractionInfo& info,
                          const QString& assignmentText,
                          const QString& fileName,
                          int lineNumber)
{
    QString text = assignmentText.trimmed();
    if (startsWithKeyword(text, QStringLiteral("assign")))
        text = text.mid(QStringLiteral("assign").size()).trimmed();
    if (text.endsWith(QLatin1Char(';')))
        text.chop(1);

    int opLength = 0;
    const int opIndex = findAssignmentOperator(text, &opLength);
    if (opIndex < 0)
        return false;

    const QString leftText = text.left(opIndex).trimmed();
    const QString rightText = text.mid(opIndex + opLength).trimmed();
    const QStringList leftPaths = accessPathsFromText(leftText);
    const QStringList rightPaths = accessPathsFromText(rightText);
    if (leftPaths.isEmpty() || rightPaths.isEmpty())
        return false;

    AssignmentInfo assignment;
    assignment.leftAccessPath = leftPaths.first();
    assignment.leftName = rootNameForAccessPathText(assignment.leftAccessPath);
    assignment.rightAccessPaths = rightPaths;
    assignment.rightNames = rootsForAccessPaths(rightPaths);
    assignment.lineNumber = lineNumber <= 0 ? 1 : lineNumber;
    assignment.sourceRange.fileName = fileName;
    assignment.sourceRange.line = assignment.lineNumber;
    assignment.sourceRange.column = 1;
    assignment.sourceRange.endLine = assignment.lineNumber;
    assignment.sourceRange.endColumn = assignmentText.size();
    if (assignment.leftName.isEmpty() || assignment.rightNames.isEmpty())
        return false;

    for (const AssignmentInfo& existing : std::as_const(info.assignments)) {
        if (existing.lineNumber == assignment.lineNumber
            && existing.leftAccessPath == assignment.leftAccessPath
            && existing.rightAccessPaths == assignment.rightAccessPaths) {
            return false;
        }
    }

    info.assignments.append(assignment);
    return true;
}

void appendTextualAssignmentFallback(RelationshipExtractionInfo& info,
                                     const QString& fileName,
                                     const QString& content,
                                     bool includeSimpleProceduralAssignments)
{
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool inBlockComment = false;
    bool collectingContinuousAssign = false;
    QString pendingAssign;
    int pendingLine = 1;

    for (int i = 0; i < lines.size(); ++i) {
        const QString cleanLine = stripCommentsFromLine(lines.at(i), &inBlockComment);
        const QString trimmed = cleanLine.trimmed();
        if (trimmed.isEmpty())
            continue;

        if (collectingContinuousAssign) {
            pendingAssign += QLatin1Char(' ');
            pendingAssign += trimmed;
            if (pendingAssign.contains(QLatin1Char(';'))) {
                appendTextAssignment(info,
                                     pendingAssign.left(pendingAssign.indexOf(QLatin1Char(';')) + 1),
                                     fileName,
                                     pendingLine);
                collectingContinuousAssign = false;
                pendingAssign.clear();
            }
            continue;
        }

        if (startsWithKeyword(trimmed, QStringLiteral("assign"))) {
            pendingAssign = trimmed;
            pendingLine = i + 1;
            if (pendingAssign.contains(QLatin1Char(';'))) {
                appendTextAssignment(info,
                                     pendingAssign.left(pendingAssign.indexOf(QLatin1Char(';')) + 1),
                                     fileName,
                                     pendingLine);
                pendingAssign.clear();
            } else {
                collectingContinuousAssign = true;
            }
            continue;
        }

        if (isControlStatementStart(trimmed)) {
            continue;
        }

        if (includeSimpleProceduralAssignments) {
            int opLength = 0;
            if (trimmed.contains(QLatin1Char(';'))
                && findAssignmentOperator(trimmed, &opLength) >= 0) {
                appendTextAssignment(info, trimmed, fileName, i + 1);
            }
        }
    }
}

} // namespace

void SmartRelationshipBuilder::setupAnalysisContext(const QString& fileName,
                                                    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbolRecords = m_symbolRecordProvider
        ? m_symbolRecordProvider(fileName)
        : QList<SemanticSymbolRecord>();
    context.localSymbolHandles.clear();
    context.recordsByName.clear();
    context.recordsByLocalHandle.clear();
    context.symbolRecordLookupCache.clear();
    context.containingModuleHandleByLine.clear();
    context.moduleRecords.clear();
    context.recordsByName.reserve(context.fileSymbolRecords.size());
    context.recordsByLocalHandle.reserve(context.fileSymbolRecords.size());

    for (const SemanticSymbolRecord& record : std::as_const(context.fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.recordsByName[record.name].append(record);
        if (record.localHandle >= 0)
            context.recordsByLocalHandle.insert(record.localHandle, record);

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            context.moduleRecords.append(record);
            if (context.currentModuleLocalHandle == -1) {
                context.currentModuleName = record.name;
                context.currentModuleLocalHandle = record.localHandle;
            }
        }
    }
}

void SmartRelationshipBuilder::setupAnalysisContextFromRecords(
    const QString& fileName,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot,
    AnalysisContext& context)
{
    context.currentFileName = fileName;
    context.fileSymbolRecords = fileSymbolRecords;
    context.localSymbolHandles.clear();
    context.recordsByName.clear();
    context.recordsByLocalHandle.clear();
    context.symbolRecordLookupCache.clear();
    context.containingModuleHandleByLine.clear();
    context.moduleRecords.clear();
    context.snapshot = snapshot;
    context.recordsByName.reserve(fileSymbolRecords.size());
    context.recordsByLocalHandle.reserve(fileSymbolRecords.size());

    for (const SemanticSymbolRecord& record : std::as_const(fileSymbolRecords)) {
        context.localSymbolHandles[record.name] = record.localHandle;
        context.recordsByName[record.name].append(record);
        if (record.localHandle >= 0)
            context.recordsByLocalHandle.insert(record.localHandle, record);

        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            context.moduleRecords.append(record);
            if (context.currentModuleLocalHandle == -1) {
                context.currentModuleName = record.name;
                context.currentModuleLocalHandle = record.localHandle;
            }
        }
    }
}

void SmartRelationshipBuilder::ensureRelationshipInfo(const QString& content,
                                                      AnalysisContext& context)
{
    if (!context.relationshipInfoLoaded) {
        if (m_slangManager) {
            context.relationshipInfo =
                m_slangManager->extractRelationshipInfo(context.currentFileName,
                                                        content,
                                                        context.includeDirs,
                                                        context.defines);
        }
        context.relationshipInfoLoaded = true;
    }

    if (!context.textualAssignmentFallbackLoaded) {
        const bool includeSimpleProceduralAssignments =
            context.relationshipInfo.assignments.isEmpty();
        appendTextualAssignmentFallback(context.relationshipInfo,
                                        context.currentFileName,
                                        content,
                                        includeSimpleProceduralAssignments);
        context.textualAssignmentFallbackLoaded = true;
    }
}
