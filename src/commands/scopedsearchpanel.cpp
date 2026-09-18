#include "scopedsearchpanel.h"

#include "scopedreplaceworkflow.h"
#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include <rtledit/edit_plan.h>

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kResultIndexRole = Qt::UserRole + 1;
constexpr int kFileNameRole = Qt::UserRole + 2;
constexpr int kLineRole = Qt::UserRole + 3;
constexpr int kColumnRole = Qt::UserRole + 4;
constexpr int kFileIdentityRole = Qt::UserRole + 5;
constexpr int kMatchIdentityRole = Qt::UserRole + 6;

QString resultIdentityKey(const ScopedSearchResult& result)
{
    if (result.identity.isValid())
        return result.identity.toString();
    return QStringLiteral("%1|%2:%3")
        .arg(result.fileName)
        .arg(result.matchStartChar)
        .arg(result.matchEndChar);
}

QString fileIdentityKey(const ScopedSearchResult& result)
{
    return result.identity.fileIdentity.isEmpty()
        ? result.fileName
        : result.identity.fileIdentity;
}

QString contextDisplayText(const SearchContextSnippet& context)
{
    QString display = context.text;
    display.replace(QStringLiteral("\r\n"),
                    QStringLiteral("\n"));
    display.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    display.replace(QLatin1Char('\n'),
                    QStringLiteral("  \u23ce  "));
    return display.trimmed();
}

QString resultOriginText(const ScopedSearchResult& result)
{
    if (result.fromTextSearch && result.fromSemanticSearch)
        return QStringLiteral("Text + Semantic");
    if (result.fromSemanticSearch)
        return QStringLiteral("Semantic");
    return QStringLiteral("Text");
}

QString searchFailureText(ScopedSearchStatus status)
{
    switch (status) {
    case ScopedSearchStatus::EmptyQuery:
        return QStringLiteral("Enter a search query.");
    case ScopedSearchStatus::ActiveDocumentNotFound:
        return QStringLiteral(
            "The active document is not available in the search snapshot.");
    case ScopedSearchStatus::SyntaxSnapshotRequired:
        return QStringLiteral(
            "A current syntax snapshot is required for this scope.");
    case ScopedSearchStatus::NoEnclosingScope:
        return QStringLiteral(
            "No enclosing syntax scope exists at the current cursor.");
    case ScopedSearchStatus::Ready:
        break;
    }
    return QString();
}

QString replaceFailureText(const ReplacePreviewPlan& preview)
{
    if (!preview.failureReason.isEmpty())
        return preview.failureReason;
    switch (preview.status) {
    case ReplacePreviewStatus::NoSelectedMatches:
        return QStringLiteral("No replacement matches are selected.");
    case ReplacePreviewStatus::StaleSearchResult:
        return QStringLiteral(
            "Search results changed; run the search again.");
    case ReplacePreviewStatus::OverlappingMatches:
        return QStringLiteral(
            "Selected replacement matches overlap.");
    case ReplacePreviewStatus::InvalidTransactionPlan:
        return QStringLiteral(
            "The replacement preview could not be validated.");
    case ReplacePreviewStatus::Ready:
        break;
    }
    return QString();
}

ScopedSearchScope scopeFromCombo(const QComboBox* combo)
{
    if (!combo)
        return ScopedSearchScope::Workspace;
    return static_cast<ScopedSearchScope>(
        combo->currentData().toInt());
}

int comboIndexForScope(const QComboBox* combo,
                       ScopedSearchScope scope)
{
    if (!combo)
        return -1;
    return combo->findData(static_cast<int>(scope));
}
} // namespace

ScopedSearchPanel::ScopedSearchPanel(
    const SearchService* service,
    QWidget* parent)
    : QWidget(parent)
    , searchService(service ? service
                            : SearchService::getInstance())
{
    setObjectName(QStringLiteral("scopedSearchPanel"));
    buildUi();
}

void ScopedSearchPanel::setSearchService(
    const SearchService* service)
{
    searchService =
        service ? service : SearchService::getInstance();
    invalidateReplacePreview(
        QStringLiteral("Search service changed; run Search again."));
}

void ScopedSearchPanel::setContextProvider(
    ContextProvider provider)
{
    contextProvider = std::move(provider);
    invalidateReplacePreview(
        QStringLiteral("Search context changed; run Search again."));
}

void ScopedSearchPanel::setSearchContext(
    const ScopedSearchPanelContext& context)
{
    fallbackContext = context;
    invalidateReplacePreview(
        QStringLiteral("Search context changed; run Search again."));
}

void ScopedSearchPanel::setReplaceWorkflow(
    ScopedReplaceWorkflow* workflow)
{
    if (replaceWorkflow == workflow)
        return;
    if (replaceWorkflow) {
        replaceWorkflow->discardPendingPreview();
        disconnect(
            replaceWorkflow.data(), nullptr, this, nullptr);
    }
    replaceWorkflow = workflow;
    if (replaceWorkflow) {
        connect(
            replaceWorkflow.data(),
            &ScopedReplaceWorkflow::stateChanged,
            this,
            &ScopedSearchPanel::updateWorkflowUi);
    }
    if (replaceDiffView)
        replaceDiffView->clear();
    updateReplaceButtons();
}

void ScopedSearchPanel::setQueryText(const QString& text)
{
    queryEdit->setText(text);
}

QString ScopedSearchPanel::queryText() const
{
    return queryEdit ? queryEdit->text() : QString();
}

void ScopedSearchPanel::setScope(ScopedSearchScope requestedScope)
{
    const int index =
        comboIndexForScope(scopeCombo, requestedScope);
    if (index >= 0)
        scopeCombo->setCurrentIndex(index);
}

ScopedSearchScope ScopedSearchPanel::scope() const
{
    return scopeFromCombo(scopeCombo);
}

void ScopedSearchPanel::setReplacementText(
    const QString& text)
{
    replacementEdit->setText(text);
}

QString ScopedSearchPanel::replacementText() const
{
    return replacementEdit
        ? replacementEdit->text()
        : QString();
}

const ScopedSearchResponse& ScopedSearchPanel::response() const
{
    return currentResponse;
}

const ReplacePreviewPlan&
ScopedSearchPanel::replacePreview() const
{
    return currentReplacePreview;
}

int ScopedSearchPanel::displayedResultCount() const
{
    return renderedResultCount;
}

int ScopedSearchPanel::displayedReplaceMatchCount() const
{
    return renderedReplaceMatchCount;
}

void ScopedSearchPanel::refresh()
{
    if (replaceWorkflow)
        replaceWorkflow->discardPendingPreview();
    if (replaceDiffView)
        replaceDiffView->clear();
    updateReplaceButtons();
    searchedContext = currentContext();
    searchedContext.documents =
        documentsForScope(searchedContext);
    currentReplacePreview = ReplacePreviewPlan{};

    if (!searchService) {
        currentResponse = ScopedSearchResponse{};
        currentResponse.status =
            ScopedSearchStatus::ActiveDocumentNotFound;
        renderResponse();
        renderReplaceChecklist();
        searchStatusLabel->setText(
            QStringLiteral("Search service is unavailable."));
        replaceStatusLabel->setText(
            QStringLiteral("Run Search before building a preview."));
        emit searchCompleted(currentResponse);
        return;
    }

    currentResponse =
        searchService->search(
            currentQuery(searchedContext),
            searchedContext.documents);
    renderResponse();
    renderReplaceChecklist();
    replaceStatusLabel->setText(
        currentResponse.results.isEmpty()
        ? QStringLiteral("No replacement candidates.")
        : QStringLiteral(
              "Select files and matches, then build a Diff preview."));
    emit searchCompleted(currentResponse);
}

ReplacePreviewPlan ScopedSearchPanel::buildReplacePreview()
{
    if (replaceWorkflow)
        replaceWorkflow->discardPendingPreview();
    if (replaceDiffView)
        replaceDiffView->clear();
    updateReplaceButtons();
    QList<SearchDocumentSnapshot> previewDocuments;
    if (!searchService) {
        currentReplacePreview = ReplacePreviewPlan{};
        currentReplacePreview.status =
            ReplacePreviewStatus::InvalidTransactionPlan;
        currentReplacePreview.failureReason =
            QStringLiteral("Search service is unavailable.");
    } else {
        const ScopedSearchPanelContext latestContext =
            currentContext();
        previewDocuments =
            documentsForScope(latestContext);
        currentReplacePreview =
            searchService->planReplace(
                currentResponse.results,
                previewDocuments,
                replaceRequestFromChecklist());
    }

    if (currentReplacePreview.ready()) {
        const bool isRequiredPreview =
            currentReplacePreview.transactionPlan.riskLevel
                == rtledit::RiskLevel::High
            && currentReplacePreview.transactionPlan.previewPolicy
                == rtledit::PreviewPolicy::Diff;
        if (!isRequiredPreview) {
            currentReplacePreview.status =
                ReplacePreviewStatus::InvalidTransactionPlan;
            currentReplacePreview.failureReason =
                QStringLiteral(
                    "Replace previews must be High-risk Diff plans.");
            currentReplacePreview.transactionPlan =
                rtledit::WorkspaceEditPlan{};
        }
    }

    if (currentReplacePreview.ready()) {
        QSet<QString> editedFiles;
        for (const rtledit::WorkspaceTextEdit& edit :
             currentReplacePreview.transactionPlan.edits) {
            editedFiles.insert(
                QString::fromStdString(edit.filePath));
        }
        replaceStatusLabel->setText(
            QStringLiteral(
                "Preview ready: %1 file(s), %2 match(es); "
                "High risk, Diff required.")
                .arg(editedFiles.size())
                .arg(static_cast<qulonglong>(
                    currentReplacePreview
                        .transactionPlan.edits.size())));
    } else {
        replaceStatusLabel->setText(
            replaceFailureText(currentReplacePreview));
    }

    emit replacePreviewReady(currentReplacePreview);
    if (currentReplacePreview.ready()
        && replaceWorkflow) {
        replaceWorkflow->preparePreview(
            currentReplacePreview,
            previewDocuments,
            dryRunCheck && dryRunCheck->isChecked());
    } else {
        updateReplaceButtons();
    }
    return currentReplacePreview;
}

void ScopedSearchPanel::confirmReplace()
{
    if (replaceWorkflow) {
        replaceWorkflow->confirm(
            documentsForScope(currentContext()));
    }
}

void ScopedSearchPanel::cancelReplace()
{
    currentReplacePreview = ReplacePreviewPlan{};
    if (replaceWorkflow)
        replaceWorkflow->cancel();
    else {
        if (replaceDiffView)
            replaceDiffView->clear();
        if (replaceStatusLabel) {
            replaceStatusLabel->setText(
                QStringLiteral(
                    "The pending replace preview was cancelled."));
        }
        updateReplaceButtons();
    }
}

void ScopedSearchPanel::undoReplace()
{
    if (replaceWorkflow)
        replaceWorkflow->undo();
}

void ScopedSearchPanel::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(6);

    auto* searchControls = new QHBoxLayout();
    searchControls->setContentsMargins(0, 0, 0, 0);
    searchControls->setSpacing(6);

    queryEdit = new ElaLineEdit(this);
    queryEdit->setObjectName(
        QStringLiteral("scopedSearchQueryEdit"));
    queryEdit->setPlaceholderText(
        QStringLiteral("Search text"));
    queryEdit->setClearButtonEnabled(true);
    searchControls->addWidget(queryEdit, 1);

    scopeCombo = new ElaComboBox(this);
    scopeCombo->setObjectName(
        QStringLiteral("scopedSearchScopeCombo"));
    scopeCombo->addItem(
        QStringLiteral("Current Syntax Block"),
        static_cast<int>(ScopedSearchScope::SyntaxBlock));
    scopeCombo->addItem(
        QStringLiteral("Current Module"),
        static_cast<int>(ScopedSearchScope::Module));
    scopeCombo->addItem(
        QStringLiteral("Current File"),
        static_cast<int>(ScopedSearchScope::File));
    scopeCombo->addItem(
        QStringLiteral("Workspace"),
        static_cast<int>(ScopedSearchScope::Workspace));
    scopeCombo->setCurrentIndex(
        comboIndexForScope(
            scopeCombo,
            ScopedSearchScope::Workspace));
    searchControls->addWidget(scopeCombo);

    caseSensitiveCheck = new ElaCheckBox(
        QStringLiteral("Case sensitive"), this);
    caseSensitiveCheck->setObjectName(
        QStringLiteral("scopedSearchCaseSensitiveCheck"));
    searchControls->addWidget(caseSensitiveCheck);

    wholeWordCheck = new ElaCheckBox(
        QStringLiteral("Whole word"), this);
    wholeWordCheck->setObjectName(
        QStringLiteral("scopedSearchWholeWordCheck"));
    searchControls->addWidget(wholeWordCheck);

    semanticCheck = new ElaCheckBox(
        QStringLiteral("Semantic"), this);
    semanticCheck->setObjectName(
        QStringLiteral("scopedSearchSemanticCheck"));
    semanticCheck->setChecked(true);
    searchControls->addWidget(semanticCheck);

    searchButton = new ElaPushButton(
        QStringLiteral("Search"), this);
    searchButton->setObjectName(
        QStringLiteral("scopedSearchButton"));
    searchControls->addWidget(searchButton);
    rootLayout->addLayout(searchControls);

    searchStatusLabel = new QLabel(this);
    searchStatusLabel->setObjectName(
        QStringLiteral("scopedSearchStatusLabel"));
    searchStatusLabel->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Fixed);
    searchStatusLabel->setText(
        QStringLiteral("Enter a search query."));
    rootLayout->addWidget(searchStatusLabel);

    resultsTree = new QTreeWidget(this);
    resultsTree->setObjectName(
        QStringLiteral("scopedSearchResultsTree"));
    resultsTree->setColumnCount(3);
    resultsTree->setHeaderLabels(
        {QStringLiteral("File / Location"),
         QStringLiteral("Stable Context"),
         QStringLiteral("Source")});
    resultsTree->setRootIsDecorated(true);
    resultsTree->setAlternatingRowColors(true);
    resultsTree->setSelectionMode(
        QAbstractItemView::SingleSelection);
    resultsTree->setContextMenuPolicy(
        Qt::CustomContextMenu);
    resultsTree->header()->setStretchLastSection(false);
    resultsTree->header()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    resultsTree->header()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    resultsTree->header()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    rootLayout->addWidget(resultsTree, 2);

    auto* replaceGroup = new QGroupBox(
        QStringLiteral("Replace Preview"), this);
    replaceGroup->setObjectName(
        QStringLiteral("scopedSearchReplaceGroup"));
    auto* replaceLayout = new QVBoxLayout(replaceGroup);
    replaceLayout->setContentsMargins(6, 6, 6, 6);
    replaceLayout->setSpacing(4);

    auto* replaceControls = new QHBoxLayout();
    replaceControls->setContentsMargins(0, 0, 0, 0);
    replaceControls->setSpacing(6);
    auto* replaceLabel = new QLabel(
        QStringLiteral("Replace with:"), replaceGroup);
    replaceControls->addWidget(replaceLabel);
    replacementEdit = new ElaLineEdit(replaceGroup);
    replacementEdit->setObjectName(
        QStringLiteral("scopedSearchReplacementEdit"));
    replacementEdit->setClearButtonEnabled(true);
    replaceControls->addWidget(replacementEdit, 1);
    buildPreviewButton = new ElaPushButton(
        QStringLiteral("Build Diff Preview"),
        replaceGroup);
    buildPreviewButton->setObjectName(
        QStringLiteral(
            "scopedSearchBuildReplacePreviewButton"));
    buildPreviewButton->setEnabled(false);
    replaceControls->addWidget(buildPreviewButton);
    dryRunCheck = new ElaCheckBox(
        QStringLiteral("Dry run"), replaceGroup);
    dryRunCheck->setObjectName(
        QStringLiteral("scopedSearchReplaceDryRunCheck"));
    dryRunCheck->setToolTip(
        QStringLiteral(
            "Build and confirm a transaction plan without changing files."));
    replaceControls->addWidget(dryRunCheck);
    replaceLayout->addLayout(replaceControls);

    replaceTree = new QTreeWidget(replaceGroup);
    replaceTree->setObjectName(
        QStringLiteral("scopedSearchReplaceTree"));
    replaceTree->setColumnCount(2);
    replaceTree->setHeaderLabels(
        {QStringLiteral("File / Match"),
         QStringLiteral("Stable Context")});
    replaceTree->setRootIsDecorated(true);
    replaceTree->setAlternatingRowColors(true);
    replaceTree->setSelectionMode(
        QAbstractItemView::SingleSelection);
    replaceTree->header()->setStretchLastSection(false);
    replaceTree->header()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    replaceTree->header()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    replaceLayout->addWidget(replaceTree, 1);

    replaceDiffView =
        new QPlainTextEdit(replaceGroup);
    replaceDiffView->setObjectName(
        QStringLiteral("scopedSearchReplaceDiffView"));
    replaceDiffView->setReadOnly(true);
    replaceDiffView->setUndoRedoEnabled(false);
    replaceDiffView->setPlaceholderText(
        QStringLiteral(
            "The checked files and matches will be rendered here "
            "as a High-risk Diff before confirmation."));
    replaceDiffView->setMinimumHeight(0);
    replaceDiffView->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Ignored);
    replaceLayout->addWidget(replaceDiffView, 1);

    auto* transactionControls = new QHBoxLayout();
    transactionControls->setContentsMargins(0, 0, 0, 0);
    transactionControls->setSpacing(6);
    transactionControls->addStretch(1);
    applyReplaceButton = new ElaPushButton(
        QStringLiteral("Apply Confirmed Diff"),
        replaceGroup);
    applyReplaceButton->setObjectName(
        QStringLiteral("scopedSearchApplyReplaceButton"));
    applyReplaceButton->setEnabled(false);
    transactionControls->addWidget(applyReplaceButton);
    cancelReplaceButton = new ElaPushButton(
        QStringLiteral("Cancel Preview"),
        replaceGroup);
    cancelReplaceButton->setObjectName(
        QStringLiteral("scopedSearchCancelReplaceButton"));
    cancelReplaceButton->setEnabled(false);
    transactionControls->addWidget(cancelReplaceButton);
    undoReplaceButton = new ElaPushButton(
        QStringLiteral("Undo Replace"),
        replaceGroup);
    undoReplaceButton->setObjectName(
        QStringLiteral("scopedSearchUndoReplaceButton"));
    undoReplaceButton->setEnabled(false);
    transactionControls->addWidget(undoReplaceButton);
    replaceLayout->addLayout(transactionControls);

    replaceStatusLabel = new QLabel(replaceGroup);
    replaceStatusLabel->setObjectName(
        QStringLiteral("scopedSearchReplaceStatusLabel"));
    replaceStatusLabel->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Fixed);
    replaceStatusLabel->setText(
        QStringLiteral("Run Search before building a preview."));
    replaceLayout->addWidget(replaceStatusLabel);
    rootLayout->addWidget(replaceGroup, 2);

    connect(searchButton,
            &QPushButton::clicked,
            this,
            &ScopedSearchPanel::refresh);
    connect(queryEdit,
            &QLineEdit::returnPressed,
            this,
            &ScopedSearchPanel::refresh);
    connect(buildPreviewButton,
            &QPushButton::clicked,
            this,
            [this]() {
                buildReplacePreview();
            });
    connect(applyReplaceButton,
            &QPushButton::clicked,
            this,
            &ScopedSearchPanel::confirmReplace);
    connect(cancelReplaceButton,
            &QPushButton::clicked,
            this,
            &ScopedSearchPanel::cancelReplace);
    connect(undoReplaceButton,
            &QPushButton::clicked,
            this,
            &ScopedSearchPanel::undoReplace);
    connect(resultsTree,
            &QTreeWidget::itemDoubleClicked,
            this,
            [this](QTreeWidgetItem* item, int) {
                activateResultItem(item);
            });
    connect(resultsTree,
            &QTreeWidget::itemActivated,
            this,
            [this](QTreeWidgetItem* item, int) {
                activateResultItem(item);
            });
    connect(resultsTree,
            &QTreeWidget::customContextMenuRequested,
            this,
            &ScopedSearchPanel::showResultContextMenu);
    connect(replaceTree,
            &QTreeWidget::itemChanged,
            this,
            [this](QTreeWidgetItem* item, int) {
                updateReplaceSelection(item);
            });
    connect(replacementEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString&) {
                invalidateReplacePreview(
                    QStringLiteral(
                        "Replacement changed; build the preview again."));
            });
    connect(dryRunCheck,
            &QCheckBox::toggled,
            this,
            [this](bool) {
                invalidateReplacePreview(
                    QStringLiteral(
                        "Dry-run mode changed; build the preview again."));
            });
}

ScopedSearchPanelContext
ScopedSearchPanel::currentContext() const
{
    return contextProvider
        ? contextProvider()
        : fallbackContext;
}

QList<SearchDocumentSnapshot>
ScopedSearchPanel::documentsForScope(
    const ScopedSearchPanelContext& context) const
{
    if (scope() == ScopedSearchScope::Workspace
        && context.workspaceDocumentsSpecified) {
        return context.workspaceDocuments;
    }
    return context.documents;
}

ScopedSearchQuery ScopedSearchPanel::currentQuery(
    const ScopedSearchPanelContext& context) const
{
    ScopedSearchQuery query;
    query.text = queryText();
    query.scope = scope();
    query.activeFileName = context.activeFileName;
    query.cursorChar = context.cursorChar;
    query.caseSensitive =
        caseSensitiveCheck && caseSensitiveCheck->isChecked();
    query.wholeWord =
        wholeWordCheck && wholeWordCheck->isChecked();
    query.includeSemantic =
        !semanticCheck || semanticCheck->isChecked();
    return query;
}

void ScopedSearchPanel::renderResponse()
{
    resultsTree->setUpdatesEnabled(false);
    resultsTree->clear();
    renderedResultCount = 0;

    QHash<QString, QTreeWidgetItem*> fileItems;
    QSet<QString> renderedMatches;
    for (int index = 0;
         index < currentResponse.results.size();
         ++index) {
        const ScopedSearchResult& result =
            currentResponse.results.at(index);
        const QString matchKey = resultIdentityKey(result);
        if (renderedMatches.contains(matchKey))
            continue;
        renderedMatches.insert(matchKey);

        const QString fileKey = fileIdentityKey(result);
        QTreeWidgetItem* fileItem =
            fileItems.value(fileKey, nullptr);
        if (!fileItem) {
            fileItem = new QTreeWidgetItem(resultsTree);
            fileItem->setText(0, result.fileName);
            fileItem->setToolTip(0, result.fileName);
            fileItem->setData(
                0, kFileIdentityRole, fileKey);
            fileItem->setFirstColumnSpanned(false);
            fileItems.insert(fileKey, fileItem);
        }

        auto* matchItem = new QTreeWidgetItem(fileItem);
        matchItem->setText(
            0,
            QStringLiteral("%1:%2")
                .arg(result.line)
                .arg(result.column));
        matchItem->setText(
            1, contextDisplayText(result.context));
        matchItem->setToolTip(1, result.context.text);
        matchItem->setText(
            2, resultOriginText(result));
        matchItem->setData(
            0, kResultIndexRole, index);
        matchItem->setData(
            0, kFileNameRole, result.fileName);
        matchItem->setData(
            0, kLineRole, result.line);
        matchItem->setData(
            0, kColumnRole, result.column);
        matchItem->setData(
            0, kMatchIdentityRole, matchKey);
        ++renderedResultCount;
    }

    for (auto it = fileItems.cbegin();
         it != fileItems.cend();
         ++it) {
        QTreeWidgetItem* item = it.value();
        item->setText(
            1,
            QStringLiteral("%1 match(es)")
                .arg(item->childCount()));
        item->setExpanded(true);
    }
    resultsTree->setUpdatesEnabled(true);

    if (currentResponse.ready()) {
        searchStatusLabel->setText(
            QStringLiteral("%1 match(es) in %2 file(s).")
                .arg(renderedResultCount)
                .arg(fileItems.size()));
    } else {
        searchStatusLabel->setText(
            searchFailureText(currentResponse.status));
    }
}

void ScopedSearchPanel::renderReplaceChecklist()
{
    updatingReplaceChecklist = true;
    replaceTree->setUpdatesEnabled(false);
    replaceTree->clear();
    renderedReplaceMatchCount = 0;

    QHash<QString, QTreeWidgetItem*> fileItems;
    QSet<QString> renderedMatches;
    for (int index = 0;
         index < currentResponse.results.size();
         ++index) {
        const ScopedSearchResult& result =
            currentResponse.results.at(index);
        const QString matchKey = resultIdentityKey(result);
        if (renderedMatches.contains(matchKey))
            continue;
        renderedMatches.insert(matchKey);

        const QString fileKey = fileIdentityKey(result);
        QTreeWidgetItem* fileItem =
            fileItems.value(fileKey, nullptr);
        if (!fileItem) {
            fileItem = new QTreeWidgetItem(replaceTree);
            fileItem->setText(0, result.fileName);
            fileItem->setToolTip(0, result.fileName);
            fileItem->setData(
                0, kFileIdentityRole, fileKey);
            fileItem->setFlags(
                fileItem->flags()
                | Qt::ItemIsUserCheckable);
            fileItem->setCheckState(0, Qt::Checked);
            fileItems.insert(fileKey, fileItem);
        }

        auto* matchItem = new QTreeWidgetItem(fileItem);
        matchItem->setText(
            0,
            QStringLiteral("%1:%2  %3")
                .arg(result.line)
                .arg(result.column)
                .arg(result.matchedText));
        matchItem->setText(
            1, contextDisplayText(result.context));
        matchItem->setToolTip(1, result.context.text);
        matchItem->setData(
            0, kResultIndexRole, index);
        matchItem->setData(
            0, kMatchIdentityRole, matchKey);
        matchItem->setFlags(
            matchItem->flags()
            | Qt::ItemIsUserCheckable);
        matchItem->setCheckState(0, Qt::Checked);
        ++renderedReplaceMatchCount;
    }

    for (auto it = fileItems.cbegin();
         it != fileItems.cend();
         ++it) {
        QTreeWidgetItem* item = it.value();
        item->setText(
            1,
            QStringLiteral("%1 match(es)")
                .arg(item->childCount()));
        item->setExpanded(true);
    }
    replaceTree->setUpdatesEnabled(true);
    updatingReplaceChecklist = false;
    buildPreviewButton->setEnabled(
        renderedReplaceMatchCount > 0);
}

void ScopedSearchPanel::activateResultItem(
    QTreeWidgetItem* item)
{
    if (!item
        || !item->parent()
        || !item->data(0, kResultIndexRole).isValid()) {
        return;
    }
    const QString fileName =
        item->data(0, kFileNameRole).toString();
    const int line =
        item->data(0, kLineRole).toInt();
    const int column =
        item->data(0, kColumnRole).toInt();
    if (fileName.isEmpty() || line <= 0 || column <= 0)
        return;
    emit navigationRequested(fileName, line, column);
}

bool ScopedSearchPanel::requestTemporaryEditorOpenForItem(
    QTreeWidgetItem* item)
{
    if (!item || !item->parent())
        return false;
    const int resultIndex =
        item->data(0, kResultIndexRole).toInt();
    if (resultIndex < 0
        || resultIndex >= currentResponse.results.size()) {
        return false;
    }

    const ScopedSearchResult& result =
        currentResponse.results.at(resultIndex);
    EditorLocation location;
    location.filePath = result.fileName;
    location.line = qMax(1, result.line);
    location.column = qMax(1, result.column);
    location.symbolKey = result.hasSemanticRecord
        ? result.semanticRecord.name
        : result.matchedText;
    location.sourceLinkId = result.identity.isValid()
        ? result.identity.toString()
        : resultIdentityKey(result);
    if (!result.matchedText.isEmpty()
        && !result.matchedText.contains(QLatin1Char('\n'))
        && !result.matchedText.contains(QLatin1Char('\r'))) {
        EditorSelectionRange selection;
        selection.startLine = location.line;
        selection.startColumn = location.column;
        selection.endLine = location.line;
        selection.endColumn = location.column
            + result.matchedText.size();
        location.selection = selection;
    }
    if (!location.isValid())
        return false;
    emit temporaryEditorOpenRequested(location);
    return true;
}

void ScopedSearchPanel::showResultContextMenu(
    const QPoint& position)
{
    if (!resultsTree)
        return;
    QTreeWidgetItem* item =
        resultsTree->itemAt(position);
    if (!item || !item->parent())
        return;

    const ActionDescriptor* descriptor =
        findActionById(QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen));
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::ContextMenu)) {
        return;
    }
    const ActionAliasDescriptor alias =
        descriptor->aliasForSurface(
            ActionSurface::ContextMenu);
    QMenu menu(resultsTree);
    QAction* action = menu.addAction(
        alias.label.trimmed().isEmpty()
            ? descriptor->canonicalName
            : alias.label);
    action->setObjectName(
        QStringLiteral("scopedSearchTemporaryEditorAction"));
    action->setProperty(
        "actionId", descriptor->id);
    action->setProperty(
        "executionRoute",
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    action->setStatusTip(descriptor->description);
    if (menu.exec(
            resultsTree->viewport()->mapToGlobal(position))
        == action) {
        requestTemporaryEditorOpenForItem(item);
    }
}

void ScopedSearchPanel::updateReplaceSelection(
    QTreeWidgetItem* item)
{
    if (updatingReplaceChecklist || !item)
        return;

    updatingReplaceChecklist = true;
    if (!item->parent()) {
        const Qt::CheckState childState =
            item->checkState(0) == Qt::Unchecked
            ? Qt::Unchecked
            : Qt::Checked;
        for (int index = 0;
             index < item->childCount();
             ++index) {
            item->child(index)->setCheckState(
                0, childState);
        }
        item->setCheckState(0, childState);
    } else {
        QTreeWidgetItem* fileItem = item->parent();
        int checked = 0;
        for (int index = 0;
             index < fileItem->childCount();
             ++index) {
            if (fileItem->child(index)->checkState(0)
                == Qt::Checked) {
                ++checked;
            }
        }
        const Qt::CheckState fileState =
            checked == 0
            ? Qt::Unchecked
            : (checked == fileItem->childCount()
               ? Qt::Checked
               : Qt::PartiallyChecked);
        fileItem->setCheckState(0, fileState);
    }
    updatingReplaceChecklist = false;
    invalidateReplacePreview(
        QStringLiteral(
            "Selection changed; build the preview again."));
}

void ScopedSearchPanel::invalidateReplacePreview(
    const QString& message)
{
    if (replaceWorkflow)
        replaceWorkflow->discardPendingPreview();
    currentReplacePreview = ReplacePreviewPlan{};
    if (replaceDiffView)
        replaceDiffView->clear();
    if (replaceStatusLabel && !message.isEmpty())
        replaceStatusLabel->setText(message);
    updateReplaceButtons();
}

ReplacePreviewRequest
ScopedSearchPanel::replaceRequestFromChecklist() const
{
    ReplacePreviewRequest request;
    request.replacementText = replacementText();
    request.filesSelectedByDefault = false;
    request.matchesSelectedByDefault = false;

    for (int fileIndex = 0;
         fileIndex < replaceTree->topLevelItemCount();
         ++fileIndex) {
        QTreeWidgetItem* fileItem =
            replaceTree->topLevelItem(fileIndex);
        const QString fileIdentity =
            fileItem->data(
                0, kFileIdentityRole).toString();
        const bool fileSelected =
            fileItem->checkState(0) != Qt::Unchecked;
        if (!fileIdentity.isEmpty()) {
            request.fileSelectionByIdentity.insert(
                fileIdentity, fileSelected);
        }
        for (int matchIndex = 0;
             matchIndex < fileItem->childCount();
             ++matchIndex) {
            QTreeWidgetItem* matchItem =
                fileItem->child(matchIndex);
            const QString matchIdentity =
                matchItem->data(
                    0, kMatchIdentityRole).toString();
            if (!matchIdentity.isEmpty()) {
                request.matchSelectionByIdentity.insert(
                    matchIdentity,
                    matchItem->checkState(0)
                        == Qt::Checked);
            }
        }
    }
    return request;
}

void ScopedSearchPanel::updateWorkflowUi(
    const ScopedReplaceWorkflowResult& result)
{
    if (replaceStatusLabel && !result.message.isEmpty())
        replaceStatusLabel->setText(result.message);
    if (replaceDiffView) {
        if (!result.renderedDiff.isEmpty())
            replaceDiffView->setPlainText(result.renderedDiff);
        else if (result.state
                 == ScopedReplaceWorkflowState::Cancelled
                 || result.state
                 == ScopedReplaceWorkflowState::Undone) {
            replaceDiffView->clear();
        }
    }
    if (applyReplaceButton) {
        applyReplaceButton->setText(
            dryRunCheck && dryRunCheck->isChecked()
                ? QStringLiteral("Confirm Dry Run")
                : QStringLiteral("Apply Confirmed Diff"));
    }
    updateReplaceButtons();
}

void ScopedSearchPanel::updateReplaceButtons()
{
    const bool pending =
        replaceWorkflow
        && replaceWorkflow->hasPendingPreview();
    if (applyReplaceButton)
        applyReplaceButton->setEnabled(pending);
    if (cancelReplaceButton)
        cancelReplaceButton->setEnabled(pending);
    if (undoReplaceButton) {
        undoReplaceButton->setEnabled(
            replaceWorkflow
            && replaceWorkflow
                   ->canUndoAppliedTransaction());
    }
}

ScopedSearchPanelCoordinator::
ScopedSearchPanelCoordinator(
    QWidget* dockParent,
    const SearchService* service,
    QObject* parent)
    : QObject(parent ? parent : dockParent)
{
    searchDock = new QDockWidget(
        QStringLiteral("Search / Replace"),
        dockParent);
    searchDock->setObjectName(
        QStringLiteral("scopedSearchDock"));
    searchDock->setAttribute(Qt::WA_DeleteOnClose, false);
    searchDock->setFeatures(
        QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable
        | QDockWidget::DockWidgetClosable);

    searchPanel =
        new ScopedSearchPanel(service, searchDock);
    searchDock->setWidget(searchPanel);

    connect(searchPanel.data(),
            &ScopedSearchPanel::navigationRequested,
            this,
            [this](const QString& fileName,
                   int line,
                   int column) {
                emit navigationRequested(
                    fileName, line, column);
                if (navigationHandler) {
                    navigationHandler(
                        fileName, line, column);
                }
            });
    connect(searchPanel.data(),
            &ScopedSearchPanel::temporaryEditorOpenRequested,
            this,
            [this](const EditorLocation& location) {
                requestTemporaryEditorOpen(location);
            });
    connect(searchPanel.data(),
            &ScopedSearchPanel::replacePreviewReady,
            this,
            &ScopedSearchPanelCoordinator::
                replacePreviewReady);
}

QString ScopedSearchPanelCoordinator::panelId()
{
    return QStringLiteral("scopedSearch");
}

QDockWidget* ScopedSearchPanelCoordinator::dock() const
{
    return searchDock.data();
}

ScopedSearchPanel*
ScopedSearchPanelCoordinator::panel() const
{
    return searchPanel.data();
}

void ScopedSearchPanelCoordinator::setContextProvider(
    ScopedSearchPanel::ContextProvider provider)
{
    if (searchPanel)
        searchPanel->setContextProvider(
            std::move(provider));
}

void ScopedSearchPanelCoordinator::setSearchContext(
    const ScopedSearchPanelContext& context)
{
    if (searchPanel)
        searchPanel->setSearchContext(context);
}

void ScopedSearchPanelCoordinator::setNavigationHandler(
    NavigationHandler handler)
{
    navigationHandler = std::move(handler);
}

void ScopedSearchPanelCoordinator::
    setRegisteredActionRequestHandler(
        RegisteredActionRequestHandler handler)
{
    registeredActionRequestHandler =
        std::move(handler);
}

ActionExecutionResult
ScopedSearchPanelCoordinator::requestTemporaryEditorOpen(
    const EditorLocation& location)
{
    ActionExecutionResult result;
    result.handled = true;
    emit temporaryEditorOpenRequested(location);
    if (!location.isValid()) {
        result.failureReason = QStringLiteral(
            "The selected Search result has no valid source location.");
    } else if (!registeredActionRequestHandler) {
        result.failureReason = QStringLiteral(
            "The temporary-editor Action is unavailable.");
    } else {
        result = registeredActionRequestHandler(
            QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen),
            editorLocationActionParameters(location));
    }
    emit temporaryEditorOpenFinished(
        location,
        result.succeeded,
        result.failureReason.isEmpty()
            ? result.message
            : result.failureReason);
    return result;
}

void ScopedSearchPanelCoordinator::setReplaceWorkflow(
    ScopedReplaceWorkflow* workflow)
{
    if (searchPanel)
        searchPanel->setReplaceWorkflow(workflow);
}

void ScopedSearchPanelCoordinator::refresh()
{
    if (searchPanel)
        searchPanel->refresh();
}

ReplacePreviewPlan
ScopedSearchPanelCoordinator::buildReplacePreview()
{
    return searchPanel
        ? searchPanel->buildReplacePreview()
        : ReplacePreviewPlan{};
}
