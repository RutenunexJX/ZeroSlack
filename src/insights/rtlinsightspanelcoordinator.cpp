#include "rtlinsightspanelcoordinator.h"

#include "applicationthememanager.h"
#include "graphexportui.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "rtlinsightsgraphcontroller.h"
#include "rtlinsightsgraphconstants.h"
#include "rtlinsightspanelviewstate.h"
#include "rtlinsightspresenter.h"
#include "signalusagehotspotpanel.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

RtlInsightsPanelCoordinator::RtlInsightsPanelCoordinator(QWidget* parent)
{
    viewState =
        std::make_unique<RtlInsightsPanelViewState>();
    graphController =
        std::make_unique<RtlInsightsGraphController>(
            *viewState);
    presenter =
        std::make_unique<RtlInsightsPresenter>(
            *viewState,
            *graphController);
    viewState->moduleBlockDiagramRequestHandler =
        [this](const QString& fileName,
               const QString& moduleName) {
            presenter->showModuleBlockDiagramForModule(
                fileName,
                moduleName);
        };

    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("rtlInsightsPanel"));
    InsightVisualStyle::applyPanel(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* titleLabel = new QLabel(QStringLiteral("RTL Insights"), panel);
    titleLabel->setObjectName(QStringLiteral("rtlInsightsTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    viewState->pinButton = new QToolButton(panel);
    viewState->pinButton->setObjectName(
        QStringLiteral("rtlInsightsPinButton"));
    viewState->pinButton->setText(QStringLiteral("Pin"));
    viewState->pinButton->setCheckable(true);
    viewState->pinButton->setToolTip(
        QStringLiteral(
            "Keep the current insight while the editor selection changes"));
    viewState->pinButton->setAutoRaise(true);
    auto* titleLayout = new QHBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);
    titleLayout->addWidget(viewState->pinButton);
    layout->addLayout(titleLayout);

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    viewState->moduleBriefButton = new QPushButton(QStringLiteral("Module Brief"), panel);
    viewState->moduleBriefButton->setObjectName(QStringLiteral("rtlModuleBriefButton"));
    viewState->signalJourneyButton = new QPushButton(QStringLiteral("Signal Journey"), panel);
    viewState->signalJourneyButton->setObjectName(QStringLiteral("rtlSignalJourneyButton"));
    viewState->signalUsageHotspotButton =
        new QPushButton(QStringLiteral("Usage Hotspot"), panel);
    viewState->signalUsageHotspotButton->setObjectName(
        QStringLiteral("rtlSignalUsageHotspotButton"));
    viewState->clockResetButton = new QPushButton(QStringLiteral("Clock/Reset Map"), panel);
    viewState->clockResetButton->setObjectName(QStringLiteral("rtlClockResetButton"));
    viewState->fsmGraphButton = new QPushButton(QStringLiteral("FSM Graph"), panel);
    viewState->fsmGraphButton->setObjectName(QStringLiteral("rtlFsmGraphButton"));
    viewState->moduleBlockDiagramButton =
        new QPushButton(QStringLiteral("Module Block Diagram"), panel);
    viewState->moduleBlockDiagramButton->setObjectName(
        QStringLiteral("rtlModuleBlockDiagramButton"));
    viewState->graphZoomOutButton = new QPushButton(QStringLiteral("-"), panel);
    viewState->graphZoomOutButton->setObjectName(QStringLiteral("rtlGraphZoomOutButton"));
    viewState->graphZoomOutButton->setFixedWidth(30);
    viewState->graphFitButton = new QPushButton(QStringLiteral("Fit"), panel);
    viewState->graphFitButton->setObjectName(QStringLiteral("rtlGraphFitButton"));
    viewState->graphFitButton->setFixedWidth(42);
    viewState->graphZoomInButton = new QPushButton(QStringLiteral("+"), panel);
    viewState->graphZoomInButton->setObjectName(QStringLiteral("rtlGraphZoomInButton"));
    viewState->graphZoomInButton->setFixedWidth(30);
    viewState->graphSearchEdit = new QLineEdit(panel);
    viewState->graphSearchEdit->setObjectName(QStringLiteral("rtlGraphSearchEdit"));
    viewState->graphSearchEdit->setPlaceholderText(QStringLiteral("Search graph"));
    InsightVisualStyle::applySearchField(viewState->graphSearchEdit);
    viewState->moduleBlockTopCombo = new QComboBox(panel);
    viewState->moduleBlockTopCombo->setObjectName(QStringLiteral("rtlModuleBlockTopCombo"));
    viewState->moduleBlockTopCombo->setMinimumWidth(150);
    viewState->moduleBlockSetSelectionButton =
        new QPushButton(panel);
    viewState->moduleBlockSetSelectionButton->setObjectName(
        QStringLiteral("rtlModuleBlockSetSelectionButton"));
    viewState->moduleBlockDepthSpin = new QSpinBox(panel);
    viewState->moduleBlockDepthSpin->setObjectName(QStringLiteral("rtlModuleBlockDepthSpin"));
    viewState->moduleBlockDepthSpin->setRange(0, 8);
    viewState->moduleBlockDepthSpin->setValue(2);
    viewState->moduleBlockDepthSpin->setPrefix(QStringLiteral("Depth "));
    viewState->moduleBlockCollapsePackagesCheck =
        new QCheckBox(QStringLiteral("Collapse packages"), panel);
    viewState->moduleBlockCollapsePackagesCheck->setObjectName(
        QStringLiteral("rtlModuleBlockCollapsePackagesCheck"));
    viewState->moduleBlockShowUnresolvedCheck =
        new QCheckBox(QStringLiteral("Show unresolved"), panel);
    viewState->moduleBlockShowUnresolvedCheck->setObjectName(
        QStringLiteral("rtlModuleBlockShowUnresolvedCheck"));
    viewState->moduleBlockShowUnresolvedCheck->setChecked(true);

    viewState->stateTransitionSignalCombo = new QComboBox(panel);
    viewState->stateTransitionSignalCombo->setObjectName(
        QStringLiteral("rtlStateTransitionSignalCombo"));
    viewState->stateTransitionCurrentCombo = new QComboBox(panel);
    viewState->stateTransitionCurrentCombo->setObjectName(
        QStringLiteral("rtlStateTransitionCurrentCombo"));
    viewState->stateTransitionNextCombo = new QComboBox(panel);
    viewState->stateTransitionNextCombo->setObjectName(
        QStringLiteral("rtlStateTransitionNextCombo"));
    viewState->stateTransitionResetCheck =
        new QCheckBox(QStringLiteral("Show reset"), panel);
    viewState->stateTransitionResetCheck->setObjectName(
        QStringLiteral("rtlStateTransitionResetCheck"));
    viewState->stateTransitionResetCheck->setChecked(true);
    viewState->stateTransitionErrorCheck =
        new QCheckBox(QStringLiteral("Show error"), panel);
    viewState->stateTransitionErrorCheck->setObjectName(
        QStringLiteral("rtlStateTransitionErrorCheck"));
    viewState->stateTransitionErrorCheck->setChecked(true);
    viewState->stateTransitionUnreachableCheck =
        new QCheckBox(QStringLiteral("Show unreachable"), panel);
    viewState->stateTransitionUnreachableCheck->setObjectName(
        QStringLiteral("rtlStateTransitionUnreachableCheck"));
    viewState->stateTransitionUnreachableCheck->setChecked(true);

    viewState->graphLayoutCombo = new QComboBox(panel);
    viewState->graphLayoutCombo->setObjectName(QStringLiteral("rtlGraphLayoutCombo"));
    viewState->graphLayoutCombo->addItems({QStringLiteral("Nested blocks"),
                                QStringLiteral("State flow"),
                                QStringLiteral("Tree")});
    viewState->graphMoreButton = new QToolButton(panel);
    viewState->graphMoreButton->setObjectName(QStringLiteral("rtlGraphMoreButton"));
    viewState->graphMoreButton->setText(QStringLiteral("..."));
    viewState->graphMoreButton->setPopupMode(QToolButton::InstantPopup);
    viewState->graphMoreButton->setToolTip(QStringLiteral("More graph actions"));
    auto* graphMoreMenu = new QMenu(viewState->graphMoreButton);
    viewState->graphJumpAction = createGraphAction(
        viewState->graphMoreButton,
        QString::fromLatin1(
            ActionIds::GraphJumpSelected));
    viewState->graphFocusAction = createGraphAction(
        viewState->graphMoreButton,
        QString::fromLatin1(
            ActionIds::GraphFocusSelected));
    viewState->graphSetTopAction = createGraphAction(
        viewState->graphMoreButton,
        QString::fromLatin1(
            ActionIds::GraphSetTopSelected));
    viewState->graphTemporaryEditorAction =
        createSelectedSourceAction(
            viewState->graphMoreButton,
            QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen));
    for (QAction* action :
         {viewState->graphJumpAction,
          viewState->graphFocusAction,
          viewState->graphSetTopAction,
          viewState->graphTemporaryEditorAction}) {
        if (action)
            graphMoreMenu->addAction(action);
    }
    graphMoreMenu->addSeparator();
    const GraphExportUi::AvailabilityProvider
        rtlGraphExportAvailable = [this]() {
            return hasExportableGraph();
        };
    viewState->graphExportAction =
        GraphExportUi::bindRegistryAction(
            viewState->graphMoreButton,
            QString::fromLatin1(
                ActionIds::GraphExportRtlInsights),
            rtlGraphExportAvailable,
            [this](const QString& outputPath,
                   const GraphExportOptions& options) {
                return exportCurrentGraph(outputPath, options);
            },
            [this](const QString& message, int timeoutMs) {
                if (viewState->statusMessageHandler) {
                    viewState->statusMessageHandler(
                        message,
                        timeoutMs);
                }
            });
    if (viewState->graphExportAction)
        graphMoreMenu->addAction(viewState->graphExportAction);
    QObject::connect(
        graphMoreMenu,
        &QMenu::aboutToShow,
        viewState->graphMoreButton,
        [this]() {
            refreshGraphActionAvailability();
            refreshGraphExportActionAvailability();
        });
    viewState->graphMoreButton->setMenu(graphMoreMenu);
    for (QPushButton* button :
         {viewState->moduleBriefButton,
          viewState->signalJourneyButton,
          viewState->signalUsageHotspotButton,
          viewState->clockResetButton,
          viewState->fsmGraphButton,
          viewState->moduleBlockDiagramButton,
          viewState->moduleBlockSetSelectionButton,
          viewState->graphZoomOutButton,
          viewState->graphFitButton,
          viewState->graphZoomInButton}) {
        InsightVisualStyle::applyToolbarButton(button);
    }
    for (QCheckBox* checkBox :
         {viewState->moduleBlockCollapsePackagesCheck,
          viewState->moduleBlockShowUnresolvedCheck,
          viewState->stateTransitionResetCheck,
          viewState->stateTransitionErrorCheck,
          viewState->stateTransitionUnreachableCheck}) {
        InsightVisualStyle::applySegmentedCheckBox(checkBox);
    }
    actionLayout->addWidget(viewState->moduleBriefButton);
    actionLayout->addWidget(viewState->signalJourneyButton);
    actionLayout->addWidget(viewState->signalUsageHotspotButton);
    actionLayout->addWidget(viewState->clockResetButton);
    actionLayout->addWidget(viewState->fsmGraphButton);
    actionLayout->addWidget(viewState->moduleBlockDiagramButton);
    actionLayout->addStretch(1);
    layout->addLayout(actionLayout);

    viewState->insightsTree = new QTreeWidget(panel);
    viewState->insightsTree->setObjectName(QStringLiteral("rtlInsightsTree"));
    viewState->insightsTree->setColumnCount(5);
    viewState->insightsTree->setHeaderLabels({"Section", "Symbol", "Detail", "File", "Line"});
    viewState->insightsTree->setRootIsDecorated(true);
    viewState->insightsTree->setAlternatingRowColors(true);
    viewState->insightsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    viewState->insightsTree->header()->setStretchLastSection(true);
    viewState->insightsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    viewState->insightsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    viewState->insightsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    viewState->insightsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    viewState->insightsGraphScene = new QGraphicsScene(panel);
    QObject::connect(
        viewState->insightsGraphScene,
        &QGraphicsScene::changed,
        viewState->graphMoreButton,
        [this]() {
            refreshGraphExportActionAvailability();
        });
    viewState->insightsGraphView = new InsightGraphView(viewState->insightsGraphScene, panel);
    viewState->insightsGraphView->setObjectName(QStringLiteral("rtlInsightsGraphView"));
    viewState->insightsGraphView->applyInsightGraphStyle();
    viewState->insightsGraphView->setZoomRange(kGraphMinScale, kGraphMaxScale);
    viewState->insightsGraphView->setGridVisible(true);
    viewState->insightsGraphView->setClearSelectionOnEmptyLeftClick(true);
    viewState->insightsGraphView->setPressHandler(
        [this](const QPoint& viewPoint,
               Qt::MouseButton button,
               Qt::KeyboardModifiers) {
            if (button != Qt::LeftButton
                || !viewState->insightsGraphView
                || (viewState->currentGraphMode != QStringLiteral("state-transition")
                    && viewState->currentGraphMode != QStringLiteral("fsm"))) {
                return false;
            }
            return graphController->selectItemAtScenePoint(
                viewState->insightsGraphView->mapToScene(viewPoint));
        });

    viewState->insightsGraphPanel = new QWidget(panel);
    viewState->insightsGraphPanel->setObjectName(QStringLiteral("rtlInsightsGraphPanel"));
    auto* graphPanelLayout = new QVBoxLayout(viewState->insightsGraphPanel);
    graphPanelLayout->setContentsMargins(0, 0, 0, 0);
    graphPanelLayout->setSpacing(6);

    auto* graphToolbarLayout = new QHBoxLayout;
    graphToolbarLayout->setContentsMargins(0, 0, 0, 0);
    graphToolbarLayout->setSpacing(6);
    auto* moduleBlockTopLabel = new QLabel(QStringLiteral("Top:"), panel);
    moduleBlockTopLabel->setObjectName(QStringLiteral("rtlModuleBlockTopLabel"));
    auto* stateSignalLabel = new QLabel(QStringLiteral("Signal:"), panel);
    stateSignalLabel->setObjectName(QStringLiteral("rtlStateSignalLabel"));
    auto* stateCurrentLabel = new QLabel(QStringLiteral("Current:"), panel);
    stateCurrentLabel->setObjectName(QStringLiteral("rtlStateCurrentLabel"));
    auto* stateNextLabel = new QLabel(QStringLiteral("Next:"), panel);
    stateNextLabel->setObjectName(QStringLiteral("rtlStateNextLabel"));
    graphToolbarLayout->addWidget(moduleBlockTopLabel);
    graphToolbarLayout->addWidget(viewState->moduleBlockTopCombo);
    graphToolbarLayout->addWidget(stateSignalLabel);
    graphToolbarLayout->addWidget(viewState->stateTransitionSignalCombo);
    graphToolbarLayout->addWidget(stateCurrentLabel);
    graphToolbarLayout->addWidget(viewState->stateTransitionCurrentCombo);
    graphToolbarLayout->addWidget(stateNextLabel);
    graphToolbarLayout->addWidget(viewState->stateTransitionNextCombo);
    graphToolbarLayout->addWidget(viewState->graphSearchEdit, 1);
    graphToolbarLayout->addWidget(viewState->moduleBlockSetSelectionButton);
    graphToolbarLayout->addWidget(viewState->graphFitButton);
    graphToolbarLayout->addWidget(viewState->moduleBlockDepthSpin);
    graphToolbarLayout->addWidget(viewState->moduleBlockCollapsePackagesCheck);
    graphToolbarLayout->addWidget(viewState->moduleBlockShowUnresolvedCheck);
    graphToolbarLayout->addWidget(viewState->stateTransitionResetCheck);
    graphToolbarLayout->addWidget(viewState->stateTransitionErrorCheck);
    graphToolbarLayout->addWidget(viewState->stateTransitionUnreachableCheck);
    graphToolbarLayout->addWidget(viewState->graphLayoutCombo);
    graphToolbarLayout->addWidget(viewState->graphZoomOutButton);
    graphToolbarLayout->addWidget(viewState->graphZoomInButton);
    graphToolbarLayout->addWidget(viewState->graphMoreButton);
    graphPanelLayout->addLayout(graphToolbarLayout);

    viewState->graphInspector = new QTreeWidget(panel);
    viewState->graphInspector->setObjectName(QStringLiteral("rtlGraphInspector"));
    viewState->graphInspector->setColumnCount(2);
    viewState->graphInspector->setHeaderLabels({QStringLiteral("Field"),
                                     QStringLiteral("Value")});
    viewState->graphInspector->setRootIsDecorated(false);
    viewState->graphInspector->setAlternatingRowColors(true);
    viewState->graphInspector->setUniformRowHeights(true);
    viewState->graphInspector->header()->setSectionResizeMode(0,
                                                   QHeaderView::ResizeToContents);
    viewState->graphInspector->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    viewState->graphInspector->setMinimumWidth(260);
    viewState->graphInspectorJumpButton =
        new QPushButton(panel);
    viewState->graphInspectorJumpButton->setObjectName(
        QStringLiteral("rtlGraphInspectorJumpButton"));
    viewState->graphInspectorFocusButton =
        new QPushButton(panel);
    viewState->graphInspectorFocusButton->setObjectName(
        QStringLiteral("rtlGraphInspectorFocusButton"));
    viewState->graphInspectorSetTopButton =
        new QPushButton(panel);
    viewState->graphInspectorSetTopButton->setObjectName(
        QStringLiteral("rtlGraphInspectorSetTopButton"));
    viewState->graphInspectorRevealButton =
        new QPushButton(QStringLiteral("Reveal"), panel);
    viewState->graphInspectorRevealButton->setObjectName(
        QStringLiteral("rtlGraphInspectorRevealButton"));
    bindGraphActionButton(
        viewState->graphInspectorJumpButton,
        viewState->graphJumpAction);
    bindGraphActionButton(
        viewState->graphInspectorFocusButton,
        viewState->graphFocusAction);
    bindGraphActionButton(
        viewState->graphInspectorSetTopButton,
        viewState->graphSetTopAction);
    bindGraphActionButton(
        viewState->moduleBlockSetSelectionButton,
        viewState->graphSetTopAction);
    for (QPushButton* button :
         {viewState->graphInspectorJumpButton,
          viewState->graphInspectorFocusButton,
          viewState->graphInspectorSetTopButton,
          viewState->graphInspectorRevealButton}) {
        InsightVisualStyle::applyToolbarButton(button);
    }
    viewState->graphInspectorRevealButton->setEnabled(false);
    viewState->graphInspectorRevealButton->setToolTip(
        QStringLiteral("Hierarchy reveal is not wired for this panel yet."));

    auto* inspectorPanel = new QWidget(panel);
    inspectorPanel->setObjectName(QStringLiteral("rtlGraphInspectorPanel"));
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(5);
    inspectorLayout->addWidget(viewState->graphInspector, 1);
    auto* inspectorActionLayout = new QHBoxLayout;
    inspectorActionLayout->setContentsMargins(0, 0, 0, 0);
    inspectorActionLayout->setSpacing(4);
    inspectorActionLayout->addWidget(viewState->graphInspectorJumpButton);
    inspectorActionLayout->addWidget(viewState->graphInspectorFocusButton);
    inspectorActionLayout->addWidget(viewState->graphInspectorSetTopButton);
    inspectorActionLayout->addWidget(viewState->graphInspectorRevealButton);
    inspectorLayout->addLayout(inspectorActionLayout);

    auto* graphBodySplitter = new QSplitter(Qt::Horizontal, panel);
    graphBodySplitter->setObjectName(QStringLiteral("rtlGraphBodySplitter"));
    graphBodySplitter->addWidget(viewState->insightsGraphView);
    graphBodySplitter->addWidget(inspectorPanel);
    graphBodySplitter->setStretchFactor(0, 1);
    graphBodySplitter->setStretchFactor(1, 0);
    graphPanelLayout->addWidget(graphBodySplitter, 1);

    viewState->graphTable = new QTableWidget(panel);
    viewState->graphTable->setObjectName(QStringLiteral("rtlGraphDetailTable"));
    viewState->graphTable->setAlternatingRowColors(true);
    viewState->graphTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    viewState->graphTable->setSelectionMode(QAbstractItemView::SingleSelection);
    viewState->graphTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    viewState->graphTable->verticalHeader()->setVisible(false);
    viewState->graphTable->setMinimumHeight(118);
    viewState->graphTable->setMaximumHeight(190);
    graphPanelLayout->addWidget(viewState->graphTable, 0);

    viewState->signalUsageHotspotPanel = new SignalUsageHotspotPanel(panel);

    viewState->insightsStack = new QStackedWidget(panel);
    viewState->insightsStack->addWidget(viewState->insightsTree);
    viewState->insightsStack->addWidget(viewState->insightsGraphPanel);
    viewState->insightsStack->addWidget(viewState->signalUsageHotspotPanel);
    layout->addWidget(viewState->insightsStack, 1);

    viewState->insightsDock = new QDockWidget(QStringLiteral("RTL Insights"), parent);
    viewState->insightsDock->setObjectName(QStringLiteral("rtlInsightsDock"));
    viewState->insightsDock->setWidget(panel);
    viewState->insightsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);
    themeAboutToChangeConnection = QObject::connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeAboutToChange,
        viewState->insightsGraphView,
        [this](ThemeMode, ThemeMode) {
            graphController->captureThemeViewportState();
        });

    QObject::connect(viewState->insightsTree, &QTreeWidget::itemDoubleClicked,
                     viewState->insightsDock, [this](QTreeWidgetItem* item, int) {
                         if (!item || !viewState->sourceNavigationHandler)
                             return;
                         RtlInsightSourceLocation location;
                         location.fileName =
                             item->data(0, Qt::UserRole).toString();
                         if (location.fileName.isEmpty())
                             return;
                         location.line =
                             item->data(0, Qt::UserRole + 1).toInt();
                         location.column =
                             item->data(0, Qt::UserRole + 2).toInt();
                         location.moduleName =
                             viewState->currentModuleName;
                         location.workspacePath =
                             viewState->currentSourceLocation.workspacePath;
                         location.activeTopModule =
                             viewState->currentSourceLocation.activeTopModule;
                         location.instancePath =
                             viewState->currentSourceLocation.instancePath;
                         location.documentRevision =
                             viewState->currentSourceLocation.documentRevision;
                         if (!viewState->sourceNavigationHandler(location)
                             && viewState->statusMessageHandler) {
                             viewState->statusMessageHandler(
                                 QStringLiteral("RTL Insights jump failed"),
                                 4000);
                         }
                     });
    QObject::connect(viewState->moduleBriefButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showModuleBrief(); });
    QObject::connect(viewState->pinButton,
                     &QToolButton::toggled,
                     viewState->insightsDock,
                     [this](bool pinned) {
                         presenter->setPinned(pinned);
                     });
    QObject::connect(viewState->signalJourneyButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showSignalJourney(); });
    QObject::connect(viewState->signalUsageHotspotButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showSignalUsageHotspot(); });
    QObject::connect(viewState->clockResetButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showClockResetDomainMap(); });
    QObject::connect(viewState->fsmGraphButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showFsmGraph(); });
    QObject::connect(viewState->moduleBlockDiagramButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() { presenter->showModuleBlockDiagram(); });
    QObject::connect(viewState->graphZoomOutButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() {
                        if (viewState->insightsGraphView)
                             viewState->insightsGraphView->zoomOut();
                     });
    QObject::connect(viewState->graphZoomInButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() {
                        if (viewState->insightsGraphView)
                             viewState->insightsGraphView->zoomIn();
                     });
    QObject::connect(viewState->graphFitButton, &QPushButton::clicked,
                     viewState->insightsDock, [this]() {
                         if (viewState->insightsGraphView)
                             viewState->insightsGraphView->fitScene(Qt::KeepAspectRatio);
                     });
    QObject::connect(viewState->graphSearchEdit,
                     &QLineEdit::textChanged,
                     viewState->insightsDock,
                     [this](const QString& text) {
                         viewState->graphSearchText = text.trimmed();
                         graphController->applySearchHighlight();
                     });
    QObject::connect(viewState->moduleBlockTopCombo,
                     qOverload<int>(&QComboBox::activated),
                     viewState->insightsDock,
                     [this](int index) {
                         if (!viewState->moduleBlockTopCombo
                             || viewState->currentGraphMode != QStringLiteral("module-block")) {
                             return;
                         }
                         const int nodeId =
                             viewState->moduleBlockTopCombo->itemData(index).toInt();
                         for (const ModuleBlockDiagramNode& node :
                              viewState->currentModuleBlockReport.nodes) {
                             if (node.nodeId != nodeId
                                 || node.unresolved
                                 || node.definitionCodeLink.fileName.isEmpty()) {
                                 continue;
                             }
                             showModuleBlockDiagramForModule(
                                 node.definitionCodeLink.fileName,
                                 node.moduleDisplayName);
                             return;
                         }
                     });
    QObject::connect(viewState->moduleBlockDepthSpin,
                     qOverload<int>(&QSpinBox::valueChanged),
                     viewState->insightsDock,
                     [this](int) {
                         if (viewState->currentGraphMode == QStringLiteral("module-block"))
                             presenter->showModuleBlockDiagram();
                     });
    QObject::connect(viewState->moduleBlockShowUnresolvedCheck,
                     &QCheckBox::toggled,
                     viewState->insightsDock,
                     [this](bool) {
                         if (viewState->currentGraphMode == QStringLiteral("module-block"))
                             graphController->mapModuleBlockDiagram(
                                 viewState->currentModuleBlockReport);
                     });
    QObject::connect(viewState->graphLayoutCombo,
                     qOverload<int>(&QComboBox::currentIndexChanged),
                     viewState->insightsDock,
                     [this](int) {
                         if (viewState->currentGraphMode == QStringLiteral("module-block"))
                             graphController->mapModuleBlockDiagram(
                                 viewState->currentModuleBlockReport);
                     });
    QObject::connect(viewState->graphTable,
                     &QTableWidget::cellClicked,
                     viewState->insightsDock,
                     [this](int row, int) {
                         if (!viewState->graphTable || row < 0)
                             return;
                         const QTableWidgetItem* item =
                             viewState->graphTable->item(row, 0);
                         if (!item)
                             return;
                         if (viewState->currentGraphMode == QStringLiteral("module-block"))
                             graphController->selectModuleBlockNode(
                                 item->data(kGraphNodeIdRole).toInt());
                     });
    QObject::connect(viewState->graphTable,
                     &QTableWidget::cellDoubleClicked,
                     viewState->insightsDock,
                     [this](int row, int) {
                         if (!viewState->graphTable || row < 0)
                             return;
                         viewState->graphTable->selectRow(row);
                         if (viewState->currentGraphMode == QStringLiteral("module-block")) {
                             const QTableWidgetItem* item =
                                 viewState->graphTable->item(row, 0);
                             if (item) {
                                 graphController->selectModuleBlockNode(
                                     item->data(kGraphNodeIdRole).toInt(),
                                     false,
                                     false);
                             }
                         }
                         graphController->navigateSelectedItem();
                     });
    presenter->renderNoContext();
}


RtlInsightsPanelCoordinator::~RtlInsightsPanelCoordinator()
{
    QObject::disconnect(themeAboutToChangeConnection);
    // The scene is parented to the MainWindow-owned panel and can outlive this
    // non-QObject coordinator during MainWindow member teardown.  Disconnect
    // deferred scene notifications before viewState and lambdas capturing this
    // coordinator are destroyed.
    if (viewState && viewState->insightsGraphScene) {
        QObject::disconnect(
            viewState->insightsGraphScene,
            nullptr,
            nullptr,
            nullptr);
    }
}

void RtlInsightsPanelCoordinator::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    setSourceNavigationHandler(
        [handler = std::move(handler)](
            const RtlInsightSourceLocation& location) {
            return handler
                && handler(
                    location.fileName,
                    location.line,
                    location.column);
        });
}

void RtlInsightsPanelCoordinator::setSourceNavigationHandler(
    std::function<bool(
        const RtlInsightSourceLocation&)> handler)
{
    viewState->sourceNavigationHandler =
        std::move(handler);
    if (viewState->signalUsageHotspotPanel) {
        viewState->signalUsageHotspotPanel->setNavigationHandler(
            [this](const QString& file, int line, int column) {
                RtlInsightSourceLocation location;
                location.fileName = file;
                location.line = line;
                location.column = column;
                location.moduleName = viewState->currentModuleName;
                location.symbolName = viewState->currentSignalName;
                return viewState->sourceNavigationHandler && viewState->sourceNavigationHandler(location);
            });
    }
}

void RtlInsightsPanelCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    viewState->statusMessageHandler = std::move(handler);
    if (viewState->signalUsageHotspotPanel) {
        viewState->signalUsageHotspotPanel->setStatusMessageHandler(
            viewState->statusMessageHandler);
    }
}

void RtlInsightsPanelCoordinator::
    setRegisteredActionRequestHandler(
        RegisteredActionRequestHandler handler)
{
    registeredActionRequestHandler =
        std::move(handler);
}

void RtlInsightsPanelCoordinator::updateModuleContext(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    presenter->updateModuleContext(
        fileName,
        moduleName,
        signalName);
}

bool RtlInsightsPanelCoordinator::syncSourceLocation(
    const RtlInsightSourceLocation& location)
{
    return presenter->syncSourceLocation(location);
}

void RtlInsightsPanelCoordinator::setPinned(bool pinned)
{
    presenter->setPinned(pinned);
}

bool RtlInsightsPanelCoordinator::isPinned() const
{
    return presenter->isPinned();
}

void RtlInsightsPanelCoordinator::setStateViewEnabled(bool enabled)
{
    if (!viewState || viewState->stateViewEnabled == enabled)
        return;
    viewState->stateViewEnabled = enabled;
    for (QWidget* control : {
             static_cast<QWidget*>(viewState->fsmGraphButton),
             static_cast<QWidget*>(viewState->stateTransitionSignalCombo),
             static_cast<QWidget*>(viewState->stateTransitionCurrentCombo),
             static_cast<QWidget*>(viewState->stateTransitionNextCombo),
             static_cast<QWidget*>(viewState->stateTransitionResetCheck),
             static_cast<QWidget*>(viewState->stateTransitionErrorCheck),
             static_cast<QWidget*>(viewState->stateTransitionUnreachableCheck)}) {
        if (control)
            control->setVisible(enabled);
    }
    if (viewState->insightsDock) {
        viewState->insightsDock->setProperty(
            "carriesStateInsight", enabled);
    }
    if (!enabled
        && (viewState->currentGraphMode == QStringLiteral("fsm")
            || viewState->currentGraphMode
                   == QStringLiteral("state-transition"))) {
        presenter->showModuleBrief();
    }
}

bool RtlInsightsPanelCoordinator::stateViewEnabledForTest() const
{
    return viewState && viewState->stateViewEnabled;
}

void RtlInsightsPanelCoordinator::showModuleInsights(
    const QString& fileName,
    const QString& moduleName,
    const QString& signalName)
{
    presenter->showModuleInsights(
        fileName,
        moduleName,
        signalName);
}

void RtlInsightsPanelCoordinator::
    showStateTransitionGraphForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName)
{
    if (!viewState->stateViewEnabled)
        return;
    presenter->showStateTransitionGraphForSignal(
        fileName,
        moduleName,
        signalName);
}

void RtlInsightsPanelCoordinator::
    showSignalUsageHotspotForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName,
        const QString& signalAccessPath)
{
    presenter->showSignalUsageHotspotForSignal(
        fileName,
        moduleName,
        signalName,
        signalAccessPath);
}

void RtlInsightsPanelCoordinator::
    showModuleBlockDiagramForModule(
        const QString& fileName,
        const QString& moduleName)
{
    presenter->showModuleBlockDiagramForModule(
        fileName,
        moduleName);
}

void RtlInsightsPanelCoordinator::showSemanticDiff(
    std::shared_ptr<const SemanticIndexSnapshot>
        beforeSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot>
        afterSnapshot,
    const QString& moduleName,
    const QString& beforeFileName,
    const QString& afterFileName)
{
    presenter->showSemanticDiff(
        std::move(beforeSnapshot),
        std::move(afterSnapshot),
        moduleName,
        beforeFileName,
        afterFileName);
}

void RtlInsightsPanelCoordinator::refresh()
{
    presenter->refresh();
}

void RtlInsightsPanelCoordinator::refreshThemePresentation()
{
    graphController->refreshThemePresentation();
    refreshGraphActionAvailability();
    refreshGraphExportActionAvailability();
}

void RtlInsightsPanelCoordinator::showModuleBrief()
{
    presenter->showModuleBrief();
}

void RtlInsightsPanelCoordinator::showSignalJourney()
{
    presenter->showSignalJourney();
}

void RtlInsightsPanelCoordinator::showSignalUsageHotspot()
{
    presenter->showSignalUsageHotspot();
}

void RtlInsightsPanelCoordinator::showClockResetDomainMap()
{
    presenter->showClockResetDomainMap();
}

void RtlInsightsPanelCoordinator::showFsmGraph()
{
    if (!viewState->stateViewEnabled)
        return;
    presenter->showFsmGraph();
}

void RtlInsightsPanelCoordinator::showModuleBlockDiagram()
{
    presenter->showModuleBlockDiagram();
}

void RtlInsightsPanelCoordinator::focusFit()
{
    graphController->focusFit();
}

void RtlInsightsPanelCoordinator::focusZoomIn()
{
    graphController->focusZoomIn();
}

void RtlInsightsPanelCoordinator::focusZoomOut()
{
    graphController->focusZoomOut();
}

void RtlInsightsPanelCoordinator::setFocusSearchText(
    const QString& text)
{
    graphController->setFocusSearchText(text);
}

QString RtlInsightsPanelCoordinator::focusSearchText() const
{
    return graphController->focusSearchText();
}

void RtlInsightsPanelCoordinator::focusInspector()
{
    graphController->focusInspector();
}
