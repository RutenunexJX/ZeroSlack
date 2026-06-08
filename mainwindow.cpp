#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "completionmanager.h"

#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "modemanager.h"
#include "symbolanalyzer.h"
#include "analysisscheduler.h"
#include "analysisprogresscoordinator.h"
#include "navigationmanager.h"
#include "navigationwidget.h"
#include "diagnosticservice.h"
#include "hierarchyservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "syminfo.h"
#include "version.h"
#include <QLabel>
#include <QStatusBar>

#include <QMessageBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setCentralWidget(ui->tabWidget);

    relationshipEngine = std::make_unique<SymbolRelationshipEngine>(this);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    modeManager = std::unique_ptr<ModeManager>(new ModeManager(ui->tabWidget, this));
    symbolAnalyzer = std::unique_ptr<SymbolAnalyzer>(new SymbolAnalyzer(this));
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));

    setupRelationshipEngine();
    setupNavigationPane();
    setupProblemsPane();
    setupReferencesPane();
    setupRelationshipsPane();
    setupManagerConnections();
    connectNavigationSignals();

    setWindowTitle(QStringLiteral("ZeroSlack  %1").arg(QLatin1String(APP_VERSION)));
    if (statusBar()) {
        QLabel* versionLabel = new QLabel(
            QStringLiteral("v%1").arg(QLatin1String(APP_VERSION)), this);
        versionLabel->setToolTip(
            QStringLiteral("ZeroSlack %1\nBuilt at %2")
                .arg(QLatin1String(APP_VERSION), QLatin1String(APP_BUILD_TIME)));
        versionLabel->setStyleSheet(QStringLiteral("color:#888; margin-right:6px;"));
        statusBar()->addPermanentWidget(versionLabel);
    }
}

MainWindow::~MainWindow()
{
    if (analysisScheduler) {
        analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
    if (relationshipEngine) {
        relationshipEngine->clearAllRelationships();
    }

    delete ui;
}

void MainWindow::setupManagerConnections()
{
    analysisScheduler->setDocumentModel(tabManager->getDocumentModel());
    analysisScheduler->setProjectModel(workspaceManager->getProjectModel());
    analysisScheduler->setSymbolAnalyzer(symbolAnalyzer.get());
    analysisScheduler->setOpenFileContentProvider([this](const QString& fileName) {
        return tabManager ? tabManager->getPlainTextFromOpenFile(fileName) : QString();
    });
    analysisScheduler->setWorkspaceOpenProvider([this]() {
        return workspaceManager && workspaceManager->isWorkspaceOpen();
    });
    analysisScheduler->setWorkspaceSymbolCancelProvider([this]() {
        return analysisProgressCoordinator
            && analysisProgressCoordinator->isSymbolAnalysisCancelled();
    });
    analysisScheduler->setRelationshipEngine(relationshipEngine.get());
    analysisScheduler->setRelationshipBuilder(relationshipBuilder.get());
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipDataInvalidated,
            this, []() {
                CompletionManager::getInstance()->invalidateRelationshipCaches();
            });
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                if (navigationManager)
                    navigationManager->refreshCurrentView();
            });
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipAnalysisFinished,
            this, &MainWindow::onSingleFileRelationshipFinished);
    connect(analysisScheduler.get(), &AnalysisScheduler::documentRefreshRequested,
            this, [this](const QString& fileName) {
                MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
                if (editor && editor->getFileName() == fileName) {
                    editor->refreshScopeAndCurrentLineHighlight();
                    QTimer::singleShot(0, editor, [editor]() {
                        editor->refreshScopeAndCurrentLineHighlight();
                    });
                }
            });
    connect(analysisScheduler.get(), &AnalysisScheduler::diagnosticsRefreshRequested,
            this, [this](const QString& fileName) {
                updateProblemsPanel(fileName);
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [](MyCodeEditor* editor) { Q_UNUSED(editor); });

    connect(tabManager.get(), &TabManager::tabClosed,
            this, [this](const QString& fileName) {
                symbolAnalyzer->analyzeOpenTabs(tabManager.get());

                if (relationshipEngine) {
                    relationshipEngine->invalidateFileRelationships(fileName);
                }
            });

    connect(workspaceManager.get(), &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                if (analysisScheduler)
                    analysisScheduler->handleExternalFileChanged(filePath, kFileChangeDebounceMs);
            });

    analysisProgressCoordinator->connectToScheduler(analysisScheduler.get());
    analysisProgressCoordinator->connectToSymbolAnalyzer(symbolAnalyzer.get());
    connect(analysisProgressCoordinator.get(),
            &AnalysisProgressCoordinator::statusMessageRequested,
            this,
            [this](const QString& message, int timeoutMs) {
                if (statusBar())
                    statusBar()->showMessage(message, timeoutMs);
            });
    connect(analysisProgressCoordinator.get(),
            &AnalysisProgressCoordinator::relationshipAnalysisErrorReported,
            this,
            &MainWindow::onRelationshipAnalysisError);

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisFinished,
            this, &MainWindow::onWorkspaceRelationshipAnalysisFinished);

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisCancelled,
            this, []() {});

    connect(modeManager.get(), &ModeManager::modeChanged,
            this,[]{});

    connect(modeManager.get(), &ModeManager::navigationToggleRequested,
                this, [this]() {
                    if (navigationDock) {
                        if (navigationDock->isVisible()) {
                            navigationDock->hide();
                        } else {
                            navigationDock->show();
                            navigationDock->raise();
                            navigationDock->activateWindow();
                        }
                    }
                });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int symbolCount) {
                Q_UNUSED(symbolCount)
                MyCodeEditor* editor = tabManager->getCurrentEditor();
                if (!editor || editor->getFileName() != fileName) return;
                editor->refreshScopeAndCurrentLineHighlight();
            });

    navigationManager->connectToTabManager(tabManager.get());
    navigationManager->connectToWorkspaceManager(workspaceManager.get());
    navigationManager->connectToSymbolAnalyzer(symbolAnalyzer.get());

}


void MainWindow::setupNavigationPane()
{
    navigationWidget = new NavigationWidget(this);

    navigationDock = new QDockWidget("Navigation", this);
    navigationDock->setWidget(navigationWidget);
    navigationDock->setFeatures(QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);

    navigationDock->setMinimumWidth(200);
    navigationDock->setMaximumWidth(400);
    navigationWidget->setMinimumWidth(180);

    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);

    navigationManager->setNavigationWidget(navigationWidget);
}

void MainWindow::setupProblemsPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    problemsScopeCombo = new QComboBox(panel);
    problemsScopeCombo->setObjectName(QStringLiteral("problemsScopeCombo"));
    problemsScopeCombo->addItem(QStringLiteral("Current File"), 0);
    problemsScopeCombo->addItem(QStringLiteral("Workspace Files"), 1);
    problemsScopeCombo->addItem(QStringLiteral("All Files"), 2);
    problemsScopeCombo->setToolTip(QStringLiteral("Problem scope"));
    filtersLayout->addWidget(problemsScopeCombo);

    problemsSeverityCombo = new QComboBox(panel);
    problemsSeverityCombo->setObjectName(QStringLiteral("problemsSeverityCombo"));
    problemsSeverityCombo->addItem(QStringLiteral("All Severities"), 0);
    problemsSeverityCombo->addItem(QStringLiteral("Errors"), 1);
    problemsSeverityCombo->addItem(QStringLiteral("Warnings"), 2);
    problemsSeverityCombo->addItem(QStringLiteral("Info"), 3);
    problemsSeverityCombo->setToolTip(QStringLiteral("Severity filter"));
    filtersLayout->addWidget(problemsSeverityCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    problemsTree = new QTreeWidget(this);
    problemsTree->setObjectName(QStringLiteral("problemsTree"));
    problemsTree->setColumnCount(5);
    problemsTree->setHeaderLabels({"Severity", "File", "Line", "Column", "Message"});
    problemsTree->setRootIsDecorated(true);
    problemsTree->setAlternatingRowColors(true);
    problemsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTree->header()->setStretchLastSection(true);
    problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(problemsTree);

    problemsDock = new QDockWidget("Problems", this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(panel);
    problemsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);

    connect(problemsScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateProblemsPanel(); });
    connect(problemsSeverityCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateProblemsPanel(); });

    connect(problemsTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                if (fileName.isEmpty())
                    return;
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

static QString diagnosticSeverityText(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

static QString relationshipTypeText(SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

static QString hierarchyDirectionText(HierarchyQuery::Direction direction)
{
    switch (direction) {
    case HierarchyQuery::Children:
        return QStringLiteral("Outgoing");
    case HierarchyQuery::Parents:
        return QStringLiteral("Incoming");
    case HierarchyQuery::Both:
        break;
    }
    return QStringLiteral("Related");
}

static QString normalizedUiFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

static QString countLabel(const QString& text, int count)
{
    return QStringLiteral("%1 (%2)").arg(text).arg(count);
}

static QString stripCountSuffix(const QString& text)
{
    if (!text.endsWith(QLatin1Char(')')))
        return text;
    const int open = text.lastIndexOf(QStringLiteral(" ("));
    if (open < 0)
        return text;
    for (int i = open + 2; i < text.size() - 1; ++i) {
        if (!text.at(i).isDigit())
            return text;
    }
    return text.left(open);
}

static QString expansionKeyForItem(QTreeWidgetItem* item)
{
    if (!item)
        return QString();

    QStringList pathParts;
    for (QTreeWidgetItem* current = item; current; current = current->parent()) {
        QStringList columns;
        for (int column = 0; column < current->columnCount(); ++column) {
            const QString text = stripCountSuffix(current->text(column));
            if (!text.isEmpty())
                columns.append(QStringLiteral("%1=%2").arg(column).arg(text));
        }

        const QString fileName = current->data(0, Qt::UserRole).toString();
        if (!fileName.isEmpty()) {
            const QString normalized = normalizedUiFileName(fileName);
            columns.append(QStringLiteral("file=%1")
                               .arg(normalized.isEmpty() ? fileName : normalized));
            columns.append(QStringLiteral("line=%1")
                               .arg(current->data(0, Qt::UserRole + 1).toInt()));
            columns.append(QStringLiteral("column=%1")
                               .arg(current->data(0, Qt::UserRole + 2).toInt()));
        }

        pathParts.prepend(columns.join(QLatin1Char('|')));
    }
    return pathParts.join(QLatin1Char('/'));
}

static bool treeHasExpandableItems(QTreeWidgetItem* item)
{
    if (!item)
        return false;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->childCount() > 0 || treeHasExpandableItems(child))
            return true;
    }
    return false;
}

static bool treeHasExpandableItems(QTreeWidget* tree)
{
    return tree && treeHasExpandableItems(tree->invisibleRootItem());
}

static void collectExpandedKeys(QTreeWidgetItem* item, QSet<QString>& keys)
{
    if (!item)
        return;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->isExpanded())
            keys.insert(expansionKeyForItem(child));
        collectExpandedKeys(child, keys);
    }
}

static QSet<QString> collectExpandedKeys(QTreeWidget* tree)
{
    QSet<QString> keys;
    if (tree)
        collectExpandedKeys(tree->invisibleRootItem(), keys);
    return keys;
}

static int restoreExpandedKeys(QTreeWidgetItem* item, const QSet<QString>& keys)
{
    if (!item)
        return 0;

    int restored = 0;
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        const bool expanded = keys.contains(expansionKeyForItem(child));
        child->setExpanded(expanded);
        if (expanded)
            restored++;
        restored += restoreExpandedKeys(child, keys);
    }
    return restored;
}

static void restoreTreeExpansion(QTreeWidget* tree,
                                 bool hadExpandableItems,
                                 const QSet<QString>& expandedKeys)
{
    if (!tree)
        return;

    const int restored = restoreExpandedKeys(tree->invisibleRootItem(), expandedKeys);
    if (!hadExpandableItems || (!expandedKeys.isEmpty() && restored == 0))
        tree->expandAll();
}

static QTreeWidgetItem* createDiagnosticItem(QTreeWidgetItem* parent,
                                             const SemanticDiagnostic& diagnostic);

void MainWindow::setupReferencesPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    referenceScopeCombo = new QComboBox(panel);
    referenceScopeCombo->setObjectName(QStringLiteral("referenceScopeCombo"));
    referenceScopeCombo->addItem(QStringLiteral("All Files"), 0);
    referenceScopeCombo->addItem(QStringLiteral("Workspace Files"), 1);
    referenceScopeCombo->addItem(QStringLiteral("Current File"), 2);
    referenceScopeCombo->setToolTip(QStringLiteral("Reference scope"));
    filtersLayout->addWidget(referenceScopeCombo);

    referenceTypeCombo = new QComboBox(panel);
    referenceTypeCombo->setObjectName(QStringLiteral("referenceTypeCombo"));
    referenceTypeCombo->addItem(QStringLiteral("All Types"), -1);
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::REFERENCES),
                                static_cast<int>(SymbolRelationshipEngine::REFERENCES));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::INSTANTIATES),
                                static_cast<int>(SymbolRelationshipEngine::INSTANTIATES));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CALLS),
                                static_cast<int>(SymbolRelationshipEngine::CALLS));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::ASSIGNS_TO),
                                static_cast<int>(SymbolRelationshipEngine::ASSIGNS_TO));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::READS_FROM),
                                static_cast<int>(SymbolRelationshipEngine::READS_FROM));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CLOCKS),
                                static_cast<int>(SymbolRelationshipEngine::CLOCKS));
    referenceTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::RESETS),
                                static_cast<int>(SymbolRelationshipEngine::RESETS));
    referenceTypeCombo->setToolTip(QStringLiteral("Reference type"));
    filtersLayout->addWidget(referenceTypeCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    referencesTree = new QTreeWidget(panel);
    referencesTree->setObjectName(QStringLiteral("referencesTree"));
    referencesTree->setColumnCount(4);
    referencesTree->setHeaderLabels({"Symbol", "File", "Line", "Relationship"});
    referencesTree->setRootIsDecorated(true);
    referencesTree->setAlternatingRowColors(true);
    referencesTree->setSelectionMode(QAbstractItemView::SingleSelection);
    referencesTree->header()->setStretchLastSection(true);
    referencesTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(referencesTree);

    referencesDock = new QDockWidget("References", this);
    referencesDock->setObjectName(QStringLiteral("referencesDock"));
    referencesDock->setWidget(panel);
    referencesDock->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable |
                                QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, referencesDock);
    referencesDock->hide();

    connect(referenceScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshReferencesPanel(); });
    connect(referenceTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshReferencesPanel(); });

    connect(referencesTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                if (fileName.isEmpty())
                    return;
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

void MainWindow::setupRelationshipsPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    relationshipViewCombo = new QComboBox(panel);
    relationshipViewCombo->setObjectName(QStringLiteral("relationshipViewCombo"));
    relationshipViewCombo->addItem(QStringLiteral("Direct"), 0);
    relationshipViewCombo->addItem(QStringLiteral("Tree"), 1);
    relationshipViewCombo->setToolTip(QStringLiteral("Relationship view"));
    filtersLayout->addWidget(relationshipViewCombo);

    relationshipDirectionCombo = new QComboBox(panel);
    relationshipDirectionCombo->setObjectName(QStringLiteral("relationshipDirectionCombo"));
    relationshipDirectionCombo->addItem(QStringLiteral("All Directions"), 0);
    relationshipDirectionCombo->addItem(QStringLiteral("Outgoing"), 1);
    relationshipDirectionCombo->addItem(QStringLiteral("Incoming"), 2);
    relationshipDirectionCombo->setToolTip(QStringLiteral("Relationship direction"));
    filtersLayout->addWidget(relationshipDirectionCombo);

    relationshipTypeCombo = new QComboBox(panel);
    relationshipTypeCombo->setObjectName(QStringLiteral("relationshipTypeCombo"));
    relationshipTypeCombo->addItem(QStringLiteral("All Types"), -1);
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::REFERENCES),
                                   static_cast<int>(SymbolRelationshipEngine::REFERENCES));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::INSTANTIATES),
                                   static_cast<int>(SymbolRelationshipEngine::INSTANTIATES));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CALLS),
                                   static_cast<int>(SymbolRelationshipEngine::CALLS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::ASSIGNS_TO),
                                   static_cast<int>(SymbolRelationshipEngine::ASSIGNS_TO));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::READS_FROM),
                                   static_cast<int>(SymbolRelationshipEngine::READS_FROM));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CLOCKS),
                                   static_cast<int>(SymbolRelationshipEngine::CLOCKS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::RESETS),
                                   static_cast<int>(SymbolRelationshipEngine::RESETS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CONTAINS),
                                   static_cast<int>(SymbolRelationshipEngine::CONTAINS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::GENERATES),
                                   static_cast<int>(SymbolRelationshipEngine::GENERATES));
    relationshipTypeCombo->setToolTip(QStringLiteral("Relationship type"));
    filtersLayout->addWidget(relationshipTypeCombo);

    relationshipDepthCombo = new QComboBox(panel);
    relationshipDepthCombo->setObjectName(QStringLiteral("relationshipDepthCombo"));
    relationshipDepthCombo->addItem(QStringLiteral("Depth 1"), 1);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 2"), 2);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 3"), 3);
    relationshipDepthCombo->addItem(QStringLiteral("Depth 4"), 4);
    relationshipDepthCombo->setToolTip(QStringLiteral("Tree depth"));
    relationshipDepthCombo->setEnabled(false);
    filtersLayout->addWidget(relationshipDepthCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    relationshipsTree = new QTreeWidget(panel);
    relationshipsTree->setObjectName(QStringLiteral("relationshipsTree"));
    relationshipsTree->setColumnCount(5);
    relationshipsTree->setHeaderLabels({"Direction", "Symbol", "File", "Line", "Relationship"});
    relationshipsTree->setRootIsDecorated(true);
    relationshipsTree->setAlternatingRowColors(true);
    relationshipsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    relationshipsTree->header()->setStretchLastSection(true);
    relationshipsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(relationshipsTree);

    relationshipsDock = new QDockWidget("Relationships", this);
    relationshipsDock->setObjectName(QStringLiteral("relationshipsDock"));
    relationshipsDock->setWidget(panel);
    relationshipsDock->setFeatures(QDockWidget::DockWidgetMovable |
                                   QDockWidget::DockWidgetFloatable |
                                   QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, relationshipsDock);
    relationshipsDock->hide();

    connect(relationshipDirectionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshRelationshipsPanel(); });
    connect(relationshipTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshRelationshipsPanel(); });
    connect(relationshipDepthCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshRelationshipsPanel(); });
    connect(relationshipViewCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const bool treeMode = relationshipViewCombo
                    && relationshipViewCombo->currentData().toInt() == 1;
                if (relationshipDepthCombo)
                    relationshipDepthCombo->setEnabled(treeMode);
                refreshRelationshipsPanel();
            });

    connect(relationshipsTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                if (fileName.isEmpty())
                    return;
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

void MainWindow::updateProblemsPanel(const QString& fileName)
{
    if (!problemsTree)
        return;

    DiagnosticQuery query;
    const bool currentFileOnly =
        !problemsScopeCombo || problemsScopeCombo->currentData().toInt() == 0;
    const bool workspaceFilesOnly =
        problemsScopeCombo && problemsScopeCombo->currentData().toInt() == 1;
    if (currentFileOnly) {
        query.fileName = fileName;
        if (query.fileName.isEmpty()) {
            MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
            if (editor)
                query.fileName = editor->getFileName();
        }
    }
    query.workspaceFilesOnly = workspaceFilesOnly;
    if (workspaceFilesOnly && workspaceManager)
        query.workspaceFiles = workspaceManager->getSystemVerilogFiles();

    const int severityFilter =
        problemsSeverityCombo ? problemsSeverityCombo->currentData().toInt() : 0;
    query.includeErrors = severityFilter == 0 || severityFilter == 1;
    query.includeWarnings = severityFilter == 0 || severityFilter == 2;
    query.includeInfo = severityFilter == 0 || severityFilter == 3;

    const DiagnosticReport report =
        DiagnosticService::getInstance()->findDiagnosticReport(query);
    const QList<DiagnosticResult>& diagnostics = report.diagnostics;

    const bool hadExpandableItems = treeHasExpandableItems(problemsTree);
    const QSet<QString> expandedKeys = collectExpandedKeys(problemsTree);
    problemsTree->clear();
    if (currentFileOnly) {
        for (const DiagnosticResult& result : diagnostics) {
            const SemanticDiagnostic& diagnostic = result.diagnostic;
            createDiagnosticItem(problemsTree->invisibleRootItem(), diagnostic);
        }
    } else {
        for (const DiagnosticFileGroup& group : report.fileGroups) {
            auto* fileGroup = new QTreeWidgetItem(problemsTree);
            fileGroup->setText(0, countLabel(group.displayName, group.count));
            fileGroup->setText(1, group.fileName);
            fileGroup->setToolTip(0, group.fileName);
            fileGroup->setToolTip(1, group.fileName);
            for (const DiagnosticResult& result : group.diagnostics)
                createDiagnosticItem(fileGroup, result.diagnostic);
        }
        restoreTreeExpansion(problemsTree, hadExpandableItems, expandedKeys);
    }
    if (diagnostics.isEmpty()) {
        auto* emptyItem = new QTreeWidgetItem(problemsTree);
        emptyItem->setText(4, QStringLiteral("No problems"));
    }

    if (problemsDock) {
        problemsDock->setWindowTitle(QStringLiteral("Problems (%1)").arg(report.totalCount));
        if (!diagnostics.isEmpty() || problemsDock->isVisible())
            problemsDock->show();
    }
}

static QTreeWidgetItem* getOrCreateChildGroup(QTreeWidgetItem* parent,
                                              QMap<QString, QTreeWidgetItem*>& groups,
                                              const QString& key,
                                              int column,
                                              const QString& text)
{
    if (groups.contains(key))
        return groups.value(key);

    auto* group = new QTreeWidgetItem(parent);
    group->setText(column, text);
    groups.insert(key, group);
    return group;
}

static QTreeWidgetItem* createDiagnosticItem(QTreeWidgetItem* parent,
                                             const SemanticDiagnostic& diagnostic)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, diagnosticSeverityText(diagnostic.severity));
    item->setText(1, QFileInfo(diagnostic.fileName).fileName());
    item->setText(2, QString::number(diagnostic.line));
    item->setText(3, QString::number(diagnostic.column));
    item->setText(4, diagnostic.message);
    item->setToolTip(1, diagnostic.fileName);
    item->setToolTip(4, diagnostic.message);
    item->setData(0, Qt::UserRole, diagnostic.fileName);
    item->setData(0, Qt::UserRole + 1, diagnostic.line);
    item->setData(0, Qt::UserRole + 2, diagnostic.column);
    return item;
}

static QTreeWidgetItem* createReferenceItem(QTreeWidgetItem* parent,
                                            const ReferenceResult& reference)
{
    const sym_list::SymbolInfo& source = reference.referencingSymbol;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, source.symbolName);
    item->setText(1, QFileInfo(source.fileName).fileName());
    item->setText(2, QString::number(source.startLine));
    item->setText(3, relationshipTypeText(reference.relationship.relationship.type));
    item->setToolTip(1, source.fileName);
    item->setData(0, Qt::UserRole, source.fileName);
    item->setData(0, Qt::UserRole + 1, source.startLine);
    item->setData(0, Qt::UserRole + 2, source.startColumn);
    return item;
}

static QTreeWidgetItem* createRelationshipItem(QTreeWidgetItem* parent,
                                               const QString& direction,
                                               const sym_list::SymbolInfo& symbol,
                                               SymbolRelationshipEngine::RelationType type)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, direction);
    item->setText(1, symbol.symbolName);
    item->setText(2, QFileInfo(symbol.fileName).fileName());
    item->setText(3, QString::number(symbol.startLine));
    item->setText(4, relationshipTypeText(type));
    item->setToolTip(2, symbol.fileName);
    item->setData(0, Qt::UserRole, symbol.fileName);
    item->setData(0, Qt::UserRole + 1, symbol.startLine);
    item->setData(0, Qt::UserRole + 2, symbol.startColumn);
    return item;
}

static QTreeWidgetItem* createHierarchyItem(QTreeWidgetItem* parent,
                                            const HierarchyNode& node,
                                            const QString& roleText)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, roleText);
    item->setText(1, node.symbol.symbolName);
    item->setText(2, QFileInfo(node.symbol.fileName).fileName());
    item->setText(3, QString::number(node.symbol.startLine));
    item->setText(4, node.depth == 0
                         ? QStringLiteral("Root")
                         : relationshipTypeText(node.viaType));
    item->setToolTip(2, node.symbol.fileName);
    item->setData(0, Qt::UserRole, node.symbol.fileName);
    item->setData(0, Qt::UserRole + 1, node.symbol.startLine);
    item->setData(0, Qt::UserRole + 2, node.symbol.startColumn);
    return item;
}

void MainWindow::showReferencesForSymbol(const QString& symbolName,
                                         const QString& fileName,
                                         const QString& moduleName)
{
    if (!referencesTree || symbolName.isEmpty())
        return;

    currentReferenceSymbolName = symbolName;
    currentReferenceFileName = fileName;
    currentReferenceModuleName = moduleName;
    refreshReferencesPanel();
}

void MainWindow::refreshReferencesPanel()
{
    if (!referencesTree || currentReferenceSymbolName.isEmpty())
        return;

    ReferenceQuery query;
    query.symbolName = currentReferenceSymbolName;
    query.fileName = currentReferenceFileName;
    query.moduleName = currentReferenceModuleName;
    const int typeFilter = referenceTypeCombo
        ? referenceTypeCombo->currentData().toInt()
        : -1;
    if (typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(typeFilter)
        };
    }

    const int scopeFilter = referenceScopeCombo
        ? referenceScopeCombo->currentData().toInt()
        : 0;
    query.workspaceFilesOnly = scopeFilter == 1;
    query.currentFileOnly = scopeFilter == 2;
    if (query.workspaceFilesOnly && workspaceManager)
        query.workspaceFiles = workspaceManager->getSystemVerilogFiles();

    const ReferenceReport report =
        ReferenceService::getInstance()->findReferenceReport(query);
    const QString subjectName = report.subjectSymbol.symbolName.isEmpty()
        ? currentReferenceSymbolName
        : report.subjectSymbol.symbolName;

    const bool hadExpandableItems = treeHasExpandableItems(referencesTree);
    const QSet<QString> expandedKeys = collectExpandedKeys(referencesTree);
    referencesTree->clear();
    for (const ReferenceFileGroup& fileGroupReport : report.fileGroups) {
        auto* fileGroup = new QTreeWidgetItem(referencesTree);
        fileGroup->setText(0, countLabel(fileGroupReport.displayName,
                                         fileGroupReport.count));
        fileGroup->setText(1, fileGroupReport.fileName);
        fileGroup->setToolTip(0, fileGroupReport.fileName);
        fileGroup->setToolTip(1, fileGroupReport.fileName);

        for (const ReferenceTypeGroup& typeGroupReport : fileGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(fileGroup);
            const QString typeText = relationshipTypeText(typeGroupReport.type);
            typeGroup->setText(3, countLabel(typeText, typeGroupReport.count));
            for (const ReferenceResult& reference : typeGroupReport.references)
                createReferenceItem(typeGroup, reference);
        }
    }
    restoreTreeExpansion(referencesTree, hadExpandableItems, expandedKeys);

    if (referencesDock) {
        referencesDock->setWindowTitle(
            QStringLiteral("References: %1 (%2)")
                .arg(subjectName)
                .arg(report.totalCount));
        referencesDock->show();
        referencesDock->raise();
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Found %1 references for %2")
                .arg(report.totalCount)
                .arg(subjectName),
            3000);
    }
}

void MainWindow::showRelationshipsForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName)
{
    if (!relationshipsTree || symbolName.isEmpty())
        return;

    currentRelationshipSymbolName = symbolName;
    currentRelationshipFileName = fileName;
    currentRelationshipModuleName = moduleName;
    refreshRelationshipsPanel();
}

void MainWindow::refreshRelationshipsPanel()
{
    if (!relationshipsTree || currentRelationshipSymbolName.isEmpty())
        return;

    RelationshipQuery query;
    query.symbolName = currentRelationshipSymbolName;
    query.fileName = currentRelationshipFileName;
    query.moduleName = currentRelationshipModuleName;

    const int typeFilter = relationshipTypeCombo
        ? relationshipTypeCombo->currentData().toInt()
        : -1;
    if (typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(typeFilter)
        };
    }

    const bool treeMode = relationshipViewCombo
        && relationshipViewCombo->currentData().toInt() == 1;
    if (treeMode) {
        HierarchyQuery hierarchyQuery;
        hierarchyQuery.symbolName = currentRelationshipSymbolName;
        hierarchyQuery.fileName = currentRelationshipFileName;
        hierarchyQuery.moduleName = currentRelationshipModuleName;
        hierarchyQuery.maxDepth = relationshipDepthCombo
            ? relationshipDepthCombo->currentData().toInt()
            : 2;
        hierarchyQuery.types = query.types.isEmpty()
            ? HierarchyService::allRelationshipTypes()
            : query.types;
        const int directionFilter = relationshipDirectionCombo
            ? relationshipDirectionCombo->currentData().toInt()
            : 0;
        if (directionFilter == 1)
            hierarchyQuery.direction = HierarchyQuery::Children;
        else if (directionFilter == 2)
            hierarchyQuery.direction = HierarchyQuery::Parents;
        else
            hierarchyQuery.direction = HierarchyQuery::Both;

        const HierarchyReport report =
            HierarchyService::getInstance()->getHierarchyReport(hierarchyQuery);

        const bool hadExpandableItems = treeHasExpandableItems(relationshipsTree);
        const QSet<QString> expandedKeys = collectExpandedKeys(relationshipsTree);
        relationshipsTree->clear();
        QMap<int, QTreeWidgetItem*> itemByNodeId;
        QMap<QString, QTreeWidgetItem*> rootDirectionGroups;
        QTreeWidgetItem* rootItem = nullptr;
        int visibleCount = 0;
        for (const HierarchyNode& node : report.nodes) {
            if (node.symbol.symbolId < 0)
                continue;

            QTreeWidgetItem* parent = relationshipsTree->invisibleRootItem();
            if (node.depth == 0) {
                rootItem = createHierarchyItem(parent, node, QStringLiteral("Root"));
                itemByNodeId.insert(node.nodeId, rootItem);
                visibleCount++;
                continue;
            }

            if (node.parentNodeId >= 0 && itemByNodeId.contains(node.parentNodeId))
                parent = itemByNodeId.value(node.parentNodeId);
            if (node.parentNodeId == 0 && rootItem) {
                const QString directionText = hierarchyDirectionText(node.direction);
                parent = getOrCreateChildGroup(rootItem,
                                               rootDirectionGroups,
                                               directionText,
                                               0,
                                               directionText);
            }

            QTreeWidgetItem* item = createHierarchyItem(
                parent,
                node,
                hierarchyDirectionText(node.direction));
            itemByNodeId.insert(node.nodeId, item);
            visibleCount++;
        }
        for (auto it = rootDirectionGroups.begin(); it != rootDirectionGroups.end(); ++it) {
            const HierarchyQuery::Direction direction = it.key() == QStringLiteral("Outgoing")
                ? HierarchyQuery::Children
                : HierarchyQuery::Parents;
            it.value()->setText(0, countLabel(it.key(),
                                              report.rootDirectionCounts.value(direction)));
        }
        restoreTreeExpansion(relationshipsTree, hadExpandableItems, expandedKeys);

        if (relationshipsDock) {
            relationshipsDock->setWindowTitle(
                QStringLiteral("Relationships: %1 tree (%2)")
                    .arg(currentRelationshipSymbolName)
                    .arg(visibleCount));
            relationshipsDock->show();
            relationshipsDock->raise();
        }

        if (statusBar()) {
            statusBar()->showMessage(
                QStringLiteral("Found %1 hierarchy nodes for %2")
                    .arg(visibleCount)
                    .arg(currentRelationshipSymbolName),
                3000);
        }
        return;
    }

    const int directionFilter = relationshipDirectionCombo
        ? relationshipDirectionCombo->currentData().toInt()
        : 0;

    RelationshipBrowseQuery browseQuery;
    browseQuery.symbolName = query.symbolName;
    browseQuery.fileName = query.fileName;
    browseQuery.moduleName = query.moduleName;
    browseQuery.types = query.types;
    browseQuery.includeOutgoing = directionFilter == 0 || directionFilter == 1;
    browseQuery.includeIncoming = directionFilter == 0 || directionFilter == 2;

    const RelationshipReport report =
        RelationshipService::getInstance()->findRelationshipReport(browseQuery);
    const QString subjectName = report.subjectSymbol.symbolName.isEmpty()
        ? currentRelationshipSymbolName
        : report.subjectSymbol.symbolName;

    const bool hadExpandableItems = treeHasExpandableItems(relationshipsTree);
    const QSet<QString> expandedKeys = collectExpandedKeys(relationshipsTree);
    relationshipsTree->clear();
    for (const RelationshipDirectionGroup& directionGroupReport : report.directionGroups) {
        const QString direction = directionGroupReport.direction == DirectedRelationshipResult::Outgoing
            ? QStringLiteral("Outgoing")
            : QStringLiteral("Incoming");
        auto* directionGroup = new QTreeWidgetItem(relationshipsTree);
        directionGroup->setText(0, countLabel(direction, directionGroupReport.count));

        for (const RelationshipTypeGroup& typeGroupReport : directionGroupReport.typeGroups) {
            auto* typeGroup = new QTreeWidgetItem(directionGroup);
            const QString typeText = relationshipTypeText(typeGroupReport.type);
            typeGroup->setText(4, countLabel(typeText, typeGroupReport.count));

            for (const DirectedRelationshipResult& directed : typeGroupReport.relationships) {
                createRelationshipItem(typeGroup,
                                       direction,
                                       directed.peerSymbol,
                                       directed.relationship.relationship.type);
            }
        }
    }
    restoreTreeExpansion(relationshipsTree, hadExpandableItems, expandedKeys);

    if (relationshipsDock) {
        relationshipsDock->setWindowTitle(
            QStringLiteral("Relationships: %1 (%2)")
                .arg(subjectName)
                .arg(report.totalCount));
        relationshipsDock->show();
        relationshipsDock->raise();
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Found %1 relationships for %2")
                .arg(report.totalCount)
                .arg(subjectName),
            3000);
    }
}

void MainWindow::connectNavigationSignals()
{
    connect(navigationManager.get(), &NavigationManager::navigationRequested,
            this, &MainWindow::onNavigationRequested);

    connect(navigationManager.get(), &NavigationManager::symbolNavigationRequested,
            this, &MainWindow::onSymbolNavigationRequested);

    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this](MyCodeEditor* editor) {
                if (editor) {
                    connect(editor, &MyCodeEditor::definitionJumpRequested,
                            this, [this](const QString&, const QString& file, int line) {
                                navigateToFileAndLine(file, line);
                            });
                    connect(editor, &MyCodeEditor::referenceSearchRequested,
                            this, &MainWindow::showReferencesForSymbol);
                    connect(editor, &MyCodeEditor::relationshipBrowseRequested,
                            this, &MainWindow::showRelationshipsForSymbol);
                }
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [this](MyCodeEditor* editor) {
                if (editor && navigationManager) {
                    navigationManager->onTabChanged(editor->getFileName());
                }
                if (problemsScopeCombo && problemsScopeCombo->currentData().toInt() == 0)
                    updateProblemsPanel();
            });
}

void MainWindow::onNavigationRequested(const QString& filePath, int lineNumber)
{
    navigateToFileAndLine(filePath, lineNumber);
}

void MainWindow::onSymbolNavigationRequested(const sym_list::SymbolInfo& symbol)
{
    navigateToFileAndLine(symbol.fileName, symbol.startLine);
}

void MainWindow::navigateToFileAndLine(const QString& filePath, int lineNumber, int columnNumber)
{
    if (filePath.isEmpty()) return;

    bool fileFound = false;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        MyCodeEditor* editor = tabManager->getEditorAt(i);
        if (editor && editor->getFileName() == filePath) {
            ui->tabWidget->setCurrentIndex(i);
            fileFound = true;
            break;
        }
    }

    if (!fileFound) {
        if (!tabManager->openFileInTab(filePath)) {
            return;
        }
    }

    if (lineNumber > 0) {
        MyCodeEditor* currentEditor = tabManager->getCurrentEditor();
        if (currentEditor) {
            QTextCursor cursor = currentEditor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            for (int i = 1; i < lineNumber; ++i) {
                cursor.movePosition(QTextCursor::Down);
            }
            if (columnNumber > 1) {
                cursor.movePosition(QTextCursor::Right,
                                    QTextCursor::MoveAnchor,
                                    columnNumber - 1);
            }
            currentEditor->setTextCursor(cursor);
            currentEditor->centerCursor();
            currentEditor->setFocus();
            currentEditor->moveMouseToCursor();
        }
    }
}

void MainWindow::on_new_file_triggered()
{
    tabManager->createNewTab();
}

void MainWindow::on_open_file_triggered()
{
    tabManager->openFileInTab(QString()); // Empty string triggers file dialog
}

void MainWindow::on_save_file_triggered()
{
    tabManager->saveCurrentTab();
}

void MainWindow::on_save_as_triggered()
{
    tabManager->saveAsCurrentTab();
}

void MainWindow::on_copy_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->copy();
    }
}

void MainWindow::on_paste_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->paste();
    }
}

void MainWindow::on_cut_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->cut();
    }
}

void MainWindow::on_undo_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->undo();
    }
}

void MainWindow::on_redo_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->redo();
    }
}

void MainWindow::on_open_direction_as_workspace_triggered()
{
    workspaceManager->openWorkspace(QString()); // Empty string triggers folder dialog
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (tabManager->hasUnsavedChanges()) {
        QMessageBox::question(
            this,
            "Warning",
            "There are unsaved changes. Quit?",
            QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes ? event->accept() : event->ignore();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (modeManager->handleKeyPress(event)) {
        return; // Event handled by mode manager
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (modeManager->handleKeyRelease(event)) {
        return; // Event handled by mode manager
    }
    QMainWindow::keyReleaseEvent(event);
}


void MainWindow::setupRelationshipEngine()
{
    if (!relationshipEngine) return;

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->attachRelationshipEngine(relationshipEngine.get());

    slangManager = std::make_unique<SlangManager>();
    CompletionManager* completionManager = CompletionManager::getInstance();
    completionManager->setSlangManager(slangManager.get());
    completionManager->setRelationshipEngine(relationshipEngine.get());

    relationshipBuilder = semanticIndex->createRelationshipBuilder(
        relationshipEngine.get(), slangManager.get(), this);

}

void MainWindow::onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound)
{
    CompletionManager::getInstance()->refreshRelationshipData();
    if (statusBar()) {
        statusBar()->showMessage(
            QString("Smart analysis completed: %1 relationships in %2")
            .arg(relationshipsFound).arg(QFileInfo(fileName).fileName()),
            2000);
    }
}

void MainWindow::onRelationshipAnalysisError(const QString& fileName, const QString& error)
{
    Q_UNUSED(fileName)
    if (statusBar()) {
        statusBar()->showMessage(
            QString("Analysis error: %1").arg(error), 3000);
    }
}

void MainWindow::requestSingleFileRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (analysisScheduler)
        analysisScheduler->requestRelationshipAnalysis(fileName, content);
}

void MainWindow::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    if (analysisScheduler)
        analysisScheduler->scheduleOpenFileAnalysis(fileName, delayMs);
}

void MainWindow::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    if (analysisScheduler)
        analysisScheduler->cancelScheduledOpenFileAnalysis(fileName);
}

void MainWindow::onSingleFileRelationshipFinished(
    const SingleFileRelationshipAnalysisResult& result)
{
    onRelationshipAnalysisCompleted(result.fileName, result.relationships.size());
}

void MainWindow::onWorkspaceRelationshipAnalysisFinished(
    const WorkspaceRelationshipAnalysisResult& result)
{
    Q_UNUSED(result)
    CompletionManager::getInstance()->refreshRelationshipData();
}
