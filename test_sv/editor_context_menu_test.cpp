#include "actionregistry.h"
#include "editorcontextmenumodel.h"

#include <QCoreApplication>
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
        capability(QStringLiteral("edit.undo"), true, false,
                   QStringLiteral("Nothing to undo."), true),
        capability(QStringLiteral("edit.redo"), true, true, {}, true),
        capability(QStringLiteral("edit.cut"), true, true, {}, true),
        capability(QStringLiteral("edit.copy"), true, true, {}, true),
        capability(QStringLiteral("edit.paste"), true, true, {}, true),
        capability(QStringLiteral("select.all"), true, true, {}, true),
        capability(QStringLiteral("navigation.goLine")),
        capability(QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen)),
        capability(QStringLiteral("source.goToDefinition")),
        capability(QStringLiteral("source.findReferences")),
        capability(QStringLiteral("source.showRelationships")),
        capability(QStringLiteral("insight.stateTransitionGraph"), false),
        capability(QStringLiteral("refactor.exposeSignalToTop")),
        capability(QStringLiteral("format.selection"), false),
        capability(QStringLiteral("format.document"))
    };

    const EditorContextMenuModel model =
        buildEditorContextMenuModel(request);
    check(model.sections.size() == 3,
          "removed context surfaces do not leave empty sections");
    check(model.sections.size() == 3
              && model.sections.at(0).section
                     == EditorContextMenuSection::Navigate
              && model.sections.at(1).section
                     == EditorContextMenuSection::Refactor
              && model.sections.at(2).section
                     == EditorContextMenuSection::Format,
          "remaining context-menu sections retain stable order");

    const QStringList removedContextActions = {
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("edit.cut"),
        QStringLiteral("edit.copy"),
        QStringLiteral("edit.paste"),
        QStringLiteral("select.all"),
        QStringLiteral("navigation.goLine"),
        QStringLiteral("source.goToDefinition"),
        QStringLiteral("source.findReferences"),
        QStringLiteral("source.showRelationships"),
    };
    for (const QString& actionId : removedContextActions) {
        check(findItem(model, actionId) == nullptr,
              "removed action is absent from the editor context menu");
    }

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
    const EditorContextMenuItem* temporaryEditor =
        findItem(model, QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen));
    check(temporaryEditor
              && temporaryEditor->enabled
              && temporaryEditor->executable
              && temporaryEditor->section
                     == EditorContextMenuSection::Navigate,
          "current editor exposes the unified temporary-editor navigation Action");

    const QStringList requiredContextActions = {
        QStringLiteral("edit.replace"),
        QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen),
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
              "every retained editor menu intent has one registry descriptor");
        check(descriptor
                  && descriptor->hasSurface(ActionSurface::ContextMenu),
              "every retained editor menu descriptor declares its surface");
    }

    const QStringList retainedShortcutActions = {
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("edit.cut"),
        QStringLiteral("edit.copy"),
        QStringLiteral("edit.paste"),
        QStringLiteral("select.all"),
        QStringLiteral("navigation.goLine"),
        QStringLiteral("source.goToDefinition"),
    };
    for (const QString& actionId : retainedShortcutActions) {
        const ActionDescriptor* descriptor = findActionById(actionId);
        check(descriptor != nullptr && !descriptor->defaultShortcut.isEmpty(),
              "removed context action retains its keyboard shortcut");
        check(descriptor
                  && !descriptor->hasSurface(ActionSurface::ContextMenu),
              "removed context action has no context-menu surface");
    }
    check(findActionById(QStringLiteral("source.findReferences")) == nullptr
              && findActionById(
                     QStringLiteral("source.showRelationships")) == nullptr,
          "standalone references and relationships actions are removed");

    std::cout << (checks - failures) << "/" << checks
              << " editor context menu checks passed\n";
    return failures == 0 ? 0 : 1;
}
