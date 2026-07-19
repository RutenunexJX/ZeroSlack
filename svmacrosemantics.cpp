#include "svmacrosemantics.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace SvMacroSemantics {
namespace {

struct LineInfo {
    QString text;
    int startPosition = 0;
    int lineNumber = 1;
};

struct DirectiveInfo {
    QString keyword;
    QString argument;
    int keywordStart = -1;
    int argumentStart = -1;
    int afterArgument = -1;
    bool hasSimpleArgument = false;
};

struct ConditionalFrame {
    bool parentActive = true;
    bool currentActive = true;
    bool anyKnownBranchTaken = false;
    bool evaluationUncertain = false;
    bool sawElse = false;
};

QString normalizedMacroFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

int skipSpaces(const QString& text, int pos)
{
    while (pos < text.size() && text.at(pos).isSpace())
        ++pos;
    return pos;
}

bool readIdentifier(const QString& text,
                    int pos,
                    QString* identifier,
                    int* end)
{
    if (!identifier || !end
        || pos < 0
        || pos >= text.size()
        || !isIdentifierStart(text.at(pos))) {
        return false;
    }

    int next = pos + 1;
    while (next < text.size() && isIdentifierPart(text.at(next)))
        ++next;
    *identifier = text.mid(pos, next - pos);
    *end = next;
    return true;
}

QList<LineInfo> splitLines(const QString& content)
{
    QList<LineInfo> result;
    int start = 0;
    int lineNumber = 1;
    while (start <= content.size()) {
        const int newline = content.indexOf(QLatin1Char('\n'), start);
        const int end = newline < 0 ? content.size() : newline;
        QString line = content.mid(start, end - start);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        LineInfo info;
        info.text = line;
        info.startPosition = start;
        info.lineNumber = lineNumber;
        result.append(info);

        if (newline < 0)
            break;
        start = newline + 1;
        ++lineNumber;
        if (start == content.size()) {
            LineInfo tail;
            tail.startPosition = start;
            tail.lineNumber = lineNumber;
            result.append(tail);
            break;
        }
    }
    return result;
}

QString lineWithoutCommentsPreservingColumns(const QString& line,
                                             bool* inBlockComment)
{
    QString result;
    result.reserve(line.size());
    bool inString = false;
    bool escaped = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = i + 1 < line.size() ? line.at(i + 1) : QChar();

        if (inBlockComment && *inBlockComment) {
            result.append(QLatin1Char(' '));
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                result.append(QLatin1Char(' '));
                *inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inString) {
            result.append(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == QLatin1Char('\\')) {
                escaped = true;
            } else if (ch == QLatin1Char('"')) {
                inString = false;
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
            result.append(ch);
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            while (result.size() < line.size())
                result.append(QLatin1Char(' '));
            break;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            if (inBlockComment)
                *inBlockComment = true;
            result.append(QLatin1Char(' '));
            result.append(QLatin1Char(' '));
            ++i;
            continue;
        }

        result.append(ch);
    }

    return result;
}

bool isDirectiveKeyword(const QString& keyword)
{
    static const QSet<QString> keywords = {
        QStringLiteral("begin_keywords"),
        QStringLiteral("celldefine"),
        QStringLiteral("default_nettype"),
        QStringLiteral("define"),
        QStringLiteral("else"),
        QStringLiteral("elsif"),
        QStringLiteral("end_keywords"),
        QStringLiteral("endcelldefine"),
        QStringLiteral("endif"),
        QStringLiteral("ifdef"),
        QStringLiteral("ifndef"),
        QStringLiteral("include"),
        QStringLiteral("line"),
        QStringLiteral("nounconnected_drive"),
        QStringLiteral("pragma"),
        QStringLiteral("resetall"),
        QStringLiteral("timescale"),
        QStringLiteral("undef"),
        QStringLiteral("unconnected_drive"),
    };
    return keywords.contains(keyword);
}

bool isBuiltinMacroName(const QString& name)
{
    static const QSet<QString> names = {
        QStringLiteral("__FILE__"),
        QStringLiteral("__LINE__"),
        QStringLiteral("__DATE__"),
        QStringLiteral("__TIME__"),
    };
    return names.contains(name);
}

bool parseDirective(const QString& maskedLine, DirectiveInfo* directive)
{
    if (!directive)
        return false;

    const int first = skipSpaces(maskedLine, 0);
    if (first >= maskedLine.size()
        || maskedLine.at(first) != QLatin1Char('`')) {
        return false;
    }

    QString keyword;
    int keywordEnd = 0;
    if (!readIdentifier(maskedLine, first + 1, &keyword, &keywordEnd))
        return false;
    if (!isDirectiveKeyword(keyword))
        return false;

    directive->keyword = keyword;
    directive->keywordStart = first + 1;
    directive->argument.clear();
    directive->argumentStart = -1;
    directive->afterArgument = keywordEnd;
    directive->hasSimpleArgument = false;

    int argStart = skipSpaces(maskedLine, keywordEnd);
    QString argument;
    int argEnd = 0;
    if (readIdentifier(maskedLine, argStart, &argument, &argEnd)) {
        directive->argument = argument;
        directive->argumentStart = argStart;
        directive->afterArgument = argEnd;
        directive->hasSimpleArgument =
            maskedLine.mid(argEnd).trimmed().isEmpty();
    }

    return true;
}

bool parseParameterList(const QString& maskedLine,
                        int openParen,
                        QStringList* parameters,
                        int* afterClose)
{
    if (!parameters || !afterClose
        || openParen < 0
        || openParen >= maskedLine.size()
        || maskedLine.at(openParen) != QLatin1Char('(')) {
        return false;
    }

    int depth = 0;
    for (int i = openParen; i < maskedLine.size(); ++i) {
        const QChar ch = maskedLine.at(i);
        if (ch == QLatin1Char('(')) {
            ++depth;
            continue;
        }
        if (ch != QLatin1Char(')'))
            continue;
        --depth;
        if (depth != 0)
            continue;

        const QString paramText = maskedLine.mid(openParen + 1,
                                                 i - openParen - 1);
        const QStringList rawParams = paramText.split(QLatin1Char(','));
        for (QString param : rawParams) {
            param = param.trimmed();
            if (!param.isEmpty())
                parameters->append(param);
        }
        *afterClose = i + 1;
        return true;
    }
    return false;
}

bool lineEndsWithContinuation(QString* line)
{
    if (!line)
        return false;

    int end = line->size() - 1;
    while (end >= 0 && line->at(end).isSpace())
        --end;
    if (end < 0 || line->at(end) != QLatin1Char('\\'))
        return false;
    line->truncate(end);
    return true;
}

QString macroBodyFromLines(const QList<LineInfo>& lines,
                           int lineIndex,
                           int bodyStart)
{
    QStringList parts;
    for (int i = lineIndex; i < lines.size(); ++i) {
        QString segment = i == lineIndex
            ? lines.at(i).text.mid(qBound(0, bodyStart, lines.at(i).text.size()))
            : lines.at(i).text;
        const bool continues = lineEndsWithContinuation(&segment);
        segment = segment.trimmed();
        if (!segment.isEmpty())
            parts.append(segment);
        if (!continues)
            break;
    }
    return parts.join(QLatin1Char('\n')).trimmed();
}

SymbolStableKey macroStableKey(const QString& fileName,
                               const QString& name,
                               int sourcePosition,
                               int sourceLength)
{
    SymbolStableKey key;
    key.fileName = normalizedMacroFileName(fileName);
    key.symbolName = name;
    key.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    key.sourcePosition = sourcePosition;
    key.sourceLength = sourceLength;
    return key;
}

SemanticSymbolRecord macroRecordFromDefinition(
    const MacroDefinition& definition)
{
    SemanticSymbolRecord record;
    if (!definition.isValid())
        return record;

    record.name = definition.name;
    record.location.fileName = definition.fileName;
    record.location.startLine = definition.line;
    record.location.startColumn = definition.column;
    record.location.endLine = definition.line;
    record.location.endColumn = definition.column + definition.name.size();
    record.location.position = definition.position;
    record.location.length = definition.length;
    record.declarationKind = SymbolTaxonomy::DeclarationKind::Macro;
    record.usageRole = SymbolTaxonomy::SymbolUsageRole::Declaration;
    record.visibility = SymbolTaxonomy::SymbolVisibility::Global;
    record.sourceRole = SymbolTaxonomy::sourceRoleForFileName(definition.fileName);
    record.collectorKind = SymbolTaxonomy::CollectorKind::DefDefine;
    record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Global;
    record.stableKey = macroStableKey(definition.fileName,
                                      definition.name,
                                      definition.position,
                                      definition.length);
    record.type.rawTypeText = definition.body;
    record.type.modportName = definition.parameters.join(QStringLiteral(", "));
    return record;
}

bool stackActive(const QList<ConditionalFrame>& stack)
{
    return stack.isEmpty() ? true : stack.constLast().currentActive;
}

bool stackAllowsDefinitionUpdates(const QList<ConditionalFrame>& stack)
{
    if (!stackActive(stack))
        return false;
    for (const ConditionalFrame& frame : stack) {
        if (frame.evaluationUncertain)
            return false;
    }
    return true;
}

bool conditionValue(const DirectiveInfo& directive,
                    const QSet<QString>& definedNames,
                    bool* known)
{
    if (known)
        *known = directive.hasSimpleArgument && !directive.argument.isEmpty();
    if (!directive.hasSimpleArgument || directive.argument.isEmpty())
        return false;

    const bool isDefined = definedNames.contains(directive.argument);
    if (directive.keyword == QStringLiteral("ifndef"))
        return !isDefined;
    return isDefined;
}

InactiveBranchRange inactiveLineRange(const LineInfo& line,
                                      int contentSize,
                                      const QString& reason)
{
    InactiveBranchRange range;
    range.startPosition = line.startPosition;
    range.length = qMax(1, line.text.size());
    if (range.startPosition + range.length > contentSize)
        range.length = qMax(0, contentSize - range.startPosition);
    range.startLine = line.lineNumber;
    range.endLine = line.lineNumber;
    range.reason = reason;
    return range;
}

bool isMacroDefinitionRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Macro
        && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration;
}

} // namespace

QList<MacroDefinition> collectMacroDefinitions(const QString& fileName,
                                               const QString& content)
{
    QList<MacroDefinition> definitions;
    const QList<LineInfo> lines = splitLines(content);
    bool inBlockComment = false;

    for (int i = 0; i < lines.size(); ++i) {
        const LineInfo& line = lines.at(i);
        const QString masked =
            lineWithoutCommentsPreservingColumns(line.text, &inBlockComment);
        DirectiveInfo directive;
        if (!parseDirective(masked, &directive)
            || directive.keyword != QStringLiteral("define")
            || directive.argument.isEmpty()) {
            continue;
        }

        int nameEnd = 0;
        QString name;
        if (!readIdentifier(masked, directive.argumentStart, &name, &nameEnd))
            continue;

        MacroDefinition definition;
        definition.fileName = fileName;
        definition.name = name;
        definition.line = line.lineNumber;
        definition.column = directive.argumentStart + 1;
        definition.position = line.startPosition + directive.argumentStart;
        definition.length = name.size();

        int bodyStart = nameEnd;
        if (nameEnd < masked.size()
            && masked.at(nameEnd) == QLatin1Char('(')) {
            int afterClose = 0;
            QStringList parameters;
            if (parseParameterList(masked, nameEnd, &parameters, &afterClose)) {
                definition.functionLike = true;
                definition.parameters = parameters;
                bodyStart = afterClose;
            }
        }
        bodyStart = skipSpaces(line.text, bodyStart);
        definition.body = macroBodyFromLines(lines, i, bodyStart);
        definitions.append(definition);
    }

    return definitions;
}

QList<MacroReference> collectMacroReferences(const QString& fileName,
                                             const QString& content)
{
    QList<MacroReference> references;
    const QList<LineInfo> lines = splitLines(content);
    bool inBlockComment = false;

    for (const LineInfo& line : lines) {
        bool inString = false;
        bool escaped = false;
        for (int i = 0; i < line.text.size(); ++i) {
            const QChar ch = line.text.at(i);
            const QChar next =
                i + 1 < line.text.size() ? line.text.at(i + 1) : QChar();

            if (inBlockComment) {
                if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (ch == QLatin1Char('\\')) {
                    escaped = true;
                } else if (ch == QLatin1Char('"')) {
                    inString = false;
                }
                continue;
            }

            if (ch == QLatin1Char('"')) {
                inString = true;
                continue;
            }
            if (ch == QLatin1Char('/') && next == QLatin1Char('/'))
                break;
            if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
                inBlockComment = true;
                ++i;
                continue;
            }
            if (ch != QLatin1Char('`'))
                continue;

            const int nameStart = i + 1;
            QString name;
            int nameEnd = 0;
            if (!readIdentifier(line.text, nameStart, &name, &nameEnd))
                continue;
            if (isDirectiveKeyword(name) || isBuiltinMacroName(name))
                continue;

            MacroReference reference;
            reference.fileName = fileName;
            reference.name = name;
            reference.line = line.lineNumber;
            reference.column = nameStart + 1;
            reference.position = line.startPosition + nameStart;
            reference.length = name.size();
            references.append(reference);
            i = nameEnd - 1;
        }
    }

    return references;
}

QList<SemanticSymbolRecord> collectMacroDefinitionRecords(
    const QString& fileName,
    const QString& content)
{
    QList<SemanticSymbolRecord> records;
    const QList<MacroDefinition> definitions =
        collectMacroDefinitions(fileName, content);
    records.reserve(definitions.size());
    for (const MacroDefinition& definition : definitions)
        records.append(macroRecordFromDefinition(definition));
    return records;
}

MacroDefinition macroDefinitionFromRecord(
    const SemanticSymbolRecord& record,
    const QString& content)
{
    if (!isMacroDefinitionRecord(record))
        return {};

    if (!content.isEmpty()) {
        const QList<MacroDefinition> definitions =
            collectMacroDefinitions(record.location.fileName, content);
        for (const MacroDefinition& definition : definitions) {
            if (definition.name != record.name)
                continue;
            if (record.location.startLine > 0
                && definition.line != record.location.startLine) {
                continue;
            }
            return definition;
        }
    }

    MacroDefinition definition;
    definition.fileName = record.location.fileName;
    definition.name = record.name;
    definition.line = record.location.startLine;
    definition.column = record.location.startColumn;
    definition.position = record.location.position;
    definition.length = record.location.length > 0
        ? record.location.length
        : record.name.size();
    definition.body = record.type.rawTypeText;
    if (!record.type.modportName.isEmpty()) {
        definition.parameters =
            record.type.modportName.split(QLatin1Char(','),
                                          Qt::SkipEmptyParts);
        for (QString& parameter : definition.parameters)
            parameter = parameter.trimmed();
        definition.functionLike = !definition.parameters.isEmpty();
    }
    return definition;
}

QString macroSignatureText(const MacroDefinition& definition)
{
    if (!definition.isValid())
        return QString();
    if (!definition.functionLike)
        return definition.name;
    return QStringLiteral("%1(%2)")
        .arg(definition.name, definition.parameters.join(QStringLiteral(", ")));
}

QString truncatedMacroBody(const MacroDefinition& definition,
                           int maxCharacters)
{
    QString body = definition.body.trimmed();
    if (body.isEmpty())
        return QStringLiteral("<empty>");
    body.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (body.size() <= maxCharacters)
        return body;
    return body.left(qMax(0, maxCharacters - 3)) + QStringLiteral("...");
}

QSet<QString> configuredDefineNames(const QHash<QString, QString>& defines)
{
    QSet<QString> names;
    for (auto it = defines.constBegin(); it != defines.constEnd(); ++it) {
        if (!it.key().isEmpty())
            names.insert(it.key());
    }
    return names;
}

QList<SemanticDiagnostic> undefinedMacroDiagnostics(
    const QString& fileName,
    const QString& content,
    const QSet<QString>& visibleMacroNames)
{
    QSet<QString> known = visibleMacroNames;
    for (const MacroDefinition& definition :
         collectMacroDefinitions(fileName, content)) {
        if (!definition.name.isEmpty())
            known.insert(definition.name);
    }

    QList<SemanticDiagnostic> diagnostics;
    QSet<QString> seen;
    for (const MacroReference& reference :
         collectMacroReferences(fileName, content)) {
        if (!reference.isValid() || known.contains(reference.name))
            continue;

        const QString key = QStringLiteral("%1:%2:%3:%4")
                                .arg(fileName)
                                .arg(reference.line)
                                .arg(reference.column)
                                .arg(reference.name);
        if (seen.contains(key))
            continue;
        seen.insert(key);

        SemanticDiagnostic diagnostic;
        diagnostic.fileName = fileName;
        diagnostic.line = reference.line;
        diagnostic.column = reference.column;
        diagnostic.message =
            QStringLiteral("Undefined macro `%1` (not found in configured defines or indexed `define directives).")
                .arg(reference.name);
        diagnostic.severity = SemanticDiagnostic::Error;
        diagnostic.owner = SemanticDiagnostic::SemanticIndexOwner;
        diagnostics.append(diagnostic);
    }
    return diagnostics;
}

QList<InactiveBranchRange> inactiveBranchRanges(
    const QString& content,
    const QHash<QString, QString>& configuredDefines)
{
    QList<InactiveBranchRange> ranges;
    const QList<LineInfo> lines = splitLines(content);
    QSet<QString> definedNames = configuredDefineNames(configuredDefines);
    QList<ConditionalFrame> stack;
    bool inBlockComment = false;

    for (const LineInfo& line : lines) {
        const QString masked =
            lineWithoutCommentsPreservingColumns(line.text, &inBlockComment);
        DirectiveInfo directive;
        const bool hasDirective = parseDirective(masked, &directive);
        const bool activeBeforeDirective = stackActive(stack);

        if (hasDirective) {
            const QString keyword = directive.keyword;
            if (keyword == QStringLiteral("define")) {
                if (stackAllowsDefinitionUpdates(stack)
                    && directive.hasSimpleArgument
                    && !directive.argument.isEmpty()) {
                    definedNames.insert(directive.argument);
                }
            } else if (keyword == QStringLiteral("undef")) {
                if (stackAllowsDefinitionUpdates(stack)
                    && directive.hasSimpleArgument
                    && !directive.argument.isEmpty()) {
                    definedNames.remove(directive.argument);
                }
            } else if (keyword == QStringLiteral("ifdef")
                       || keyword == QStringLiteral("ifndef")) {
                bool known = false;
                const bool value =
                    conditionValue(directive, definedNames, &known);
                ConditionalFrame frame;
                frame.parentActive = activeBeforeDirective;
                frame.evaluationUncertain = !known;
                frame.currentActive = known
                    ? (frame.parentActive && value)
                    : frame.parentActive;
                frame.anyKnownBranchTaken = known && frame.currentActive;
                stack.append(frame);
            } else if (keyword == QStringLiteral("elsif")) {
                if (!stack.isEmpty()) {
                    ConditionalFrame& frame = stack.last();
                    bool known = false;
                    const bool value =
                        conditionValue(directive, definedNames, &known);
                    if (frame.sawElse) {
                        frame.currentActive = false;
                    } else if (frame.anyKnownBranchTaken) {
                        frame.currentActive = false;
                    } else if (!known || frame.evaluationUncertain) {
                        frame.evaluationUncertain = true;
                        frame.currentActive = frame.parentActive;
                    } else {
                        frame.currentActive = frame.parentActive && value;
                        frame.anyKnownBranchTaken = frame.currentActive;
                    }
                }
            } else if (keyword == QStringLiteral("else")) {
                if (!stack.isEmpty()) {
                    ConditionalFrame& frame = stack.last();
                    frame.sawElse = true;
                    if (frame.evaluationUncertain
                        && !frame.anyKnownBranchTaken) {
                        frame.currentActive = frame.parentActive;
                    } else {
                        frame.currentActive =
                            frame.parentActive && !frame.anyKnownBranchTaken;
                        frame.anyKnownBranchTaken =
                            frame.anyKnownBranchTaken || frame.currentActive;
                    }
                }
            } else if (keyword == QStringLiteral("endif")) {
                if (!stack.isEmpty())
                    stack.removeLast();
            }
            continue;
        }

        if (!activeBeforeDirective) {
            const InactiveBranchRange range =
                inactiveLineRange(line,
                                  content.size(),
                                  QStringLiteral("inactive preprocessor branch"));
            if (range.isValid())
                ranges.append(range);
        }
    }

    return ranges;
}

QList<SemanticDecoration> inactiveBranchDecorations(
    const QString& fileName,
    const QString& content,
    const QHash<QString, QString>& configuredDefines)
{
    QList<SemanticDecoration> decorations;
    const QList<InactiveBranchRange> ranges =
        inactiveBranchRanges(content, configuredDefines);
    decorations.reserve(ranges.size());
    for (const InactiveBranchRange& range : ranges) {
        SemanticDecoration decoration;
        decoration.role = SemanticDecorationRole::InactivePreprocessorBranch;
        decoration.text = range.reason;
        decoration.startPosition = range.startPosition;
        decoration.length = range.length;
        decoration.symbolRecord.location.fileName = fileName;
        decoration.symbolRecord.location.startLine = range.startLine;
        decoration.symbolRecord.location.endLine = range.endLine;
        if (decoration.isValid())
            decorations.append(decoration);
    }
    return decorations;
}

} // namespace SvMacroSemantics
