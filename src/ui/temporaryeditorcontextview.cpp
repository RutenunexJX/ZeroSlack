#include "temporaryeditorcontextview.h"

#include "mycodeeditor.h"
#include "temporaryeditorsession.h"

#include <QVBoxLayout>

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

bool TemporaryEditorContextView::goBack()
{
    if (!session || !session->goBack()) return false;
    attachEditor();
    return true;
}

bool TemporaryEditorContextView::goForward()
{
    if (!session || !session->goForward()) return false;
    attachEditor();
    return true;
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

void TemporaryEditorContextView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    contentHost = new QWidget(this);
    contentHost->setObjectName(
        QStringLiteral("temporaryEditorContextContent"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    root->addWidget(contentHost, 1);

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
                emit currentLocationChanged(location);
            });
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
