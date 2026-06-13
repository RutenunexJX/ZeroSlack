#include "completioncontexthelper.h"

#include "relationshipservice.h"
#include "semanticindex.h"
#include "symbolrelationshipengine.h"

#include <QRegularExpression>

bool CompletionContextHelper::tryParseStructMember(
    const QString& line,
    QString& outVariableName,
    QString& outMemberPrefix)
{
    static const QRegularExpression memberPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\.([a-zA-Z0-9_]*)\\s*$"));
    const QRegularExpressionMatch match = memberPattern.match(line);
    if (!match.hasMatch())
        return false;

    outVariableName = match.captured(1);
    outMemberPrefix = match.captured(2);
    return true;
}

QString CompletionContextHelper::extractStructVariable(const QString& context)
{
    static const QRegularExpression dotPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)(?:\\s*\\[[^\\]]*\\])*\\s*\\.$"));
    QRegularExpressionMatch match = dotPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression arrowPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s*->$"));
    match = arrowPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    return QString();
}

QString CompletionContextHelper::extractEnumVariable(const QString& context)
{
    static const QRegularExpression assignPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s*="));
    QRegularExpressionMatch match = assignPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression casePattern(
        QStringLiteral("case\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\)"));
    match = casePattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression ifPattern(
        QStringLiteral("if\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*=="));
    match = ifPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);

    return QString();
}

QString CompletionContextHelper::extractModuleType(const QString& context)
{
    static const QRegularExpression instPattern(
        QStringLiteral("([a-zA-Z_][a-zA-Z0-9_]*)\\s+[a-zA-Z_][a-zA-Z0-9_]*\\s*\\("));
    const QRegularExpressionMatch match = instPattern.match(context);
    if (match.hasMatch())
        return match.captured(1);
    return QString();
}

int CompletionContextHelper::contextScore(
    const QString& symbol,
    const QString& context)
{
    if (context == QLatin1String("clock")
        && symbol.contains(QStringLiteral("clk"), Qt::CaseInsensitive)) {
        return 50;
    }

    static const QRegularExpression resetPattern(
        QStringLiteral("rst|reset"),
        QRegularExpression::CaseInsensitiveOption);
    if (context == QLatin1String("reset") && symbol.contains(resetPattern))
        return 50;

    return 0;
}

int CompletionContextHelper::relationshipScore(
    SemanticIndex* semanticIndex,
    const QString& symbol,
    const QString& currentContext)
{
    if (!semanticIndex || currentContext.isEmpty())
        return 0;

    RelationshipService relationships(semanticIndex);

    if (relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::CONTAINS)) {
        return 40;
    }

    if (relationships.hasNamedRelationship(
            symbol,
            currentContext,
            SymbolRelationshipEngine::REFERENCES)
        || relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::REFERENCES)) {
        return 30;
    }

    if (relationships.hasNamedRelationship(
            symbol,
            currentContext,
            SymbolRelationshipEngine::CALLS)
        || relationships.hasNamedRelationship(
            currentContext,
            symbol,
            SymbolRelationshipEngine::CALLS)) {
        return 25;
    }

    return 0;
}

int CompletionContextHelper::scopeScore(
    SemanticIndex* semanticIndex,
    const QString& symbol,
    const QString& currentModule)
{
    return semanticIndex ? semanticIndex->scopeScoreForSymbol(symbol, currentModule)
                         : 0;
}
