#ifndef INLINECOMMANDMODE_H
#define INLINECOMMANDMODE_H

#include "completiontypes.h"

class InlineCommandMode
{
public:
    static QList<InlineCommandDescriptor> descriptors();
    static QList<InlineCommandDescriptor> descriptorsForIntent(
        InlineCommandIntent intent);
    static InlineCommandMatch match(const QString& lineUpToCursor);
    static CommandModeCommand toCommandModeCommand(
        const InlineCommandDescriptor& descriptor);
    static QString headerText(const InlineCommandDescriptor& descriptor);
};

#endif // INLINECOMMANDMODE_H
