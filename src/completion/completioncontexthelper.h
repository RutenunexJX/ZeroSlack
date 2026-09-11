#ifndef COMPLETIONCONTEXTHELPER_H
#define COMPLETIONCONTEXTHELPER_H

#include <QString>

class CompletionContextHelper
{
public:
    static bool tryParseStructMember(
        const QString& line,
        QString& outVariableName,
        QString& outMemberPrefix);
};

#endif // COMPLETIONCONTEXTHELPER_H
