#ifndef PINLOOMCODELINKCOORDINATOR_H
#define PINLOOMCODELINKCOORDINATOR_H

#include "actionregistry.h"
#include "pinloomcodelinkstore.h"
#include "pinloomhostclient.h"
#include "zeroslackexport.h"

class ContextWorkspaceController;

struct ZEROSLACK_API PinloomSourceLinkResult {
    QString requestId;
    QVariantMap source;
    PinloomHostEntry entry;
    bool linked = false;
    QString message;
};

class ZEROSLACK_API PinloomCodeLinkCoordinator : public QObject
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
    using CreateReply = std::function<void(const PinloomSourceLinkResult&)>;
    void createSourceAnchor(PinloomHostClient* client,
                            const QVariantMap& source,
                            const QString& title,
                            CreateReply reply);
    void setCompletionNotice(std::function<void(const QString&)> notice);

    static bool handlesRoute(const QString& route);
    ActionExecutionResult execute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation);

private:
    ContextWorkspaceController* contextWorkspace = nullptr;
    PinloomCodeLinkStore linkStore;
    std::function<void(const QString&)> completionNotice;
};

#endif // PINLOOMCODELINKCOORDINATOR_H
