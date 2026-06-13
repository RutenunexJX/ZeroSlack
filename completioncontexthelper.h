#ifndef COMPLETIONCONTEXTHELPER_H
#define COMPLETIONCONTEXTHELPER_H

#include <QString>

class SemanticIndex;

class CompletionContextHelper
{
public:
    static bool tryParseStructMember(
        const QString& line,
        QString& outVariableName,
        QString& outMemberPrefix);
    static QString extractStructVariable(const QString& context);
    static QString extractEnumVariable(const QString& context);
    static QString extractModuleType(const QString& context);
    static int contextScore(const QString& symbol, const QString& context);
    static int relationshipScore(
        SemanticIndex* semanticIndex,
        const QString& symbol,
        const QString& currentContext);
    static int scopeScore(
        SemanticIndex* semanticIndex,
        const QString& symbol,
        const QString& currentModule);
};

#endif // COMPLETIONCONTEXTHELPER_H
