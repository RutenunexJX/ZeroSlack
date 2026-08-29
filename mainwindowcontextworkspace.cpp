#include "mainwindow.h"

#include "contextworkspacecontroller.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"
#include "navigationcommandcoordinator.h"
#include "panellayoutcontroller.h"
#include "pinloomcodelinkcoordinator.h"
#include "pinloomcontextprovider.h"
#include "pinloomhostclient.h"
#include "settingscenterpanel.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorsearchprovider.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticindex.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "wavesimulationconfiguration.h"
#include "workspacemanager.h"

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QStatusBar>
#include <QTextCursor>

#include <utility>

bool MainWindow::revealSuiteSource(const QString& filePath,
                                   int lineNumber,
                                   int columnNumber)
{
    if (!navigationCommandCoordinator || filePath.trimmed().isEmpty())
        return false;
    const bool revealed =
        navigationCommandCoordinator->navigateToFileAndLineAndFlash(
            filePath,
            qMax(1, lineNumber),
            qMax(1, columnNumber));
    if (revealed) {
        show();
        raise();
        activateWindow();
    }
    return revealed;
}

void MainWindow::setupContextWorkspace()
{
    if (!editorSplitHost || contextWorkspaceController)
        return;
    contextWorkspaceController =
        std::make_unique<ContextWorkspaceController>(
            this,
            editorSplitHost,
            this);
    pinloomCodeLinkCoordinator =
        std::make_unique<PinloomCodeLinkCoordinator>(
            contextWorkspaceController.get());
    if (panelLayoutController) {
        panelLayoutController->registerSidePanel(
            QStringLiteral("contextWorkspace"),
            contextWorkspaceController->dockWidget());
    }
    if (workspaceManager) {
        const QString workspaceRoot =
            workspaceManager->isWorkspaceOpen()
            ? workspaceManager->getWorkspacePath()
            : QString();
        contextWorkspaceController->setWorkspaceRoot(workspaceRoot);
        pinloomCodeLinkCoordinator->setWorkspaceRoot(workspaceRoot);
        connect(workspaceManager.get(),
                &WorkspaceManager::workspaceActivated,
                contextWorkspaceController.get(),
                [this,
                 controller = contextWorkspaceController.get(),
                 coordinator = pinloomCodeLinkCoordinator.get()](
                    int,
                    const QString&,
                    const QString& path) {
                    controller->setWorkspaceRoot(path);
                    coordinator->setWorkspaceRoot(path);
                    requestLiveInsightUpdates();
                });
        connect(workspaceManager.get(),
                &WorkspaceManager::workspaceClosed,
                contextWorkspaceController.get(),
                [this,
                 controller = contextWorkspaceController.get(),
                 coordinator = pinloomCodeLinkCoordinator.get()]() {
                    controller->setWorkspaceRoot({});
                    coordinator->setWorkspaceRoot({});
                    requestLiveInsightUpdates();
                });
    }

    auto temporaryProvider =
        std::make_unique<TemporaryEditorContextProvider>(
            tabManager.get());
    temporaryProvider->setSearchProvider(
        [this](const QString& rawQuery)
            -> EditorSearchCandidates {
            return temporaryEditorSearchProvider
                ? temporaryEditorSearchProvider->query(rawQuery)
                : EditorSearchCandidates{};
        });
    contextWorkspaceController->registerProvider(
        std::move(temporaryProvider));

    liveInsightSession =
        std::make_unique<LiveInsightSession>(this);
    const auto summaryBuilder = [](
        const LiveInsightBuildRequest& request,
        const LiveInsightCancellationToken& cancellation) {
        if (cancellation.isCancellationRequested())
            return LiveInsightBuildResult::cancellation(request);
        if (!request.isWellFormed()) {
            return LiveInsightBuildResult::failure(
                request,
                QStringLiteral("Malformed Live Insight request."));
        }
        QVariantMap payload = request.input;
        payload.insert(
            QStringLiteral("kind"),
            liveInsightKindId(request.key.kind));
        payload.insert(
            QStringLiteral("generation"),
            QVariant::fromValue<qulonglong>(request.generation));
        payload.insert(
            QStringLiteral("requestKey"),
            request.key.toVariantMap());
        if (cancellation.isCancellationRequested())
            return LiveInsightBuildResult::cancellation(request);
        return LiveInsightBuildResult::success(request, payload);
    };
    for (LiveInsightKind kind : {
             LiveInsightKind::Kernel,
             LiveInsightKind::Module,
             LiveInsightKind::State,
             LiveInsightKind::Hotspot,
             LiveInsightKind::Wave}) {
        liveInsightSession->setBuilder(kind, summaryBuilder);
    }
    connect(
        liveInsightSession.get(),
        &LiveInsightSession::snapshotChanged,
        this,
        [this](LiveInsightKind kind,
               const LiveInsightSnapshot& snapshot) {
            if (snapshot.phase != LiveInsightPhase::Ready
                || snapshot.stale
                || snapshot.publishedGeneration
                       != snapshot.requestedGeneration) {
                return;
            }
            refreshLiveInsightToolPages(
                static_cast<int>(kind));
        });

    for (LiveInsightKind kind : {
             LiveInsightKind::Kernel,
             LiveInsightKind::Module,
             LiveInsightKind::Hotspot,
             LiveInsightKind::State,
             LiveInsightKind::Wave}) {
        auto provider =
            std::make_unique<LiveInsightsContextProvider>(
                kind,
                liveInsightSession.get());
        provider->setFullViewHandler(
            [this](const ContextResource& resource) {
                openLiveInsightFullView(resource);
            });
        provider->setPinRequestHandler(
            [this](bool pinned, const ContextResource& resource) {
                if (!contextWorkspaceController)
                    return;
                if (pinned) {
                    contextWorkspaceController->pinPeek();
                } else {
                    contextWorkspaceController->unpinResource(
                        resource.stableKey());
                }
            });
        contextWorkspaceController->registerProvider(
            std::move(provider));
    }
    if (semanticDocks && semanticDocks->refreshCoordinator()) {
        semanticDocks->refreshCoordinator()
            ->setLiveInsightOpenHandler(
                [this](LiveInsightKind kind,
                       const QString& symbolName,
                       const QString& fileName,
                       const QString& moduleName,
                       const QString& signalAccessPath) {
                    return openLiveInsightFromSourceAction(
                        kind,
                        symbolName,
                        fileName,
                        moduleName,
                        signalAccessPath);
                });
    }
    connect(
        contextWorkspaceController.get(),
        &ContextWorkspaceController::fullViewRequested,
        this,
        [this](const ContextResource& resource) {
            if (LiveInsightsContextProvider::isWorkbenchProviderId(
                    resource.providerId)) {
                openLiveInsightFullView(resource);
            }
        });

    pinloomHostClient = std::make_unique<PinloomHostClient>(this);
    if (settingsCenterPanel) {
        pinloomHostClient->setExecutablePath(
            settingsCenterPanel->snapshot()
                .value(QStringLiteral(
                    "integration.pinloomExecutablePath"))
                .toString());
    }
    auto pinloomProvider =
        std::make_unique<PinloomContextProvider>(
            pinloomHostClient.get());
    pinloomProvider->setLinkHandler(
        [this](const QVariantMap& sourceMap,
               const PinloomHostEntry& entry,
               QString* failureReason) {
            if (!pinloomCodeLinkCoordinator) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "Code-link storage is unavailable.");
                }
                return false;
            }
            return pinloomCodeLinkCoordinator->attachLink(
                sourceMap, entry, failureReason);
        });
    contextWorkspaceController->registerProvider(
        std::move(pinloomProvider));

    requestLiveInsightUpdates();
}

LiveInsightToolContext MainWindow::activeLiveInsightToolContext() const
{
    LiveInsightToolContext context;
    context.workspaceRoot =
        workspaceManager && workspaceManager->isWorkspaceOpen()
        ? workspaceManager->getWorkspacePath()
        : QString();
    context.workspaceId = context.workspaceRoot.trimmed().isEmpty()
        ? QStringLiteral("standalone")
        : QDir::cleanPath(context.workspaceRoot);
    if (!tabManager)
        return context;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor)
        return context;
    const DocumentSnapshot document =
        tabManager->getCurrentDocumentMetadata();
    context.documentId = document.documentId.trimmed();
    if (context.documentId.isEmpty())
        context.documentId = document.fileName.trimmed();
    context.fileName = document.fileName;
    context.documentText = editor->cachedDocumentText();
    context.dirty = document.dirty;
    context.moduleName = document.currentModuleName;
    if (SharedDocument* sharedDocument =
            tabManager->sharedDocumentForEditor(editor)) {
        context.documentRevision = sharedDocument->textRevision();
    } else {
        context.documentRevision =
            static_cast<quint64>(qMax(0, document.textVersion));
    }
    context.semanticRevision =
        SemanticIndex::getInstance()->snapshotToken().revision;

    QTextCursor symbolCursor = editor->textCursor();
    if (!symbolCursor.hasSelection())
        symbolCursor.select(QTextCursor::WordUnderCursor);
    context.signalName = symbolCursor.selectedText().trimmed();

    const EditorAlwaysScopeTarget alwaysScope =
        editor->currentAlwaysScopeTarget();
    if (alwaysScope.ok()) {
        context.scopeStartPosition = alwaysScope.startPosition;
        context.scopeEndPosition = alwaysScope.endPosition;
        context.scopeLabel = alwaysScope.label;
        context.scopeStartLineZeroBased = alwaysScope.startLine;
    } else {
        const EditorModuleScopeTarget moduleScope =
            editor->currentModuleScopeTarget();
        if (moduleScope.ok()) {
            context.scopeStartPosition = moduleScope.startPosition;
            context.scopeEndPosition = moduleScope.endPosition;
            context.scopeLabel = moduleScope.label;
            context.scopeStartLineZeroBased = moduleScope.startLine;
            if (context.moduleName.trimmed().isEmpty())
                context.moduleName = moduleScope.moduleName;
        }
    }
    return context;
}

QString MainWindow::liveInsightWaveformLibraryPath() const
{
    const QString explicitPath =
        qEnvironmentVariable("ZEROSLACK_WAVEWIDGETS_LIBRARY");
    if (QFileInfo(explicitPath).isFile())
        return QFileInfo(explicitPath).absoluteFilePath();
    const QString configured =
        WaveSimulationConfiguration().toolPaths().widgetLibrary;
    return QFileInfo(configured).isFile()
        ? QFileInfo(configured).absoluteFilePath()
        : QString();
}

void MainWindow::requestLiveInsightUpdates()
{
    if (!liveInsightSession || !tabManager)
        return;
    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor) {
        for (LiveInsightKind kind : {
                 LiveInsightKind::Kernel,
                 LiveInsightKind::Module,
                 LiveInsightKind::State,
                 LiveInsightKind::Hotspot,
                 LiveInsightKind::Wave}) {
            liveInsightSession->clear(kind);
        }
        return;
    }

    const DocumentSnapshot document =
        tabManager->getCurrentDocumentMetadata();
    SharedDocument* sharedDocument =
        tabManager->sharedDocumentForEditor(editor);
    const LiveInsightToolContext context =
        activeLiveInsightToolContext();
    const QString workspaceId = !context.workspaceRoot.isEmpty()
        ? QDir::cleanPath(context.workspaceRoot)
        : QStringLiteral("standalone");
    QString documentId = document.documentId.trimmed();
    if (documentId.isEmpty())
        documentId = document.fileName.trimmed();
    if (documentId.isEmpty()) {
        documentId = QStringLiteral("untitled:%1")
            .arg(reinterpret_cast<quintptr>(editor),
                 0,
                 16);
    }
    const quint64 documentRevision = sharedDocument
        ? sharedDocument->textRevision()
        : static_cast<quint64>(qMax(0, document.textVersion));
    const quint64 semanticRevision =
        SemanticIndex::getInstance()->snapshotToken().revision;
    const QString baseContextKey =
        QStringLiteral("%1|%2|%3|%4:%5")
            .arg(context.moduleName,
                 context.signalName,
                 context.scopeLabel)
            .arg(context.scopeStartPosition)
            .arg(context.scopeEndPosition);

    for (LiveInsightKind kind : {
             LiveInsightKind::Kernel,
             LiveInsightKind::Module,
             LiveInsightKind::State,
             LiveInsightKind::Hotspot,
             LiveInsightKind::Wave}) {
        LiveInsightRequestKey key;
        key.kind = kind;
        key.workspaceId = workspaceId;
        key.documentId = documentId;
        key.documentRevision = documentRevision;
        key.semanticRevision = semanticRevision;
        key.contextKey = QStringLiteral("%1|%2")
            .arg(liveInsightKindId(kind), baseContextKey);

        QVariantMap input;
        input.insert(QStringLiteral("fileName"), context.fileName);
        input.insert(QStringLiteral("moduleName"), context.moduleName);
        input.insert(QStringLiteral("signalName"), context.signalName);
        input.insert(QStringLiteral("scopeLabel"), context.scopeLabel);
        input.insert(QStringLiteral("dirty"), context.dirty);
        switch (kind) {
        case LiveInsightKind::Kernel:
            input.insert(QStringLiteral("title"),
                         QStringLiteral("Signal Kernel Graph"));
            input.insert(
                QStringLiteral("summary"),
                context.signalName.trimmed().isEmpty()
                    ? QStringLiteral("Select a signal to trace its data-flow kernel.")
                    : QStringLiteral("%1 · inputs and outputs")
                          .arg(context.signalName));
            break;
        case LiveInsightKind::Module:
            input.insert(QStringLiteral("title"),
                         QStringLiteral("Module Diagram"));
            input.insert(
                QStringLiteral("summary"),
                context.moduleName.trimmed().isEmpty()
                    ? QStringLiteral("No module scope selected.")
                    : QStringLiteral("%1 · hierarchy and connectivity")
                          .arg(context.moduleName));
            break;
        case LiveInsightKind::State:
            input.insert(QStringLiteral("title"),
                         QStringLiteral("State Diagram"));
            input.insert(
                QStringLiteral("summary"),
                context.moduleName.trimmed().isEmpty()
                    ? QStringLiteral("No FSM scope selected.")
                    : QStringLiteral("%1 · state transitions")
                          .arg(context.moduleName));
            break;
        case LiveInsightKind::Hotspot:
            input.insert(QStringLiteral("title"),
                         QStringLiteral("Signal Hotspot"));
            input.insert(
                QStringLiteral("summary"),
                context.signalName.trimmed().isEmpty()
                    ? QStringLiteral("Select a signal to rank its evidence.")
                    : QStringLiteral("%1 · Track / Matrix evidence")
                          .arg(context.signalName));
            break;
        case LiveInsightKind::Wave:
            input.insert(QStringLiteral("title"),
                         QStringLiteral("Symbolic Preview"));
            input.insert(
                QStringLiteral("summary"),
                QStringLiteral("%1\nRTL-derived symbolic values; not a simulation result.")
                    .arg(context.scopeLabel.trimmed().isEmpty()
                             ? QStringLiteral("Current RTL scope")
                             : context.scopeLabel));
            input.insert(
                QStringLiteral("provenance"),
                QStringLiteral("Symbolic Preview"));
            break;
        }
        liveInsightSession->requestUpdate(key, input);
    }
}

void MainWindow::refreshLiveInsightToolPages(int kindValue)
{
    const LiveInsightKind kind =
        static_cast<LiveInsightKind>(kindValue);
    LiveInsightToolPage* page =
        liveInsightToolPages.value(kindValue).data();
    if (!page || !page->hasVisibleSurface())
        return;
    page->setContext(activeLiveInsightToolContext());
}

bool MainWindow::openLiveInsightFromSourceAction(
    LiveInsightKind kind,
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath)
{
    if (!contextWorkspaceController || !tabManager)
        return false;

    LiveInsightToolContext context = activeLiveInsightToolContext();
    if (!fileName.trimmed().isEmpty())
        context.fileName = fileName;
    if (kind == LiveInsightKind::Module) {
        context.moduleName = !symbolName.trimmed().isEmpty()
            ? symbolName
            : moduleName;
        context.signalName.clear();
        context.signalAccessPath.clear();
    } else {
        if (!moduleName.trimmed().isEmpty())
            context.moduleName = moduleName;
        context.signalName = symbolName;
        context.signalAccessPath = signalAccessPath;
    }

    QVariantMap state;
    state.insert(QStringLiteral("followEditor"), true);
    state.insert(QStringLiteral("pinned"), true);
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            kind,
            contextWorkspaceController->workspaceRoot(),
            state);
    QString failureReason;
    if (!contextWorkspaceController->openResource(
            resource,
            ContextOpenMode::Peek,
            &failureReason)
        || !contextWorkspaceController->openResource(
            resource,
            ContextOpenMode::Pinned,
            &failureReason)) {
        if (statusBar()) {
            statusBar()->showMessage(
                failureReason.trimmed().isEmpty()
                    ? QStringLiteral(
                          "Live Insights sidebar is unavailable.")
                    : failureReason,
                5000);
        }
        return false;
    }

    requestLiveInsightUpdates();
    openLiveInsightFullView(resource, &context);
    return true;
}

void MainWindow::openLiveInsightFullView(
    const ContextResource& resource,
    const LiveInsightToolContext* contextOverride)
{
    if (!tabManager)
        return;
    LiveInsightKind kind = LiveInsightKind::Module;
    if (!LiveInsightsContextProvider::kindFromResource(
            resource, &kind)) {
        return;
    }
    const LiveInsightToolContext context = contextOverride
        ? *contextOverride
        : activeLiveInsightToolContext();
    const QString stableId =
        QStringLiteral("live-insight:%1")
            .arg(liveInsightKindId(kind));
    if (auto* existing = dynamic_cast<LiveInsightToolPage*>(
            tabManager->toolPage(stableId))) {
        existing->setContext(context);
        tabManager->activateToolPage(stableId);
        return;
    }

    auto* page = new LiveInsightToolPage(kind);
    page->setWaveformLibraryPath(
        liveInsightWaveformLibraryPath());
    page->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            return revealSuiteSource(fileName, line, column);
        });
    page->setStatusHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    page->setVisibilityHandler(
        [owner = QPointer<MainWindow>(this),
         session = QPointer<LiveInsightSession>(
             liveInsightSession.get()),
         page,
         kind](bool visible) {
            if (session) {
                session->setConsumerVisible(page, kind, visible);
                const LiveInsightSnapshot snapshot =
                    session->snapshot(kind);
                if (visible && owner
                    && snapshot.phase == LiveInsightPhase::Ready
                    && !snapshot.stale
                    && snapshot.publishedGeneration
                           == snapshot.requestedGeneration) {
                    page->setContext(
                        owner->activeLiveInsightToolContext());
                }
            }
        });
    page->setRefreshHandler(
        [owner = QPointer<MainWindow>(this),
         target = QPointer<LiveInsightToolPage>(page)]() {
            if (!owner || !target)
                return;
            target->setContext(
                owner->activeLiveInsightToolContext());
            owner->requestLiveInsightUpdates();
        });
    page->setContext(context);
    const QString title = kind == LiveInsightKind::Wave
        ? QStringLiteral("Symbolic Wave Preview")
        : kind == LiveInsightKind::Kernel
            ? QStringLiteral("Signal Kernel Graph")
            : kind == LiveInsightKind::Module
                ? QStringLiteral("Module Block Diagram")
                : kind == LiveInsightKind::Hotspot
                    ? QStringLiteral("Signal Hotspot")
                    : QStringLiteral("State Transition Graph");
    tabManager->openToolPage(page, stableId, title);
    liveInsightToolPages.insert(static_cast<int>(kind), page);
    if (liveInsightSession) {
        liveInsightSession->setConsumerVisible(
            page, kind, page->hasVisibleSurface());
    }
    requestLiveInsightUpdates();
}
