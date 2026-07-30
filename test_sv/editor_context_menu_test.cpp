#include "actionregistry.h"
#include "editorcontextmenumodel.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

const EditorContextMenuSectionModel* findSection(
    const EditorContextMenuModel& model,
    EditorContextMenuSection section)
{
    for (const EditorContextMenuSectionModel& candidate : model.sections) {
        if (candidate.section == section)
            return &candidate;
    }
    return nullptr;
}

const EditorContextMenuItem* findItem(
    const EditorContextMenuModel& model,
    const QString& actionId)
{
    for (const EditorContextMenuSectionModel& section : model.sections) {
        for (const EditorContextMenuItem& item : section.items) {
            if (item.actionId == actionId)
                return &item;
        }
    }
    return nullptr;
}

EditorActionContext currentContext()
{
    EditorActionContext context;
    context.workspacePath = QStringLiteral("C:/rtl/workspace");
    context.fileName = QStringLiteral("C:/rtl/workspace/top.sv");
    context.moduleName = QStringLiteral("top");
    context.semanticState = EditorActionSemanticState::Current;
    context.resolvedHierarchy.workspacePath = context.workspacePath;
    context.resolvedHierarchy.activeTopModule = QStringLiteral("top");
    context.resolvedHierarchy.instancePath = QStringLiteral("top");
    return context;
}

EditorContextMenuCapability capability(
    const QString& id,
    bool relevant = true,
    bool executable = true,
    const QString& reason = QString(),
    bool standard = false)
{
    EditorContextMenuCapability result;
    result.actionId = id;
    result.relevant = relevant;
    result.executable = executable;
    result.unavailableReason = reason;
    result.standard = standard;
    return result;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    EditorContextMenuRequest request;
    request.actionContext = currentContext();
    request.actionContext.resolvedHierarchy = {};
    request.actionContext.hierarchySelectionRequired = true;
    request.actionContext.hierarchyResolutionReason =
        QStringLiteral("Choose an active top / instance.");
    request.symbolAvailable = true;
    request.capabilities = {
        capability(QStringLiteral("edit.undo"),
                   true,
                   false,
                   QStringLiteral("Nothing to undo."),
                   true),
        capability(QStringLiteral("edit.redo"), true, true, {}, true),
        capability(QStringLiteral("edit.cut"),
                   true,
                   false,
                   QStringLiteral("Select text to cut."),
                   true),
        capability(QStringLiteral("edit.copy"),
                   true,
                   false,
                   QStringLiteral("Select text to copy."),
                   true),
        capability(QStringLiteral("edit.paste"), true, true, {}, true),
        capability(QStringLiteral("source.goToDefinition")),
        capability(QStringLiteral("source.findReferences")),
        capability(QStringLiteral("insight.stateTransitionGraph"), false),
        capability(QStringLiteral("refactor.exposeSignalToTop")),
        capability(QStringLiteral("format.selection"), false),
        capability(QStringLiteral("format.document"))
    };

    const EditorContextMenuModel model =
        buildEditorContextMenuModel(request);
    check(model.sections.size() == 5,
          "menu has the five stable standard/category sections");
    const QList<EditorContextMenuSection> expectedOrder = {
        EditorContextMenuSection::Standard,
        EditorContextMenuSection::Navigate,
        EditorContextMenuSection::Inspect,
        EditorContextMenuSection::Refactor,
        EditorContextMenuSection::Format
    };
    for (int index = 0;
         index < expectedOrder.size() && index < model.sections.size();
         ++index) {
        check(model.sections.at(index).section == expectedOrder.at(index),
              "menu sections retain stable order");
    }

    const EditorContextMenuSectionModel* standard =
        findSection(model, EditorContextMenuSection::Standard);
    check(standard && standard->items.size() == 5,
          "standard actions remain in the familiar leading block");
    check(standard
              && standard->items.at(0).actionId
                     == QStringLiteral("edit.undo")
              && standard->items.at(1).actionId
                     == QStringLiteral("edit.redo")
              && standard->items.at(2).actionId
                     == QStringLiteral("edit.cut")
              && standard->items.at(3).actionId
                     == QStringLiteral("edit.copy")
              && standard->items.at(4).actionId
                     == QStringLiteral("edit.paste"),
          "Undo/Redo/Cut/Copy/Paste order is stable");

    const EditorContextMenuItem* undo =
        findItem(model, QStringLiteral("edit.undo"));
    check(undo && !undo->enabled,
          "hard-unavailable standard action is disabled");
    check(undo
              && undo->text.contains(QStringLiteral("Nothing to undo"))
              && undo->visibleReason
                     == QStringLiteral("Nothing to undo."),
          "hard-unavailable action exposes its reason in visible text");

    const EditorContextMenuItem* expose =
        findItem(model, QStringLiteral("refactor.exposeSignalToTop"));
    check(expose && expose->enabled && !expose->executable,
          "missing hierarchy keeps Expose enterable for resolution");
    check(expose
              && expose->text.contains(QStringLiteral("active top"))
              && !expose->visibleReason.isEmpty(),
          "resolvable hierarchy failure is visible in the menu label");

    check(findItem(model, QStringLiteral("insight.stateTransitionGraph"))
              == nullptr,
          "irrelevant specialized Insight action is omitted");
    check(findItem(model, QStringLiteral("format.selection")) == nullptr,
          "selection-only action is omitted without a selection");

    const QStringList requiredContextActions = {
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("edit.cut"),
        QStringLiteral("edit.copy"),
        QStringLiteral("edit.paste"),
        QStringLiteral("select.all"),
        QStringLiteral("navigation.goLine"),
        QStringLiteral("edit.replace"),
        QStringLiteral("source.goToDefinition"),
        QStringLiteral("source.findReferences"),
        QStringLiteral("source.showRelationships"),
        QStringLiteral("insight.signalKernelGraph"),
        QStringLiteral("insight.signalUsageHotspot"),
        QStringLiteral("insight.stateTransitionGraph"),
        QStringLiteral("insight.moduleBlockDiagram"),
        QStringLiteral("refactor.createSignalDefinition"),
        QStringLiteral("refactor.editInstanceSlots"),
        QStringLiteral("refactor.createAssignmentQueue"),
        QStringLiteral("refactor.exposeSignalToTop"),
        QStringLiteral("format.commentLines"),
        QStringLiteral("format.uncommentLines"),
        QStringLiteral("format.indentLines"),
        QStringLiteral("format.unindentLines"),
        QStringLiteral("format.profile.structured"),
        QStringLiteral("format.profile.indentOnly"),
        QStringLiteral("format.onSave"),
        QStringLiteral("format.selection"),
        QStringLiteral("format.document")
    };
    for (const QString& actionId : requiredContextActions) {
        const ActionDescriptor* descriptor = findActionById(actionId);
        check(descriptor != nullptr,
              "every editor menu intent has one registry descriptor");
        check(descriptor
                  && descriptor->hasSurface(ActionSurface::ContextMenu),
              "every editor menu descriptor declares its context-menu alias");
    }

    EditorContextMenuRequest staleRequest;
    staleRequest.actionContext = currentContext();
    staleRequest.actionContext.semanticState =
        EditorActionSemanticState::Stale;
    staleRequest.symbolAvailable = true;
    staleRequest.capabilities = {
        capability(
            QStringLiteral("source.goToDefinition"),
            true,
            false,
            QStringLiteral("Semantic snapshot is stale; analyze the workspace."))
    };
    const EditorContextMenuModel staleModel =
        buildEditorContextMenuModel(staleRequest);
    const EditorContextMenuItem* staleDefinition =
        findItem(staleModel, QStringLiteral("source.goToDefinition"));
    check(staleDefinition
              && staleDefinition->enabled
              && !staleDefinition->executable,
          "recoverable semantic action remains clickable");
    check(staleDefinition
              && staleDefinition->text.contains(
                     QStringLiteral("Semantic snapshot is stale")),
          "recoverable semantic reason is visible without a tooltip");

    std::cout << (checks - failures) << "/" << checks
              << " editor context menu checks passed\n";
    return failures == 0 ? 0 : 1;
}
