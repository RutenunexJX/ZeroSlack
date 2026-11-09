#include "simulationruncoordinator.h"
#include "workbench.h"
#include "../core/workspace.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "shareddocument.h"
#include "mycodeeditor.h"
#include <QDir>
#include <QFileInfo>
#include <QTextDocument>

namespace simdock {
SimulationRunCoordinator::SimulationRunCoordinator(TabManager* tabs, WorkspaceManager* workspaces, Workbench* view, QObject* parent)
    : QObject(parent), m_tabs(tabs), m_workspaces(workspaces), m_view(view), m_controller(view->controller())
{
    setObjectName(QStringLiteral("simulationRunCoordinator"));
    const QPointer<SimulationRunCoordinator> owner(this);
    view->setRunHandler([owner] { return owner ? owner->run() : QStringLiteral("The workspace has closed."); });
    view->setTargetHandler([owner](const QString& file) { return owner ? owner->selectTarget(file) : QStringLiteral("The workspace has closed."); });
}
SimulationBuildRequest SimulationRunCoordinator::capture(const SimulationTarget& target, QString* error, bool allowPending) const
{
    error->clear();
    SimulationBuildRequest request; request.target = target;
    if (!m_workspaces || !m_tabs || !m_controller) { *error = tr("The workspace is unavailable."); return request; }
    request.project = m_workspaces->projectSnapshot();
    if (request.project.workspaceRoot.isEmpty()
        || QFileInfo(request.project.workspaceRoot).canonicalFilePath() != m_controller->workspace()) {
        *error = tr("Open the target's workspace before running simulation."); return request;
    }
    if (!allowPending && (!request.project.sourceDiscoveryComplete || m_workspaces->isWorkspaceScanActive())) {
        *error = tr("Workspace discovery is still running. Run again when it completes."); return request;
    }
    request.includeDirectories = m_controller->includeDirectories();
    request.definitionSources = m_controller->definitionSources();
    for (auto* editor : m_tabs->openEditors()) {
        const auto file = editor->documentFileName();
        if (file.isEmpty() || !insideWorkspace(request.project.workspaceRoot, file)) continue;
        const auto key = SemanticInputCapture::pathKey(file);
        auto* document = m_tabs->sharedDocumentForEditor(editor);
        if (!document || request.documentInstances.contains(key)) continue;
        request.documentInstances.insert(key, document->instanceSerial());
        request.documentRevisions.insert(key, document->textRevision());
        if (editor->document()->isModified()) request.overrides.insert(key, editor->cachedDocumentText());
    }
    return request;
}
QString SimulationRunCoordinator::selectTarget(const QString& selected)
{
    if (!m_tabs || !m_view || !m_controller || m_controller->busy()) return tr("The simulation workspace is unavailable or busy.");
    QString file = selected;
    if (file.isEmpty()) {
        QPointer<MyCodeEditor> editor = m_tabs->editorActionTarget();
        if (!editor) return tr("Open a Verilog or SystemVerilog TB in the editor.");
        if (editor->documentFileName().isEmpty() && !m_tabs->saveEditorView(editor, true))
            return tr("Save the unnamed TB before selecting it.");
        if (!editor) return tr("The TB editor closed during Save As.");
        file = editor->documentFileName();
    }
    const auto suffix = QFileInfo(file).suffix().toLower();
    if (suffix != "sv" && suffix != "v") return tr("Select a Verilog or SystemVerilog source file as the TB.");
    QString error;
    const auto inputs = capture({file, {}}, &error, true);
    if (!error.isEmpty()) return error;
    QMap<QString, QByteArray> overrides;
    for (auto it = inputs.overrides.cbegin(); it != inputs.overrides.cend(); ++it) overrides.insert(it.key(), it.value().toUtf8());
    return m_view->selectTarget(file, overrides);
}
QString SimulationRunCoordinator::check(const SimulationBuildRequest& request, bool checkCatalog) const
{
    if (!m_tabs || !m_workspaces || !m_controller) return tr("The simulation workspace closed during preparation.");
    if (checkCatalog && m_workspaces->projectSnapshot().semanticIdentity() != request.project.semanticIdentity())
        return tr("Workspace inputs changed during preparation. Run again with the current catalog.");
    QSet<QString> observed;
    for (auto* editor : m_tabs->openEditors()) {
        const auto file = editor->documentFileName();
        if (file.isEmpty() || !insideWorkspace(request.project.workspaceRoot, file)) continue;
        const auto key = SemanticInputCapture::pathKey(file);
        if (observed.contains(key)) continue;
        observed.insert(key);
        const auto* document = m_tabs->sharedDocumentForEditor(editor);
        if (!document) continue;
        if (request.documentInstances.contains(key)) {
            if (document->instanceSerial() != request.documentInstances.value(key)
                || document->textRevision() != request.documentRevisions.value(key))
                return tr("An open document changed during simulation preparation: %1").arg(file);
        } else if (editor->document()->isModified()) {
            return tr("An additional document was modified during simulation preparation: %1").arg(file);
        }
    }
    return {};
}
QString SimulationRunCoordinator::saveParticipating(const SimulationBuildPlan& plan, SimulationBuildRequest& request)
{
    const auto issue = check(request); if (!issue.isEmpty()) return issue;
    bool saved = false;
    for (const auto& path : plan.participatingFiles) {
        QPointer<MyCodeEditor> selected;
        for (auto* editor : m_tabs->openEditors())
            if (SemanticInputCapture::pathKey(editor->documentFileName()) == path) { selected = editor; break; }
        if (!selected) {
            if (request.overrides.contains(path)) return tr("A modified input closed during preparation: %1").arg(path);
            continue;
        }
        if (!selected->document()->isModified()) continue;
        const auto* document = m_tabs->sharedDocumentForEditor(selected);
        if (!document || document->instanceSerial() != request.documentInstances.value(path)
            || document->textRevision() != request.documentRevisions.value(path))
            return tr("An input changed before it could be saved: %1").arg(path);
        if (selected->documentReadOnly()) return tr("A participating input is read-only: %1").arg(path);
        if (!m_tabs->saveEditorView(selected))
            return tr("Saving a participating input was cancelled or failed: %1").arg(path);
        if (!selected || selected->document()->isModified())
            return tr("A participating input changed while it was being saved: %1").arg(path);
        saved = true;
    }
    QString error;
    auto current = capture(request.target, &error, true);
    if (!error.isEmpty()) return error;
    current.refreshCatalog = saved;
    request = std::move(current);
    return {};
}
WorkspaceSimulationController::SaveInputs SimulationRunCoordinator::saveInputs()
{
    const QPointer<SimulationRunCoordinator> owner(this);
    return [owner](const SimulationBuildPlan& plan, SimulationBuildRequest& request) {
        return owner ? owner->saveParticipating(plan, request) : QStringLiteral("The workspace closed during preparation.");
    };
}
WorkspaceSimulationController::CheckCurrent SimulationRunCoordinator::checkCurrent()
{
    const QPointer<SimulationRunCoordinator> owner(this);
    return [owner](const SimulationBuildRequest& request) {
        return owner ? owner->check(request) : QStringLiteral("The workspace closed during preparation.");
    };
}
WorkspaceSimulationController::AwaitInputs SimulationRunCoordinator::awaitInputs()
{
    const QPointer<SimulationRunCoordinator> owner(this);
    return [owner](SimulationBuildRequest& request, QString& error) {
        if (!owner || !owner->m_workspaces || !owner->m_controller) {
            error = QStringLiteral("The workspace closed during preparation."); return false;
        }
        error = owner->check(request, false);
        if (!error.isEmpty()) return false;
        const auto current = owner->m_workspaces->projectSnapshot();
        auto expected = request.project;
        expected.systemVerilogFiles = current.systemVerilogFiles;
        expected.sourceDiscoveryComplete = current.sourceDiscoveryComplete;
        if (expected.semanticIdentity() != current.semanticIdentity()) {
            error = QStringLiteral("Workspace configuration changed during simulation preparation."); return false;
        }
        if (owner->m_workspaces->isWorkspaceScanActive() || !current.sourceDiscoveryComplete) return false;
        request.project = current;
        return true;
    };
}
QString SimulationRunCoordinator::run()
{
    if (!m_controller || !m_view || m_controller->busy()) return tr("The simulation workspace is unavailable or busy.");
    if (m_controller->target().isEmpty()) { const auto issue = selectTarget(); if (!issue.isEmpty()) return issue; }
    QString error;
    auto request = capture(m_controller->target(), &error, true);
    if (!error.isEmpty()) return error;
    m_controller->prepare(m_view->simulator(), std::move(request), m_view->waveTheme(), saveInputs(), checkCurrent(), awaitInputs());
    return {};
}
}
