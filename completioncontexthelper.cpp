#include "completioncontexthelper.h"

#include "relationshipservice.h"
#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "svtokenutils.h"

namespace {

int skipSpacesBackward(const QString& text, int endExclusive)
{
    int pos = qMin(endExclusive, text.size());
    while (pos > 0 && text.at(pos - 1).isSpace())
        --pos;
    return pos;
}

int skipSpacesForward(const QString& text, int pos)
{
    while (pos < text.size() && text.at(pos).isSpace())
        ++pos;
    return pos;
}

bool readIdentifierEndingAt(
    const QString& text,
    int endExclusive,
    QString& outIdentifier)
{
    const int end = skipSpacesBackward(text, endExclusive);
    int start = end;
    while (start > 0
           && SvTokenUtils::isIdentifierContinue(text.at(start - 1))) {
        --start;
    }
    if (start == end || !SvTokenUtils::isIdentifierStart(text.at(start)))
        return false;
    outIdentifier = text.mid(start, end - start);
    return true;
}

bool readIdentifierAt(
    const QString& text,
    int pos,
    QString& outIdentifier,
    int* outEnd = nullptr)
{
    if (pos < 0 || pos >= text.size()
        || !SvTokenUtils::isIdentifierStart(text.at(pos))) {
        return false;
    }
    int end = pos + 1;
    while (end < text.size()
           && SvTokenUtils::isIdentifierContinue(text.at(end))) {
        ++end;
    }
    outIdentifier = text.mid(pos, end - pos);
    if (outEnd)
        *outEnd = end;
    return true;
}

int skipTrailingPackedSelects(const QString& text, int endExclusive)
{
    int pos = skipSpacesBackward(text, endExclusive);
    while (pos > 0 && text.at(pos - 1) == QLatin1Char(']')) {
        int depth = 1;
        --pos;
        while (pos > 0 && depth > 0) {
            --pos;
            if (text.at(pos) == QLatin1Char(']'))
                ++depth;
            else if (text.at(pos) == QLatin1Char('['))
                --depth;
        }
        if (depth != 0)
            return endExclusive;
        pos = skipSpacesBackward(text, pos);
    }
    return pos;
}

QString identifierBeforeAssignment(const QString& context)
{
    const int assignment = context.indexOf(QLatin1Char('='));
    if (assignment < 0)
        return QString();

    QString identifier;
    const int identifierEnd = assignment > 0
            && context.at(assignment - 1) == QLatin1Char('<')
        ? assignment - 1
        : assignment;
    return readIdentifierEndingAt(context, identifierEnd, identifier)
        ? identifier
        : QString();
}

QString identifierInCallParens(const QString& context, const QString& keyword)
{
    const int keywordPos = SvTokenUtils::indexOfWord(context, keyword);
    if (keywordPos < 0)
        return QString();
    int pos = keywordPos + keyword.size();
    pos = skipSpacesForward(context, pos);
    if (pos >= context.size() || context.at(pos) != QLatin1Char('('))
        return QString();
    pos = skipSpacesForward(context, pos + 1);

    QString identifier;
    int end = pos;
    if (!readIdentifierAt(context, pos, identifier, &end))
        return QString();
    end = skipSpacesForward(context, end);
    return end < context.size() && context.at(end) == QLatin1Char(')')
        ? identifier
        : QString();
}

QString identifierInEqualityCondition(const QString& context)
{
    const int ifPos = SvTokenUtils::indexOfWord(context, QStringLiteral("if"));
    if (ifPos < 0)
        return QString();
    int pos = ifPos + QStringLiteral("if").size();
    pos = skipSpacesForward(context, pos);
    if (pos >= context.size() || context.at(pos) != QLatin1Char('('))
        return QString();
    pos = skipSpacesForward(context, pos + 1);

    QString identifier;
    int end = pos;
    if (!readIdentifierAt(context, pos, identifier, &end))
        return QString();
    end = skipSpacesForward(context, end);
    if (end + 1 < context.size()
        && context.at(end) == QLatin1Char('=')
        && context.at(end + 1) == QLatin1Char('=')) {
        return identifier;
    }
    return QString();
}

}

bool CompletionContextHelper::tryParseStructMember(
    const QString& line,
    QString& outVariableName,
    QString& outMemberPrefix)
{
    outVariableName.clear();
    outMemberPrefix.clear();

    const int end = skipSpacesBackward(line, line.size());
    int memberStart = end;
    while (memberStart > 0
           && SvTokenUtils::isIdentifierContinue(line.at(memberStart - 1))) {
        --memberStart;
    }

    const int dotPosition = skipSpacesBackward(line, memberStart) - 1;
    if (dotPosition < 0 || line.at(dotPosition) != QLatin1Char('.'))
        return false;

    QString variableName;
    if (!readIdentifierEndingAt(line, dotPosition, variableName))
        return false;

    outVariableName = variableName;
    outMemberPrefix = line.mid(memberStart, end - memberStart);
    return outMemberPrefix.isEmpty()
        || SvTokenUtils::isIdentifier(outMemberPrefix);
}

QString CompletionContextHelper::extractStructVariable(const QString& context)
{
    const QString trimmed = context.trimmed();
    if (trimmed.endsWith(QLatin1Char('.'))) {
        const int beforeDot = skipTrailingPackedSelects(
            trimmed,
            trimmed.size() - 1);
        QString identifier;
        return readIdentifierEndingAt(trimmed, beforeDot, identifier)
            ? identifier
            : QString();
    }

    if (trimmed.endsWith(QStringLiteral("->"))) {
        QString identifier;
        return readIdentifierEndingAt(
                   trimmed,
                   trimmed.size() - QStringLiteral("->").size(),
                   identifier)
            ? identifier
            : QString();
    }

    return QString();
}

QString CompletionContextHelper::extractEnumVariable(const QString& context)
{
    const QString assigned = identifierBeforeAssignment(context);
    if (!assigned.isEmpty())
        return assigned;

    const QString caseIdentifier =
        identifierInCallParens(context, QStringLiteral("case"));
    if (!caseIdentifier.isEmpty())
        return caseIdentifier;

    const QString equalityIdentifier = identifierInEqualityCondition(context);
    if (!equalityIdentifier.isEmpty())
        return equalityIdentifier;

    return QString();
}

QString CompletionContextHelper::extractModuleType(const QString& context)
{
    int pos = 0;
    while (pos < context.size()) {
        QString moduleType;
        int moduleTypeEnd = pos;
        if (!readIdentifierAt(context, pos, moduleType, &moduleTypeEnd)) {
            ++pos;
            continue;
        }

        int instanceStart = skipSpacesForward(context, moduleTypeEnd);
        if (instanceStart == moduleTypeEnd) {
            pos = moduleTypeEnd;
            continue;
        }

        QString instanceName;
        int instanceEnd = instanceStart;
        if (!readIdentifierAt(context, instanceStart, instanceName, &instanceEnd)) {
            pos = moduleTypeEnd;
            continue;
        }

        const int paren = skipSpacesForward(context, instanceEnd);
        if (paren < context.size() && context.at(paren) == QLatin1Char('('))
            return moduleType;
        pos = instanceEnd;
    }
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

    const QString lowerSymbol = symbol.toLower();
    if (context == QLatin1String("reset")
        && (lowerSymbol.contains(QStringLiteral("rst"))
            || lowerSymbol.contains(QStringLiteral("reset")))) {
        return 50;
    }

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
