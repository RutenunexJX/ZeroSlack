#ifndef RTLINSIGHTVIEWPLUGINS_H
#define RTLINSIGHTVIEWPLUGINS_H

#include "insightgraphcore.h"
#include "zeroslackexport.h"

#include <QString>
#include <memory>
#include <vector>

enum class InsightWorkbenchViewKind : quint8 {
    Kernel,
    Block,
    Hotspot,
    StateTransition
};

struct ZEROSLACK_API InsightViewContext {
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    QString fileName;
    QString moduleName;
    QString signalName;
    QString signalAccessPath;
};

struct ZEROSLACK_API InsightViewBuildResult {
    InsightGraphDraft draft;
    QString summary;
    QString errorText;
    bool available = false;
};

class ZEROSLACK_API IInsightViewPlugin
{
public:
    virtual ~IInsightViewPlugin() = default;

    virtual InsightWorkbenchViewKind kind() const = 0;
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString iconKey() const = 0;
    virtual InsightViewBuildResult build(
        const InsightViewContext& context) const = 0;
};

class ZEROSLACK_API SignalKernelInsightViewPlugin final
    : public IInsightViewPlugin
{
public:
    InsightWorkbenchViewKind kind() const override;
    QString pluginId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    InsightViewBuildResult build(
        const InsightViewContext& context) const override;
};

class ZEROSLACK_API ModuleBlockInsightViewPlugin final
    : public IInsightViewPlugin
{
public:
    InsightWorkbenchViewKind kind() const override;
    QString pluginId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    InsightViewBuildResult build(
        const InsightViewContext& context) const override;
};

class ZEROSLACK_API SignalHotspotInsightViewPlugin final
    : public IInsightViewPlugin
{
public:
    InsightWorkbenchViewKind kind() const override;
    QString pluginId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    InsightViewBuildResult build(
        const InsightViewContext& context) const override;
};

class ZEROSLACK_API StateTransitionInsightViewPlugin final
    : public IInsightViewPlugin
{
public:
    InsightWorkbenchViewKind kind() const override;
    QString pluginId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    InsightViewBuildResult build(
        const InsightViewContext& context) const override;
};

ZEROSLACK_API QString insightWorkbenchViewKindId(
    InsightWorkbenchViewKind kind);
ZEROSLACK_API QString insightWorkbenchViewDisplayName(
    InsightWorkbenchViewKind kind);
ZEROSLACK_API std::vector<std::unique_ptr<IInsightViewPlugin>>
createDefaultInsightViewPlugins();

#endif // RTLINSIGHTVIEWPLUGINS_H
