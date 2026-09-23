#include "mainwindow.h"

#include "contextworkspacecontroller.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"
#include "navigationcommandcoordinator.h"
#include "panellayoutcontroller.h"
#include "pinloomcodelinkcoordinator.h"
#include "pinloomcontextprovider.h"
#include "../integrations/xips/xipscontextprovider.h"
#include "pinloomhostclient.h"
#include "settingscenterservice.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorsearchprovider.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "statetransitiongraphservice.h"
#include "semanticindex.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
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
                    if (tabManager) {
                        const QString file = tabManager->getCurrentDocumentMetadata().fileName;
                        controller->setActiveDocument(file.isEmpty() ? QString() : QDir(path).relativeFilePath(file));
                    }
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

    if (tabManager) {
        const auto relativeDocument = [controller = contextWorkspaceController.get()](const QString& file) {
            if (file.isEmpty() || controller->workspaceRoot().isEmpty()) return QString();
            return QDir(controller->workspaceRoot()).relativeFilePath(file);
        };
        connect(tabManager.get(), &TabManager::activeDocumentChanged, contextWorkspaceController.get(),
                [controller = contextWorkspaceController.get(), relativeDocument](const DocumentSnapshot& snapshot) {
                    controller->setActiveDocument(relativeDocument(snapshot.fileName));
                });
        connect(tabManager.get(), &TabManager::tabClosed, contextWorkspaceController.get(),
                [this, controller = contextWorkspaceController.get(), relativeDocument](const QString& file) {
                    for (auto* editor : tabManager->openEditors()) {
                        auto* document = tabManager->sharedDocumentForEditor(editor);
                        if (document && QDir::cleanPath(document->fileName()).compare(QDir::cleanPath(file), Qt::CaseInsensitive) == 0)
                            return;
                    }
                    controller->documentClosed(relativeDocument(file));
                });
        contextWorkspaceController->setActiveDocument(relativeDocument(tabManager->getCurrentDocumentMetadata().fileName));
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
             LiveInsightKind::Hotspot}) {
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
             LiveInsightKind::State}) {
        auto provider =
            std::make_unique<LiveInsightsContextProvider>(
                kind,
                liveInsightSession.get());
        provider->setFullViewHandler(
            [this](const ContextResource& resource) {
                openLiveInsightFullView(resource);
            });
        provider->setToolContextSource(
            [this]() {
                return activeLiveInsightToolContext();
            });
        provider->setTargetPickRequest(
            [this](LiveInsightKind kind,
                   std::function<void(
                       const LiveInsightsContextView::TargetCandidate&)>
                       picked) {
                return beginLiveInsightTargetPick(kind, std::move(picked));
            });
        provider->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                return revealSuiteSource(fileName, line, column);
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
    if (settingsCenterService) {
        pinloomHostClient->setExecutablePath(
            settingsCenterService->load(workspaceManager->getWorkspacePath())
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

    contextWorkspaceController->registerProvider(
        std::make_unique<XipsContextProvider>(tabManager.get(), workspaceManager.get()));

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

bool MainWindow::beginLiveInsightTargetPick(
    LiveInsightKind kind,
    std::function<void(const LiveInsightsContextView::TargetCandidate&)>
        picked)
{
    MyCodeEditor* editor =
        tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (!editor) {
        postActivityMessage(
            QStringLiteral(
                "Open the source file that holds the target before picking one."),
            5000);
        return false;
    }

    EditorInsightTargetClass targetClass = EditorInsightTargetClass::Signal;
    switch (kind) {
    case LiveInsightKind::Kernel:
    case LiveInsightKind::Hotspot:
    case LiveInsightKind::State:
        targetClass = EditorInsightTargetClass::Signal;
        break;
    case LiveInsightKind::Module:
        targetClass = EditorInsightTargetClass::Module;
        break;
    }

    const LiveInsightToolContext context = activeLiveInsightToolContext();
    const QString fileName = context.fileName;
    const QString moduleName = context.moduleName;

    EditorInsightTargetPickController::Validator validator;
    if (kind == LiveInsightKind::State) {
        // Only the graph service knows whether a register actually drives an
        // FSM, so that answer is fetched once, for the chosen candidate.
        validator = [fileName, moduleName](
                        const EditorInsightTargetCandidate& candidate,
                        QString* reason) {
            StateTransitionGraphQuery query;
            query.symbolName = candidate.name;
            query.fileName = fileName;
            query.moduleName = moduleName;
            const StateTransitionGraphReport report =
                StateTransitionGraphService::getInstance()
                    ->buildStateTransitionGraph(query);
            if (report.found)
                return true;
            if (reason) {
                const QString detail =
                    report.notFoundReasonDisplayName.trimmed();
                *reason = detail.isEmpty()
                    ? QStringLiteral(
                          "%1 has no state transition graph.").arg(candidate.name)
                    : QStringLiteral("%1: %2").arg(candidate.name, detail);
            }
            return false;
        };
    }
    // Kernel, Hotspot and Module have no second stage of their own: the
    // syntax and taxonomy filter is the whole test for them today.

    auto handler = [this, kind, moduleName, picked = std::move(picked)](
                       const EditorInsightTargetCandidate& candidate) {
        LiveInsightsContextView::TargetCandidate target;
        switch (kind) {
        case LiveInsightKind::Kernel:
        case LiveInsightKind::Hotspot:
        case LiveInsightKind::State:
            target.moduleName = moduleName;
            target.signalName = candidate.name;
            target.signalAccessPath = candidate.name;
            target.label = candidate.name;
            break;
        case LiveInsightKind::Module:
            target.moduleName = candidate.name;
            target.label = candidate.name;
            break;
        }
        if (picked)
            picked(target);
        requestLiveInsightUpdates();
    };

    QString message;
    if (!editor->startInsightTargetPickMode(
            targetClass, std::move(validator), std::move(handler), &message)) {
        postActivityMessage(
            message.trimmed().isEmpty()
                ? QStringLiteral("The editor cannot pick a target right now.")
                : message,
            5000);
        return false;
    }
    postActivityMessage(
        QStringLiteral(
            "Pick a target: Tab moves between blinking targets, Enter or click "
            "selects, Esc cancels."),
        5000);
    return true;
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
                 LiveInsightKind::Hotspot}) {
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
             LiveInsightKind::Hotspot}) {
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
    page->refreshTargetContext(activeLiveInsightToolContext());
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
    const ContextResource resource =
        LiveInsightsContextProvider::resourceForKind(
            kind,
            contextWorkspaceController->workspaceRoot(),
            state);
    QString failureReason;
    if (!contextWorkspaceController->openResource(
            resource,
            ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
            &failureReason)) {
        postActivityMessage(
            failureReason.trimmed().isEmpty()
                ? QStringLiteral(
                      "Live Insights sidebar is unavailable.")
                : failureReason,
            5000);
        return false;
    }
    contextWorkspaceController->setDockVisible(true);
    contextWorkspaceController->focusResource(resource.stableKey());

    auto* view = qobject_cast<LiveInsightsContextView*>(
        contextWorkspaceController->viewForResource(
            resource.stableKey()));
    if (!view) {
        postActivityMessage(
            QStringLiteral("The Live Insights section is unavailable."),
            5000);
        return false;
    }
    LiveInsightsContextView::TargetCandidate target;
    target.fileName = context.fileName;
    target.moduleName = context.moduleName;
    target.signalName = context.signalName;
    target.signalAccessPath = context.signalAccessPath;
    target.label = target.signalName.trimmed().isEmpty()
        ? target.moduleName
        : target.signalName;
    if (!view->applyTargetCandidate(target)) {
        postActivityMessage(
            QStringLiteral(
                "The selected symbol is not a target for this insight."),
            5000);
        return false;
    }

    requestLiveInsightUpdates();
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
    LiveInsightToolContext context = contextOverride
        ? *contextOverride
        : activeLiveInsightToolContext();
    const auto target = resource.state.value(QStringLiteral("target")).toMap();
    if (!contextOverride && !target.isEmpty()) {
        const auto file = target.value(QStringLiteral("fileName")).toString();
        if (!file.isEmpty() && context.fileName != file) {
            context.fileName = file;
            context.documentId = file;
            context.documentRevision = 0;
            context.documentText.clear();
            context.dirty = false;
        }
        context.moduleName = target.value(QStringLiteral("moduleName")).toString();
        context.signalName = target.value(QStringLiteral("signalName")).toString();
        context.signalAccessPath = target.value(QStringLiteral("signalAccessPath")).toString();
    }
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
    page->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            return revealSuiteSource(fileName, line, column);
        });
    page->setStatusHandler(
        [this](const QString& message, int timeoutMs) {
                postActivityMessage(message, timeoutMs);
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
                    page->refreshTargetContext(
                        owner->activeLiveInsightToolContext());
                }
            }
        });
    page->setContext(context);
    const QString title = kind == LiveInsightKind::Kernel
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
