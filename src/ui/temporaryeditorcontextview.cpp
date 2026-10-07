#include "uicontrols.h"
#include "temporaryeditorcontextview.h"

#include "mycodeeditor.h"
#include "temporaryeditorsearchpopup.h"
#include "temporaryeditorsession.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHideEvent>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent>
#include <atomic>
#include <optional>

#include <utility>

struct TemporaryEditorContextView::AsyncQuery {
    struct Request { QString query; EditorSearchTask task; quint64 generation; };
    TemporaryEditorContextView* owner;
    QThreadPool pool;
    QFutureWatcher<EditorSearchCandidates>* watcher = nullptr;
    std::optional<Request> pending;
    std::shared_ptr<std::atomic_bool> cancelled;
    quint64 generation = 0;
    explicit AsyncQuery(TemporaryEditorContextView* owner) : owner(owner) {
        pool.setMaxThreadCount(1);
        pool.setThreadPriority(QThread::LowPriority);
    }
    ~AsyncQuery() {
        cancel();
        if (watcher) QObject::disconnect(watcher, nullptr, owner, nullptr);
        pool.waitForDone();
    }
    void cancel() {
        ++generation;
        if (cancelled) cancelled->store(true);
        pending.reset();
    }
    void submit(const QString& query, EditorSearchTask task) {
        cancel();
        pending = Request{query, std::move(task), generation};
        launch();
    }
    void launch() {
        if (watcher || !pending) return;
        auto request = std::move(*pending);
        pending.reset();
        auto token = std::make_shared<std::atomic_bool>(false);
        cancelled = token;
        auto* observed = new QFutureWatcher<EditorSearchCandidates>(owner);
        watcher = observed;
        QObject::connect(observed, &QFutureWatcher<EditorSearchCandidates>::finished, owner,
            [this, observed, token, query = request.query, requested = request.generation] {
                auto result = observed->future().takeResult();
                watcher = nullptr;
                observed->deleteLater();
                const QPointer<TemporaryEditorContextView> alive(owner);
                if (!token->load() && requested == generation && owner->isVisible()
                    && owner->searchEdit->text() == query) {
                    owner->searchPopup->setCandidates(result, query);
                    emit owner->searchResultsReady(query, result.size());
                }
                if (alive) launch();
            });
        observed->setFuture(QtConcurrent::run(&pool, [task = std::move(request.task), token] {
            return task && !token->load() ? task([token] { return token->load(); }) : EditorSearchCandidates{};
        }));
    }
};

TemporaryEditorContextView::TemporaryEditorContextView(
    TabManager* tabManager,
    QWidget* parent)
    : QWidget(parent)
    , session(std::make_unique<TemporaryEditorSession>(
          tabManager, this))
{
    setObjectName(QStringLiteral("temporaryEditorContextView"));
    asyncQuery = std::make_unique<AsyncQuery>(this);
    buildUi();
    session->setViewParent(contentHost);
    connectSession();
    updateNavigationButtons();
}

TemporaryEditorContextView::~TemporaryEditorContextView()
{
    asyncQuery.reset();
    if (session)
        session->close();
}

MyCodeEditor* TemporaryEditorContextView::editor() const
{
    return session ? session->editor() : nullptr;
}

EditorLocation TemporaryEditorContextView::currentLocation() const
{
    return session ? session->currentLocation() : EditorLocation{};
}

bool TemporaryEditorContextView::canGoBack() const
{
    return session && session->canGoBack();
}

bool TemporaryEditorContextView::canGoForward() const
{
    return session && session->canGoForward();
}

int TemporaryEditorContextView::historyCount() const
{
    return session ? session->historyCount() : 0;
}

QLineEdit* TemporaryEditorContextView::searchField() const
{
    return searchEdit;
}

QToolButton* TemporaryEditorContextView::backButton() const
{
    return backToolButton;
}

QToolButton* TemporaryEditorContextView::forwardButton() const
{
    return forwardToolButton;
}

void TemporaryEditorContextView::setSearchProvider(
    SearchProvider provider)
{
    asyncQuery->cancel();
    searchProvider = std::move(provider);
    if (searchPopup) searchPopup->clearCandidates();
}

void TemporaryEditorContextView::refreshSearchResults()
{
    if (!searchEdit || !searchPopup || !isVisible()) return;
    const QString query = searchEdit->text();
    searchPopup->clearCandidates();
    if (!searchProvider || query.trimmed().isEmpty()) { asyncQuery->cancel(); return; }
    asyncQuery->submit(query, searchProvider(query));
}

bool TemporaryEditorContextView::openLocation(
    const EditorLocation& location)
{
    if (!session || !session->openLocation(location))
        return false;
    asyncQuery->cancel();
    searchPopup->clearCandidates();
    attachEditor();
    return true;
}

bool TemporaryEditorContextView::saveCurrent(bool forceSaveAs)
{
    return session && session->saveCurrent(forceSaveAs);
}

QVariantMap TemporaryEditorContextView::saveState() const
{
    QVariantMap result;
    const EditorLocation location = currentLocation();
    if (location.isValid()) {
        result.insert(
            QStringLiteral("location"),
            editorLocationActionParameters(location));
    }
    return result;
}

void TemporaryEditorContextView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (searchPopup)
        searchPopup->synchronizeGeometry();
}

void TemporaryEditorContextView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (searchPopup)
        searchPopup->synchronizeGeometry();
    refreshSearchResults();
}

void TemporaryEditorContextView::hideEvent(QHideEvent* event)
{
    asyncQuery->cancel();
    searchPopup->clearCandidates();
    QWidget::hideEvent(event);
}

void TemporaryEditorContextView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* searchBar = new QWidget(this);
    searchBar->setObjectName(
        QStringLiteral("temporaryEditorContextSearchBar"));
    auto* searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(6, 4, 6, 4);
    searchLayout->setSpacing(4);

    backToolButton = UiControls::toolButton(searchBar);
    backToolButton->setObjectName(
        QStringLiteral("temporaryEditorContextBack"));
    backToolButton->setIcon(
        style()->standardIcon(QStyle::SP_ArrowBack));
    backToolButton->setToolTip(tr("Back"));
    searchLayout->addWidget(backToolButton);

    forwardToolButton = UiControls::toolButton(searchBar);
    forwardToolButton->setObjectName(
        QStringLiteral("temporaryEditorContextForward"));
    forwardToolButton->setIcon(
        style()->standardIcon(QStyle::SP_ArrowForward));
    forwardToolButton->setToolTip(tr("Forward"));
    searchLayout->addWidget(forwardToolButton);

    searchEdit = UiControls::lineEdit(searchBar);
    searchEdit->setObjectName(
        QStringLiteral("temporaryEditorContextSearch"));
    searchEdit->setPlaceholderText(
        tr("Open file or symbol"));
    searchEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(searchEdit, 1);
    root->addWidget(searchBar);

    contentHost = new QWidget(this);
    contentHost->setObjectName(
        QStringLiteral("temporaryEditorContextContent"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    root->addWidget(contentHost, 1);

    searchPopup = new TemporaryEditorSearchPopup(this);
    searchPopup->setCancellationHandler([this] { asyncQuery->cancel(); });
    searchPopup->attachSearchField(searchEdit);
    searchPopup->setActivationHandler(
        [this](const EditorSearchCandidate& candidate) {
            if (session && session->openSearchCandidate(candidate)) {
                asyncQuery->cancel();
                attachEditor();
                searchEdit->clear();
                searchPopup->clearCandidates();
            }
        });

    connect(backToolButton,
            &QToolButton::clicked,
            this,
            [this]() {
                if (session && session->goBack())
                    attachEditor();
            });
    connect(forwardToolButton,
            &QToolButton::clicked,
            this,
            [this]() {
                if (session && session->goForward())
                    attachEditor();
            });
    connect(searchEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString&) { refreshSearchResults(); });
}

void TemporaryEditorContextView::connectSession()
{
    connect(session.get(),
            &TemporaryEditorSession::editorChanged,
            this,
            [this](MyCodeEditor*) { attachEditor(); });
    connect(session.get(),
            &TemporaryEditorSession::currentLocationChanged,
            this,
            [this](const EditorLocation& location) {
                updateNavigationButtons();
                emit currentLocationChanged(location);
            });
    connect(session.get(),
            &TemporaryEditorSession::navigationAvailabilityChanged,
            this,
            [this](bool, bool) { updateNavigationButtons(); });
    connect(session.get(),
            &TemporaryEditorSession::openFailed,
            this,
            &TemporaryEditorContextView::openFailed);
}

void TemporaryEditorContextView::attachEditor()
{
    MyCodeEditor* currentEditor = editor();
    if (!currentEditor)
        return;
    if (contentLayout->indexOf(currentEditor) < 0)
        contentLayout->addWidget(currentEditor);
    currentEditor->show();
    currentEditor->setFocus();
}

void TemporaryEditorContextView::updateNavigationButtons()
{
    backToolButton->setEnabled(canGoBack());
    forwardToolButton->setEnabled(canGoForward());
}
