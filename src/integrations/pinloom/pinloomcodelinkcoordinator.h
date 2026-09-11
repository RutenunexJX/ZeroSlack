#ifndef PINLOOMCODELINKCOORDINATOR_H
#define PINLOOMCODELINKCOORDINATOR_H

#include "actionregistry.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "zeroslackexport.h"

class ContextWorkspaceController;

class ZEROSLACK_API PinloomCodeLinkCoordinator
{
public:
    explicit PinloomCodeLinkCoordinator(
        ContextWorkspaceController* contextWorkspace);

    void setWorkspaceRoot(const QString& workspaceRoot);
    PinloomCodeLinkStore* store();
    const PinloomCodeLinkStore* store() const;

    bool attachLink(const QVariantMap& sourceMap,
                    const PinloomHostEntry& entry,
                    QString* failureReason = nullptr);

    static bool handlesRoute(const QString& route);
    ActionExecutionResult execute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation);

private:
    ContextWorkspaceController* contextWorkspace = nullptr;
    PinloomCodeLinkStore linkStore;
};

#endif // PINLOOMCODELINKCOORDINATOR_H
