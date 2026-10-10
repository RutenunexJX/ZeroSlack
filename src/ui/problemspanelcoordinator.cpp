#include "problemspanelcoordinator.h"

#include "activitylogservice.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include "uitypography.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kGroupKeyRole = Qt::UserRole + 3;
constexpr int kIdentityRole = Qt::UserRole + 4;
const QStringList severityNames{"All", "Errors", "Warnings", "Info"};

QTreeWidgetItem* createDiagnosticItem(QTreeWidgetItem* parent, const DiagnosticResult& result)
{
    const auto& diagnostic = result.diagnostic;
    auto* item = new QTreeWidgetItem(parent);
    item->setIcon(0, RoundedIcons::icon(diagnostic.severity == SemanticDiagnostic::Error
        ? RoundedIcons::Error : diagnostic.severity == SemanticDiagnostic::Warning
            ? RoundedIcons::Warning : RoundedIcons::Info));
    item->setData(0, Qt::AccessibleTextRole, result.severityDisplayName);
    item->setText(1, result.messageDisplayName);
    item->setText(2, diagnostic.line > 0
        ? QStringLiteral("%1:%2").arg(diagnostic.line).arg(qMax(1, diagnostic.column)) : QString());
    const QString detail = QStringLiteral("%1\n%2\n%3\nSource: %4")
        .arg(result.severityDisplayName, result.messageDisplayName,
             diagnostic.fileName.isEmpty() ? QStringLiteral("Workspace")
                 : QStringLiteral("%1:%2").arg(diagnostic.fileName, item->text(2)), result.ownerDisplayName);
    for (int column = 0; column < 3; ++column) {
        item->setToolTip(column, detail);
        item->setData(column, Qt::AccessibleDescriptionRole, detail);
    }
    item->setData(0, Qt::UserRole, diagnostic.fileName);
    item->setData(0, Qt::UserRole + 1, diagnostic.line);
    item->setData(0, Qt::UserRole + 2, diagnostic.column);
    item->setData(0, kIdentityRole, QStringList{diagnostic.fileName, QString::number(diagnostic.line),
        QString::number(diagnostic.column), result.severityDisplayName, result.messageDisplayName,
        result.ownerDisplayName}.join(QChar(0)));
    return item;
}
}

ProblemsPanelCoordinator::ProblemsPanelCoordinator(QWidget* parent) : QObject(parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);
    problemsScopeCombo = UiControls::comboBox(panel);
    problemsScopeCombo->setObjectName(QStringLiteral("problemsScopeCombo"));
    problemsScopeCombo->addItem(QStringLiteral("Current File"), 0);
    problemsScopeCombo->addItem(QStringLiteral("Workspace Files"), 1);
    problemsScopeCombo->addItem(QStringLiteral("All Files"), 2);
    problemsScopeCombo->setToolTip(QStringLiteral("Problem scope"));
    problemsScopeCombo->setAccessibleName(QStringLiteral("Problem scope"));
    toolbar->addWidget(problemsScopeCombo);
    severityButtons = new QButtonGroup(this);
    severityButtons->setExclusive(true);
    for (int i = 0; i < severityNames.size(); ++i) {
        auto* button = UiControls::pushButton(severityNames[i] + QStringLiteral(" 0"), panel);
        button->setObjectName(QStringLiteral("problems%1Button").arg(severityNames[i]));
        button->setCheckable(true);
        button->setChecked(i == 0);
        severityButtons->addButton(button, i);
        toolbar->addWidget(button);
    }
    toolbar->addStretch(1);
    diagnosticStateLabel = UiControls::label(panel);
    diagnosticStateLabel->setObjectName(QStringLiteral("diagnosticStateLabel"));
    UiTypography::apply(diagnosticStateLabel, UiTypography::Role::Metadata);
    toolbar->addWidget(diagnosticStateLabel);
    layout->addLayout(toolbar);

    contentStack = new QStackedWidget(panel);
    contentStack->setObjectName(QStringLiteral("problemsContent"));
    contentStack->installEventFilter(this);
    contentStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    problemsTree = UiControls::treeWidget(contentStack);
    problemsTree->setObjectName(QStringLiteral("problemsTree"));
    problemsTree->installEventFilter(this);
    problemsTree->setColumnCount(3);
    problemsTree->setHeaderLabels({QString(), QStringLiteral("Message"), QStringLiteral("Location")});
    problemsTree->setAlternatingRowColors(false);
    problemsTree->setUniformRowHeights(true);
    problemsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTree->setAllColumnsShowFocus(true);
    problemsTree->header()->setStretchLastSection(false);
    problemsTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    problemsTree->setColumnWidth(0, 36);
    problemsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    problemsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    contentStack->addWidget(problemsTree);
    emptyStateLabel = UiControls::label(QStringLiteral("No file open"), contentStack);
    emptyStateLabel->setObjectName(QStringLiteral("problemsEmptyState"));
    emptyStateLabel->setAlignment(Qt::AlignCenter);
    emptyStateLabel->setWordWrap(true);
    contentStack->addWidget(emptyStateLabel);
    layout->addWidget(contentStack, 1);

    problemsDock = new QDockWidget(QStringLiteral("Problems"), parent);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(panel);
    problemsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    connect(problemsScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { update(); });
    connect(severityButtons, &QButtonGroup::idClicked, this, [this] { update(); });
    connect(problemsTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) { navigateItem(item); });
    update();
}

void ProblemsPanelCoordinator::setCurrentFileProvider(std::function<QString()> provider) { currentFileProvider = std::move(provider); }
void ProblemsPanelCoordinator::setWorkspaceFilesProvider(std::function<QStringList()> provider) { workspaceFilesProvider = std::move(provider); }
void ProblemsPanelCoordinator::setWorkspaceRootProvider(std::function<QString()> provider) { workspaceRootProvider = std::move(provider); }
void ProblemsPanelCoordinator::setNavigationHandler(std::function<bool(const QString&, int, int)> handler) { navigationHandler = std::move(handler); }
void ProblemsPanelCoordinator::setStatusMessageHandler(std::function<void(const QString&, int)> handler) { statusMessageHandler = std::move(handler); }
DiagnosticSeverityFilter ProblemsPanelCoordinator::severityFilter() const { return static_cast<DiagnosticSeverityFilter>(qMax(0, severityButtons->checkedId())); }
QAbstractButton* ProblemsPanelCoordinator::severityButton(DiagnosticSeverityFilter filter) const { return severityButtons->button(static_cast<int>(filter)); }

void ProblemsPanelCoordinator::setAnalysisState(const QString& state)
{
    externalAnalysisState = state.trimmed().toLower();
    if (displayedReport) updateStatus(*displayedReport);
}

void ProblemsPanelCoordinator::updateStatus(const DiagnosticPanelReport& report)
{
    const bool noFile = showsCurrentFileScope() && (!currentFileProvider || currentFileProvider().isEmpty());
    QString state = externalAnalysisState;
    if (state.isEmpty() && report.availableBands.analysisBandCounts.value(QStringLiteral("unbanded")) > 0)
        state = QStringLiteral("stale");
    const bool analyzing = state == QStringLiteral("analyzing") || state == QStringLiteral("pending") || state == QStringLiteral("queued");
    const bool failed = state.contains(QStringLiteral("fail")) || state.contains(QStringLiteral("error"));
    QString status;
    if (!noFile) {
        if (analyzing) status = QStringLiteral("Analyzing…");
        else if (failed) status = QStringLiteral("Analysis failed");
        else if (state == QStringLiteral("stale")) status = QStringLiteral("Results need update");
    }
    diagnosticStateLabel->setText(status);
    diagnosticStateLabel->setVisible(!status.isEmpty());
    int total = 0;
    for (int count : report.scopeSeverityCounts) total += count;
    emptyStateLabel->setText(noFile ? QStringLiteral("No file open")
        : analyzing ? QStringLiteral("Analyzing…")
        : failed ? QStringLiteral("Analysis failed")
        : total > 0 ? QStringLiteral("No issues match the selected filter") : QStringLiteral("No issues found"));
    contentStack->setCurrentWidget(report.visible.totalCount > 0 ? static_cast<QWidget*>(problemsTree) : emptyStateLabel);
}

void ProblemsPanelCoordinator::update()
{
    ++updateInvocations;
    DiagnosticPanelQueryOptions options;
    options.scope = static_cast<DiagnosticPanelScope>(problemsScopeCombo->currentData().toInt());
    options.severity = severityFilter();
    if (currentFileProvider) options.currentFileName = currentFileProvider();
    if (workspaceFilesProvider) options.workspaceFiles = workspaceFilesProvider();
    const auto projection = DiagnosticService::getInstance()->reportForPanel(options);
    updateStatus(*projection);
    if (projection == displayedReport) return;
    displayedReport = projection;
    const auto& report = projection->visible;
    const auto& counts = projection->scopeSeverityCounts;
    const QList<int> values{counts.value(SemanticDiagnostic::Error) + counts.value(SemanticDiagnostic::Warning) + counts.value(SemanticDiagnostic::Info),
        counts.value(SemanticDiagnostic::Error), counts.value(SemanticDiagnostic::Warning), counts.value(SemanticDiagnostic::Info)};
    for (int i = 0; i < severityNames.size(); ++i) {
        auto* button = severityButtons->button(i);
        button->setText(QStringLiteral("%1 %2").arg(severityNames[i]).arg(values[i]));
        button->setAccessibleName(button->text());
        button->setToolTip(QStringLiteral("%1 in the selected scope").arg(button->text()));
    }
    if (report.totalCount > 0) {
        const QString message = QStringLiteral("Diagnostics visible: %1 %2; %3").arg(report.totalCount)
            .arg(report.totalCount == 1 ? QStringLiteral("diagnostic") : QStringLiteral("diagnostics"), report.analysisBandSummaryText());
        if (message != lastDiagnosticActivityMessage) {
            ActivityLogService::getInstance()->append(QStringLiteral("Analyzer"), ActivityLogLevel::Info, message);
            lastDiagnosticActivityMessage = message;
        }
    } else lastDiagnosticActivityMessage.clear();

    QSet<QString> collapsedFiles;
    for (int i = 0; i < problemsTree->topLevelItemCount(); ++i) {
        auto* item = problemsTree->topLevelItem(i);
        if (item->childCount() > 0 && !item->isExpanded()) collapsedFiles.insert(item->data(0, kGroupKeyRole).toString());
    }
    const QString selected = problemsTree->currentItem() ? problemsTree->currentItem()->data(0, kIdentityRole).toString() : QString();
    const int vertical = problemsTree->verticalScrollBar()->value(), horizontal = problemsTree->horizontalScrollBar()->value();
    const QSignalBlocker blocker(problemsTree);
    const bool updates = problemsTree->updatesEnabled();
    problemsTree->setUpdatesEnabled(false);
    problemsTree->clear();
    problemsTree->setRootIsDecorated(!showsCurrentFileScope());
    if (showsCurrentFileScope()) {
        for (const auto& result : report.diagnostics) createDiagnosticItem(problemsTree->invisibleRootItem(), result);
    } else {
        const QString root = workspaceRootProvider ? workspaceRootProvider() : QString();
        for (const auto& group : report.fileGroups) {
            auto* item = new QTreeWidgetItem(problemsTree);
            const QString path = group.fileName.isEmpty() ? QStringLiteral("Workspace")
                : root.isEmpty() ? group.fileName : QDir(root).relativeFilePath(group.fileName);
            item->setText(0, QStringLiteral("%1 (%2)").arg(path).arg(group.count));
            item->setFirstColumnSpanned(true);
            item->setToolTip(0, group.fileName);
            item->setData(0, kGroupKeyRole, group.fileKey);
            for (const auto& result : group.diagnostics) createDiagnosticItem(item, result);
            item->setExpanded(!collapsedFiles.contains(group.fileKey));
        }
    }
    if (!selected.isEmpty()) for (QTreeWidgetItemIterator it(problemsTree); *it; ++it) {
        if ((*it)->data(0, kIdentityRole).toString() == selected) { problemsTree->setCurrentItem(*it); break; }
    }
    problemsTree->verticalScrollBar()->setValue(vertical);
    problemsTree->horizontalScrollBar()->setValue(horizontal);
    problemsTree->setUpdatesEnabled(updates);
    problemsDock->setProperty("bottomBadgeText", report.totalCount > 0 ? QString::number(report.totalCount) : QString());
    problemsDock->setProperty("bottomBadgeTone", report.severityCounts.value(SemanticDiagnostic::Error) > 0 ? QStringLiteral("error")
        : report.severityCounts.value(SemanticDiagnostic::Warning) > 0 ? QStringLiteral("warning") : report.totalCount > 0 ? QStringLiteral("info") : QString());
    problemsDock->setWindowTitle(QStringLiteral("Problems (%1)").arg(report.totalCount));
}

void ProblemsPanelCoordinator::navigateItem(QTreeWidgetItem* item)
{
    if (!item || !navigationHandler) return;
    const QString file = item->data(0, Qt::UserRole).toString();
    if (file.isEmpty()) return;
    const int line = item->data(0, Qt::UserRole + 1).toInt(), column = item->data(0, Qt::UserRole + 2).toInt();
    QString failure;
    if (!QFileInfo::exists(file)) failure = QStringLiteral("Diagnostic file does not exist: %1").arg(file);
    else if (line <= 0) failure = QStringLiteral("Diagnostic location is invalid");
    else if (!navigationHandler(file, line, column)) failure = QStringLiteral("Failed to open diagnostic location");
    if (!failure.isEmpty() && statusMessageHandler) statusMessageHandler(failure, 4000);
}

bool ProblemsPanelCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == contentStack && event->type() == QEvent::Show) {
        QMetaObject::invokeMethod(this, [this] { if (isVisibleToUser()) update(); }, Qt::QueuedConnection);
    }
    if (watched == problemsTree && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            auto* item = problemsTree->currentItem();
            if (item && item->childCount() > 0) item->setExpanded(!item->isExpanded());
            else navigateItem(item);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool ProblemsPanelCoordinator::showsCurrentFileScope() const { return problemsScopeCombo->currentData().toInt() == 0; }
bool ProblemsPanelCoordinator::isVisibleToUser() const { return contentStack && contentStack->isVisible(); }
