#include "completioncontexthelper.h"

#include "svtokenutils.h"

namespace {

int skipSpacesBackward(const QString& text, int endExclusive)
{
    int pos = qMin(endExclusive, text.size());
    while (pos > 0 && text.at(pos - 1).isSpace())
        --pos;
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
