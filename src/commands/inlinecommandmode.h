#ifndef INLINECOMMANDMODE_H
#define INLINECOMMANDMODE_H

#include "completiontypes.h"

class InlineCommandMode
{
public:
    static QList<InlineCommandDescriptor> descriptors();
    static QList<InlineCommandDescriptor> descriptorsForIntent(
        InlineCommandIntent intent);
    static InlineCommandMatch matchAbbreviationBeforeCursor(
        const QString& textBeforeCursor);
    static InlineCommandMatch matchAbbreviationBeforeCursor(
        const QString& textBeforeCursor,
        const QList<InlineCommandDescriptor>& registry);
    static InlineCommandMatch match(const QString& lineUpToCursor);
    static bool isPositionInCommentOrString(const QString& text,
                                            int position);
    static CommandModeCommand toCommandModeCommand(
        const InlineCommandDescriptor& descriptor);
    static QString headerText(const InlineCommandDescriptor& descriptor);
};

#endif // INLINECOMMANDMODE_H
