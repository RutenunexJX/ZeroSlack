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

#include <utility>

TemporaryEditorContextView::TemporaryEditorContextView(
    TabManager* tabManager,
    QWidget* parent)
    : QWidget(parent)
    , session(std::make_unique<TemporaryEditorSession>(
          tabManager, this))
{
    setObjectName(QStringLiteral("temporaryEditorContextView"));
    buildUi();
    session->setViewParent(contentHost);
    connectSession();
    updateNavigationButtons();
}

TemporaryEditorContextView::~TemporaryEditorContextView()
{
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
    searchProvider = std::move(provider);
}

bool TemporaryEditorContextView::openLocation(
    const EditorLocation& location)
{
    if (!session || !session->openLocation(location))
        return false;
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

    backToolButton = new QToolButton(searchBar);
    backToolButton->setObjectName(
        QStringLiteral("temporaryEditorContextBack"));
    backToolButton->setIcon(
        style()->standardIcon(QStyle::SP_ArrowBack));
    backToolButton->setToolTip(tr("Back"));
    searchLayout->addWidget(backToolButton);

    forwardToolButton = new QToolButton(searchBar);
    forwardToolButton->setObjectName(
        QStringLiteral("temporaryEditorContextForward"));
    forwardToolButton->setIcon(
        style()->standardIcon(QStyle::SP_ArrowForward));
    forwardToolButton->setToolTip(tr("Forward"));
    searchLayout->addWidget(forwardToolButton);

    searchEdit = new QLineEdit(searchBar);
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
    searchPopup->attachSearchField(searchEdit);
    searchPopup->setActivationHandler(
        [this](const EditorSearchCandidate& candidate) {
            if (openLocation(candidate.location)) {
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
            [this](const QString& query) {
                const EditorSearchCandidates candidates =
                    searchProvider && !query.trimmed().isEmpty()
                    ? searchProvider(query)
                    : EditorSearchCandidates{};
                searchPopup->setCandidates(candidates, query);
            });
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
