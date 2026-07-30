#ifndef ACTIONREGISTRY_H
#define ACTIONREGISTRY_H

#include <QList>
#include <QString>
#include <QtGlobal>

enum class ActionCategory {
    Unknown,
    Navigate,
    Inspect,
    Refactor,
    Format,
    Insert,
    Select,
    Workspace,
    Fold,
    Help
};

enum class ActionScope {
    Unknown,
    Application,
    Workspace,
    Editor,
    Module,
    Package,
    Symbol,
    Hierarchy
};

enum class ActionParameterKind {
    None,
    PositiveInteger,
    TextQuery,
    TemplateSeed,
    HierarchySelection,
    PackageTool
};

enum class ActionSurface {
    Unknown,
    CommandLayer,
    InlineSemantic,
    InlineTemplate,
    GlobalControl,
    PackageTools,
    ContextMenu,
    Shortcut,
    ActionCatalog
};

enum class ActionRecoveryPolicy {
    None,
    Explain,
    Analyze,
    SelectHierarchy
};

namespace ActionRequirements {
inline constexpr quint32 Editor = 1u << 0;
inline constexpr quint32 Workspace = 1u << 1;
inline constexpr quint32 SemanticCurrent = 1u << 2;
inline constexpr quint32 Symbol = 1u << 3;
inline constexpr quint32 Package = 1u << 4;
inline constexpr quint32 Hierarchy = 1u << 5;
}

struct ActionParameterModel {
    ActionParameterKind kind = ActionParameterKind::None;
    QString name;
    QString placeholder;
};

struct ActionAliasDescriptor {
    ActionSurface surface = ActionSurface::Unknown;
    QString token;
    QString label;
    QString description;
    QString defaultValue;
    QString adapterKey;
    QString intentKey;
    bool triggerAdapter = true;
    bool catalogued = false;
};

struct ActionDescriptor {
    QString id;
    QString canonicalName;
    QString description;
    ActionCategory category = ActionCategory::Unknown;
    ActionScope scope = ActionScope::Unknown;
    ActionParameterModel parameterModel;
    quint32 requirementMask = 0;
    ActionRecoveryPolicy recoveryPolicy = ActionRecoveryPolicy::None;
    QString unavailableReason;
    QString executionRoute;
    QList<ActionAliasDescriptor> aliases;

    bool hasSurface(ActionSurface surface) const;
    ActionAliasDescriptor aliasForSurface(ActionSurface surface) const;
};

struct ActionAvailabilityContext {
    bool editorAvailable = false;
    bool workspaceAvailable = false;
    bool semanticCurrent = false;
    bool symbolAvailable = false;
    bool packageAvailable = false;
    bool hierarchyBound = false;
};

struct ActionAvailabilityState {
    bool executable = false;
    bool resolvable = false;
    QString reason;

    bool enterable() const { return executable || resolvable; }
};

struct ActionCatalogEntry {
    QString actionId;
    QString canonicalName;
    ActionCategory category = ActionCategory::Unknown;
    ActionScope scope = ActionScope::Unknown;
    QString displayText;
};

const QList<ActionDescriptor>& actionRegistry();
const ActionDescriptor* findActionById(const QString& id);
const ActionDescriptor* findActionByAlias(ActionSurface surface,
                                          const QString& token);
const ActionAliasDescriptor* findActionAlias(
    const ActionDescriptor& descriptor,
    ActionSurface surface,
    const QString& token = QString());
QList<const ActionDescriptor*> actionDescriptorsForSurface(
    ActionSurface surface);
QList<ActionCatalogEntry> unifiedActionCatalog();
bool validateActionRegistry(const QList<ActionDescriptor>& registry,
                            QString* reason = nullptr);
bool actionRegistryIsValid(QString* reason = nullptr);
ActionAvailabilityState evaluateActionAvailability(
    const ActionDescriptor& descriptor,
    const ActionAvailabilityContext& context);
QString actionCategoryText(ActionCategory category);
QString actionScopeText(ActionScope scope);
QString actionSurfaceText(ActionSurface surface);

#endif // ACTIONREGISTRY_H
