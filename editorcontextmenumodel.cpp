#include "editorcontextmenumodel.h"

#include "actionregistry.h"

#include <QStringList>

namespace {
EditorContextMenuSection sectionFor(
    const ActionDescriptor& descriptor,
    bool standard)
{
    if (standard)
        return EditorContextMenuSection::Standard;

    switch (descriptor.category) {
    case ActionCategory::Navigate:
        return EditorContextMenuSection::Navigate;
    case ActionCategory::Inspect:
        return EditorContextMenuSection::Inspect;
    case ActionCategory::Format:
        return EditorContextMenuSection::Format;
    case ActionCategory::Refactor:
    case ActionCategory::Insert:
    case ActionCategory::Select:
    case ActionCategory::Fold:
    case ActionCategory::Workspace:
    case ActionCategory::Help:
    case ActionCategory::Unknown:
        return EditorContextMenuSection::Refactor;
    }
    return EditorContextMenuSection::Refactor;
}

ActionAvailabilityContext availabilityContext(
    const EditorContextMenuRequest& request)
{
    ActionAvailabilityContext context;
    context.editorAvailable =
        !request.actionContext.fileName.trimmed().isEmpty();
    context.workspaceAvailable =
        !request.actionContext.workspacePath.trimmed().isEmpty();
    context.semanticCurrent =
        request.actionContext.semanticState
        == EditorActionSemanticState::Current;
    context.symbolAvailable = request.symbolAvailable;
    context.packageAvailable =
        !request.actionContext.packageName.trimmed().isEmpty();
    context.hierarchyBound =
        !request.actionContext.resolvedHierarchy.instancePath
             .trimmed()
             .isEmpty()
        && !request.actionContext.resolvedHierarchy.activeTopModule
                .trimmed()
                .isEmpty();
    return context;
}

QString contextReason(
    const ActionDescriptor& descriptor,
    const EditorContextMenuRequest& request,
    const ActionAvailabilityState& availability)
{
    if ((descriptor.requirementMask & ActionRequirements::Hierarchy)
        && !availability.executable
        && !request.actionContext.hierarchyResolutionReason
                .trimmed()
                .isEmpty()) {
        return request.actionContext.hierarchyResolutionReason.trimmed();
    }
    if ((descriptor.requirementMask
         & ActionRequirements::SemanticCurrent)
        && request.actionContext.semanticState
               != EditorActionSemanticState::Current) {
        switch (request.actionContext.semanticState) {
        case EditorActionSemanticState::Stale:
            return QStringLiteral(
                "Semantic snapshot is stale; analyze the workspace.");
        case EditorActionSemanticState::Analyzing:
            return QStringLiteral("Semantic analysis is in progress.");
        case EditorActionSemanticState::Failed:
            return request.actionContext.semanticError.trimmed().isEmpty()
                ? QStringLiteral("Semantic analysis failed.")
                : request.actionContext.semanticError.trimmed();
        case EditorActionSemanticState::Unavailable:
            return QStringLiteral(
                "No semantic snapshot is available; analyze the workspace.");
        case EditorActionSemanticState::Current:
            break;
        }
    }
    return availability.reason.trimmed();
}

QString visibleActionText(const QString& label, const QString& reason)
{
    if (reason.trimmed().isEmpty())
        return label;
    return QStringLiteral("%1 — %2").arg(label, reason.trimmed());
}

EditorContextMenuSectionModel* findOrAppendSection(
    EditorContextMenuModel* model,
    EditorContextMenuSection section)
{
    for (EditorContextMenuSectionModel& candidate : model->sections) {
        if (candidate.section == section)
            return &candidate;
    }

    EditorContextMenuSectionModel added;
    added.section = section;
    added.title = editorContextMenuSectionText(section);
    model->sections.append(added);
    return &model->sections.last();
}
}

QString editorContextMenuSectionText(EditorContextMenuSection section)
{
    switch (section) {
    case EditorContextMenuSection::Standard:
        return QString();
    case EditorContextMenuSection::Navigate:
        return QStringLiteral("Navigate");
    case EditorContextMenuSection::Inspect:
        return QStringLiteral("Inspect");
    case EditorContextMenuSection::Refactor:
        return QStringLiteral("Refactor");
    case EditorContextMenuSection::Format:
        return QStringLiteral("Format");
    }
    return QString();
}

EditorContextMenuModel buildEditorContextMenuModel(
    const EditorContextMenuRequest& request)
{
    EditorContextMenuModel unordered;
    const ActionAvailabilityContext registryContext =
        availabilityContext(request);

    for (const EditorContextMenuCapability& capability :
         request.capabilities) {
        if (!capability.relevant)
            continue;

        const ActionDescriptor* descriptor =
            findActionById(capability.actionId);
        if (!descriptor
            || !descriptor->hasSurface(ActionSurface::ContextMenu)) {
            continue;
        }

        const ActionAvailabilityState availability =
            evaluateActionAvailability(*descriptor, registryContext);
        const bool executable =
            capability.executable && availability.executable;
        QString reason;
        if (!capability.executable) {
            reason = capability.unavailableReason.trimmed();
        } else if (!availability.executable) {
            reason = contextReason(*descriptor, request, availability);
        }
        if (!executable && reason.isEmpty())
            reason = descriptor->unavailableReason.trimmed();

        const ActionAliasDescriptor alias =
            descriptor->aliasForSurface(ActionSurface::ContextMenu);
        const QString label = alias.label.trimmed().isEmpty()
            ? descriptor->canonicalName
            : alias.label.trimmed();

        EditorContextMenuItem item;
        item.actionId = descriptor->id;
        item.section = sectionFor(*descriptor, capability.standard);
        item.executable = executable;
        const bool runtimeResolvable =
            !executable
            && (capability.enterableWhenUnavailable
                || (!capability.executable
                    && descriptor->recoveryPolicy
                           == ActionRecoveryPolicy::Explain));
        item.enabled = executable
            || availability.resolvable
            || runtimeResolvable;
        item.visibleReason = executable ? QString() : reason;
        item.text = visibleActionText(label, item.visibleReason);

        findOrAppendSection(&unordered, item.section)
            ->items.append(item);
    }

    EditorContextMenuModel ordered;
    const QList<EditorContextMenuSection> order = {
        EditorContextMenuSection::Standard,
        EditorContextMenuSection::Navigate,
        EditorContextMenuSection::Inspect,
        EditorContextMenuSection::Refactor,
        EditorContextMenuSection::Format
    };
    for (EditorContextMenuSection section : order) {
        for (const EditorContextMenuSectionModel& candidate :
             unordered.sections) {
            if (candidate.section == section
                && !candidate.items.isEmpty()) {
                ordered.sections.append(candidate);
                break;
            }
        }
    }
    return ordered;
}
