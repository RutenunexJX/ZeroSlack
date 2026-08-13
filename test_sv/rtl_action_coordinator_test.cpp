#include "actionregistry.h"
#include "mycodeeditor.h"
#include "panellayoutcontroller.h"
#include "rtlhighriskeditpanel.h"
#include "rtlactioncoordinator.h"
#include "semanticdockcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

QString normalized(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeFile(const QString& path, const QString& text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size();
}

const ActionDescriptor* action(const char* id)
{
    return findActionById(QString::fromLatin1(id));
}
}

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);

    const ActionDescriptor* renameAction =
        action(ActionIds::RtlRename);
    const ActionDescriptor* connectionAction =
        action(ActionIds::RtlConnectionTransform);
    const ActionDescriptor* instancePairAction =
        action(ActionIds::RtlConnectInstancePair);
    const ActionDescriptor* multiSignalAction =
        action(ActionIds::RtlPropagateMultipleSignals);
    expect("all four RTL action descriptors exist",
           renameAction && connectionAction
               && instancePairAction && multiSignalAction);
    if (!renameAction || !connectionAction
        || !instancePairAction || !multiSignalAction) {
        return 1;
    }

    RtlActionCoordinator unavailable(
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {});
    ActionInvocation missingInvocation;
    const ActionDescriptor* ownedActions[] = {
        renameAction,
        connectionAction,
        instancePairAction,
        multiSignalAction};
    bool allMissingDependenciesRejected = true;
    for (const ActionDescriptor* descriptor : ownedActions) {
        const ActionExecutionResult result =
            unavailable.execute(*descriptor, missingInvocation);
        allMissingDependenciesRejected =
            allMissingDependenciesRejected
            && result.handled
            && !result.succeeded
            && !result.failureReason.isEmpty();
    }
    expect("missing dependencies reject every owned route",
           allMissingDependenciesRejected);

    ActionDescriptor foreignAction;
    foreignAction.executionRoute =
        QStringLiteral("workspace.foreign");
    const ActionExecutionResult foreignResult =
        unavailable.execute(foreignAction, missingInvocation);
    expect("unowned routes are left unhandled",
           !foreignResult.handled && !foreignResult.succeeded);

    QTemporaryDir temporaryRoot;
    expect("temporary workspace is valid",
           temporaryRoot.isValid());
    if (!temporaryRoot.isValid())
        return 1;

    const QString workspacePath = normalized(
        QDir(temporaryRoot.path()).filePath(
            QStringLiteral("workspace")));
    const QString childFile = normalized(
        QDir(workspacePath).filePath(
            QStringLiteral("child.sv")));
    const QString topFile = normalized(
        QDir(workspacePath).filePath(
            QStringLiteral("top.sv")));
    const QString childText = QStringLiteral(
        "module child #(parameter int WIDTH = 8) (\n"
        "    input logic [WIDTH-1:0] data_i,\n"
        "    output logic [WIDTH-1:0] data_o\n"
        ");\n"
        "    assign data_o = data_i;\n"
        "endmodule\n");
    const QString topText = QStringLiteral(
        "module top;\n"
        "    logic [7:0] data_i;\n"
        "    logic [7:0] data_o;\n"
        "    child u_child (\n"
        "        .data_i(data_i),\n"
        "        .data_o(data_o)\n"
        "    );\n"
        "endmodule\n");
    expect("RTL workspace fixtures are written",
           writeFile(childFile, childText)
               && writeFile(topFile, topText));

    QMainWindow host;
    auto* tabWidget = new QTabWidget(&host);
    host.setCentralWidget(tabWidget);
    TabManager tabManager(tabWidget);
    WorkspaceManager workspaceManager;
    workspaceManager
        .setRecentWorkspacePersistenceEnabledForTesting(false);
    expect("workspace opens",
           workspaceManager.openWorkspace(workspacePath));
    tabManager.setWorkspaceScope(
        {workspacePath}, workspacePath);
    expect("workspace scan state is deterministic",
           workspaceManager.restoreSessionScanState(
               {childFile, topFile}, true));
    expect("subject document opens",
           tabManager.openFileInTab(childFile));
    MyCodeEditor* editor = tabManager.getCurrentEditor();
    expect("subject editor is current", editor != nullptr);
    if (!editor)
        return 1;

    const int subjectPosition = childText.indexOf(
        QStringLiteral("data_i"));
    QTextCursor cursor(editor->document());
    cursor.setPosition(subjectPosition + 1);
    editor->setTextCursor(cursor);

    SemanticDockCoordinator semanticDocks(
        &host,
        &tabManager,
        &workspaceManager,
        nullptr,
        nullptr);
    semanticDocks.setup();
    PanelLayoutController panelLayout(&host);
    expect("High+Diff panel is registered",
           panelLayout.registerBottomPanel(
               RtlHighRiskEditPanelCoordinator::panelId(),
               semanticDocks
                   .rtlHighRiskEditPanelCoordinator()
                   ->dock()));
    panelLayout.finalize();

    EditorActionSemanticState semanticState =
        EditorActionSemanticState::Stale;
    QString semanticFailure =
        QStringLiteral("fixture semantic snapshot is stale");
    RtlActionCoordinatorCallbacks callbacks;
    callbacks.resolveContext =
        [&workspacePath,
         &semanticState,
         &semanticFailure](
            const EditorSemanticContext& editorContext) {
            EditorActionContext result;
            result.workspacePath = workspacePath;
            result.fileName = editorContext.fileName;
            result.moduleName = editorContext.moduleName;
            result.semanticState = semanticState;
            result.semanticError = semanticFailure;
            result.semanticSnapshotRevision =
                SemanticIndex::getInstance()
                    ->snapshotRevision();
            return result;
        };
    int showPanelCalls = 0;
    callbacks.showPanel =
        [&showPanelCalls](const QString&) {
            ++showPanelCalls;
        };
    RtlActionCoordinator coordinator(
        &tabManager,
        &workspaceManager,
        &semanticDocks,
        &panelLayout,
        &host,
        std::move(callbacks));

    ActionInvocation renameInvocation;
    renameInvocation.workspaceId = workspacePath;
    renameInvocation.mode = ActionExecutionMode::DryRun;
    renameInvocation.parameters.insert(
        QStringLiteral("newName"),
        QStringLiteral("payload_i"));
    const ActionExecutionResult staleResult =
        coordinator.execute(
            *renameAction, renameInvocation);
    expect("stale semantic context blocks launch",
           staleResult.handled
               && !staleResult.succeeded
               && staleResult.failureReason
                      == semanticFailure);

    SemanticIndex* index = SemanticIndex::getInstance();
    const std::shared_ptr<const SemanticIndexSnapshot>
        previousSnapshot = index->snapshot();
    const QHash<QString, QString> fileContents = {
        {childFile, childText},
        {topFile, topText}};
    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractOverlayWorkspaceSymbolRecords(
            fileContents,
            {},
            {},
            nullptr,
            nullptr,
            {childFile, topFile});
    index->setSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                records,
                {},
                {},
                fileContents)));
    semanticState = EditorActionSemanticState::Current;
    semanticFailure.clear();

    const ActionExecutionResult launched =
        coordinator.execute(
            *renameAction, renameInvocation);
    RtlHighRiskEditPanelCoordinator* highRisk =
        semanticDocks
            .rtlHighRiskEditPanelCoordinator();
    expect("current semantic context starts rename session",
           launched.handled
               && launched.succeeded
               && launched.dryRun
               && highRisk
               && highRisk->activeKind()
                      == RtlHighRiskEditKind::Rename
               && highRisk->panel()->state()
                      == RtlHighRiskEditPanelState::Editing);

    const RtlHighRiskEditPanelOutcome preview =
        highRisk->requestPreview();
    expect("dry-run preview uses the real High+Diff workflow",
           preview.panelState
                   == RtlHighRiskEditPanelState::
                       DryRunPreviewReady
               && preview.hasStructuredPreview
               && preview.fileCount >= 1
               && preview.editCount >= 1
               && highRisk->panel()
                      ->displayedDiffText()
                      .contains(
                          QStringLiteral("payload_i")));

    QFile unchangedChild(childFile);
    expect("dry-run preview does not write workspace files",
           unchangedChild.open(QIODevice::ReadOnly)
               && QString::fromUtf8(
                      unchangedChild.readAll())
                      == childText);
    expect("rename launch uses panel layout directly",
           showPanelCalls == 0);

    highRisk->resetForWorkspaceClose();
    if (previousSnapshot)
        index->setSnapshot(previousSnapshot);
    else
        index->clearSnapshot();

    std::printf("rtl_action_coordinator_test: %d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
