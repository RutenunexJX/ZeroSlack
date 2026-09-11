#include "sourcenavigationservice.h"

#include <QStringList>

std::unique_ptr<SourceNavigationService> SourceNavigationService::instance = nullptr;

SourceNavigationService* SourceNavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SourceNavigationService>();
    return instance.get();
}

SourceNavigationService::SourceNavigationService() = default;

SourceNavigationService::~SourceNavigationService() = default;

namespace {
bool isInsideLineCommentOrString(const QString& lineText, int column)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < lineText.size(); ++i) {
        if (i == column)
            return inString;

        const QChar ch = lineText.at(i);
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
            && i + 1 < lineText.size()
            && lineText.at(i + 1) == QLatin1Char('/')) {
            return column >= i;
        }
    }
    return inString && column >= lineText.size();
}

bool isPreprocessorDirectiveIdentifier(const QString& identifier)
{
    static const QStringList directives = {
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
        QStringLiteral("pragma"),
        QStringLiteral("resetall"),
        QStringLiteral("timescale"),
        QStringLiteral("undef"),
        QStringLiteral("unconnected_drive"),
    };
    return directives.contains(identifier);
}
}

IncludeDirectiveTarget SourceNavigationService::includeAtColumn(
    const QString& lineText,
    int column) const
{
    IncludeDirectiveTarget target;
    if (lineText.isEmpty() || column < 0)
        return target;

    const int keywordPos = lineText.indexOf(QStringLiteral("`include"));
    if (keywordPos < 0)
        return target;

    const int firstQuote = lineText.indexOf(QLatin1Char('"'), keywordPos);
    if (firstQuote < 0)
        return target;

    const int secondQuote = lineText.indexOf(QLatin1Char('"'), firstQuote + 1);
    if (secondQuote < 0)
        return target;

    if (column <= firstQuote || column >= secondQuote)
        return target;

    const QString includePath =
        lineText.mid(firstQuote + 1, secondQuote - firstQuote - 1).trimmed();
    if (includePath.isEmpty())
        return target;

    target.matched = true;
    target.includePath = includePath;
    target.startColumn = firstQuote + 1;
    target.endColumn = secondQuote;
    return target;
}

PackageImportTarget SourceNavigationService::packageImportAtColumn(
    const QString& lineText,
    int column) const
{
    PackageImportTarget target;
    if (lineText.isEmpty() || column < 0)
        return target;

    const int importPos = lineText.indexOf(QStringLiteral("import"));
    if (importPos < 0)
        return target;

    int packageStart = importPos + QStringLiteral("import").size();
    while (packageStart < lineText.size()
           && lineText.at(packageStart).isSpace()) {
        ++packageStart;
    }

    if (packageStart >= lineText.size())
        return target;

    const QChar firstPackageChar = lineText.at(packageStart);
    if (!firstPackageChar.isLetter() && firstPackageChar != QLatin1Char('_'))
        return target;

    int packageEnd = packageStart + 1;
    while (packageEnd < lineText.size()) {
        const QChar current = lineText.at(packageEnd);
        if (!current.isLetterOrNumber() && current != QLatin1Char('_'))
            break;
        ++packageEnd;
    }

    int scopePos = packageEnd;
    while (scopePos < lineText.size()
           && lineText.at(scopePos).isSpace()) {
        ++scopePos;
    }
    if (lineText.mid(scopePos, 2) != QStringLiteral("::"))
        return target;

    if (column < packageStart || column >= packageEnd)
        return target;

    target.matched = true;
    target.packageName = lineText.mid(packageStart, packageEnd - packageStart);
    target.startColumn = packageStart;
    target.endColumn = packageEnd;
    return target;
}

SourceIdentifierTarget SourceNavigationService::identifierAtColumn(
    const QString& lineText,
    int column) const
{
    SourceIdentifierTarget target;
    if (lineText.isEmpty() || column < 0)
        return target;

    int hitColumn = column;
    if (hitColumn >= lineText.size()) {
        hitColumn = lineText.size() - 1;
    }

    if (isInsideLineCommentOrString(lineText, hitColumn))
        return target;

    if (hitColumn >= 0
        && lineText.at(hitColumn) == QLatin1Char('.')
        && hitColumn + 1 < lineText.size()
        && isIdentifierStart(lineText.at(hitColumn + 1))) {
        ++hitColumn;
    }

    if (hitColumn >= 0
        && lineText.at(hitColumn) == QLatin1Char('`')
        && hitColumn + 1 < lineText.size()
        && isIdentifierStart(lineText.at(hitColumn + 1))) {
        ++hitColumn;
    }

    if (hitColumn < 0 || !isIdentifierPart(lineText.at(hitColumn)))
        return target;

    int startColumn = hitColumn;
    while (startColumn > 0
           && isIdentifierPart(lineText.at(startColumn - 1))) {
        --startColumn;
    }

    if (!isIdentifierStart(lineText.at(startColumn)))
        return target;

    int endColumn = hitColumn + 1;
    while (endColumn < lineText.size()
           && isIdentifierPart(lineText.at(endColumn))) {
        ++endColumn;
    }

    const QString identifier = lineText.mid(startColumn, endColumn - startColumn);
    if (startColumn > 0
        && lineText.at(startColumn - 1) == QLatin1Char('`')
        && isPreprocessorDirectiveIdentifier(identifier)) {
        return target;
    }

    target.matched = true;
    target.identifier = identifier;
    target.startColumn = startColumn;
    target.endColumn = endColumn;
    return target;
}

SourceMemberAccessTarget SourceNavigationService::memberAccessAtColumn(
    const QString& lineText,
    int column) const
{
    SourceMemberAccessTarget target;
    const SourceIdentifierTarget identifier = identifierAtColumn(lineText, column);
    if (!identifier.matched || identifier.identifier.isEmpty())
        return target;

    int startColumn = identifier.startColumn;
    int endColumn = identifier.endColumn;

    while (startColumn > 0) {
        int dotColumn = startColumn - 1;
        while (dotColumn >= 0 && lineText.at(dotColumn).isSpace())
            --dotColumn;
        if (dotColumn < 0 || lineText.at(dotColumn) != QLatin1Char('.'))
            break;

        int previousEnd = dotColumn;
        while (previousEnd > 0 && lineText.at(previousEnd - 1).isSpace())
            --previousEnd;
        int previousStart = previousEnd;
        while (previousStart > 0
               && isIdentifierPart(lineText.at(previousStart - 1))) {
            --previousStart;
        }
        if (previousStart >= previousEnd
            || !isIdentifierStart(lineText.at(previousStart))) {
            break;
        }
        startColumn = previousStart;
    }

    while (endColumn < lineText.size()) {
        int dotColumn = endColumn;
        while (dotColumn < lineText.size() && lineText.at(dotColumn).isSpace())
            ++dotColumn;
        if (dotColumn >= lineText.size()
            || lineText.at(dotColumn) != QLatin1Char('.')) {
            break;
        }

        int nextStart = dotColumn + 1;
        while (nextStart < lineText.size()
               && lineText.at(nextStart).isSpace()) {
            ++nextStart;
        }
        if (nextStart >= lineText.size()
            || !isIdentifierStart(lineText.at(nextStart))) {
            break;
        }

        int nextEnd = nextStart + 1;
        while (nextEnd < lineText.size()
               && isIdentifierPart(lineText.at(nextEnd))) {
            ++nextEnd;
        }
        endColumn = nextEnd;
    }

    QString accessPath = lineText.mid(startColumn, endColumn - startColumn);
    accessPath.remove(QLatin1Char(' '));
    if (!accessPath.contains(QLatin1Char('.')))
        return target;

    const int firstDot = accessPath.indexOf(QLatin1Char('.'));
    target.matched = true;
    target.accessPath = accessPath;
    target.rootIdentifier = accessPath.left(firstDot);
    target.memberPath = accessPath.mid(firstDot + 1);
    target.startColumn = startColumn;
    target.endColumn = endColumn;
    return target;
}

bool SourceNavigationService::isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool SourceNavigationService::isIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

SourceNavigationTarget SourceNavigationService::targetAtColumn(
    const QString& lineText,
    int column) const
{
    const IncludeDirectiveTarget includeTarget = includeAtColumn(lineText, column);
    if (includeTarget.matched) {
        SourceNavigationTarget target;
        target.matched = true;
        target.kind = SourceNavigationTargetKind::IncludeDirective;
        target.text = includeTarget.includePath;
        target.startColumn = includeTarget.startColumn;
        target.endColumn = includeTarget.endColumn;
        return target;
    }

    const PackageImportTarget importTarget = packageImportAtColumn(lineText, column);
    if (importTarget.matched) {
        SourceNavigationTarget target;
        target.matched = true;
        target.kind = SourceNavigationTargetKind::PackageImport;
        target.text = importTarget.packageName;
        target.startColumn = importTarget.startColumn;
        target.endColumn = importTarget.endColumn;
        return target;
    }

    const SourceIdentifierTarget identifierTarget = identifierAtColumn(lineText, column);
    if (identifierTarget.matched) {
        SourceNavigationTarget target;
        target.matched = true;
        target.kind = SourceNavigationTargetKind::Identifier;
        target.text = identifierTarget.identifier;
        target.startColumn = identifierTarget.startColumn;
        target.endColumn = identifierTarget.endColumn;
        return target;
    }

    return {};
}

SourceSymbolActionContext SourceNavigationService::symbolActionContextAtColumn(
    const QString& lineText,
    int column,
    const QString& fileName,
    const QString& moduleName) const
{
    SourceSymbolActionContext context;
    if (fileName.isEmpty())
        return context;

    const SourceIdentifierTarget identifier = identifierAtColumn(lineText, column);
    if (!identifier.matched || identifier.identifier.isEmpty())
        return context;

    const SourceMemberAccessTarget memberAccess =
        memberAccessAtColumn(lineText, column);

    context.available = true;
    context.symbolName = identifier.identifier;
    if (memberAccess.matched) {
        context.memberAccessPath = memberAccess.accessPath;
        context.memberAccessRootName = memberAccess.rootIdentifier;
    }
    context.fileName = fileName;
    context.moduleName = moduleName;
    return context;
}

SourceEditorNavigationTarget SourceNavigationService::editorNavigationTargetAtColumn(
    const QString& lineText,
    int column,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    SourceEditorNavigationTarget editorTarget;
    const SourceNavigationTarget sourceTarget = targetAtColumn(lineText, column);
    if (!sourceTarget.matched)
        return editorTarget;

    editorTarget.matched = true;
    editorTarget.text = sourceTarget.text;
    editorTarget.startColumn = sourceTarget.startColumn;
    editorTarget.endColumn = sourceTarget.endColumn;
    editorTarget.cursorColumn =
        sourceTarget.kind == SourceNavigationTargetKind::Identifier
            ? sourceTarget.startColumn
            : column;
    editorTarget.includeTarget =
        sourceTarget.kind == SourceNavigationTargetKind::IncludeDirective;
    editorTarget.identifierTarget =
        sourceTarget.kind == SourceNavigationTargetKind::Identifier;
    editorTarget.jumpable = editorTarget.includeTarget
        || sourceTarget.kind == SourceNavigationTargetKind::PackageImport
        || (editorTarget.identifierTarget
            && canResolveIdentifier
            && canResolveIdentifier(editorTarget.text));
    return editorTarget;
}

SourceLineNavigationTarget SourceNavigationService::lineNavigationTarget(
    int lineNumber,
    int columnNumber) const
{
    SourceLineNavigationTarget target;
    if (lineNumber <= 0)
        return target;

    target.matched = true;
    target.lineNumber = lineNumber;
    target.columnNumber = columnNumber > 1 ? columnNumber : -1;
    target.lineMoves = lineNumber - 1;
    target.columnMoves = target.columnNumber > 1 ? target.columnNumber - 1 : 0;
    return target;
}
