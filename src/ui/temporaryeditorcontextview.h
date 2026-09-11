#ifndef TEMPORARYEDITORCONTEXTVIEW_H
#define TEMPORARYEDITORCONTEXTVIEW_H

#include "editorlocation.h"
#include "editorsearchcandidate.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class MyCodeEditor;
class QLineEdit;
class QResizeEvent;
class QShowEvent;
class QToolButton;
class QVBoxLayout;
class TabManager;
class TemporaryEditorSearchPopup;
class TemporaryEditorSession;

class ZEROSLACK_API TemporaryEditorContextView final : public QWidget
{
    Q_OBJECT

public:
    using SearchProvider =
        std::function<EditorSearchCandidates(const QString&)>;

    explicit TemporaryEditorContextView(
        TabManager* tabManager,
        QWidget* parent = nullptr);
    ~TemporaryEditorContextView() override;

    MyCodeEditor* editor() const;
    EditorLocation currentLocation() const;
    bool canGoBack() const;
    bool canGoForward() const;
    int historyCount() const;

    QLineEdit* searchField() const;
    QToolButton* backButton() const;
    QToolButton* forwardButton() const;

    void setSearchProvider(SearchProvider provider);
    bool openLocation(const EditorLocation& location);
    bool saveCurrent(bool forceSaveAs = false);
    QVariantMap saveState() const;

signals:
    void currentLocationChanged(const EditorLocation& location);
    void openFailed(const EditorLocation& location);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* contentHost = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    QLineEdit* searchEdit = nullptr;
    QToolButton* backToolButton = nullptr;
    QToolButton* forwardToolButton = nullptr;
    TemporaryEditorSearchPopup* searchPopup = nullptr;
    std::unique_ptr<TemporaryEditorSession> session;
    SearchProvider searchProvider;

    void buildUi();
    void connectSession();
    void attachEditor();
    void updateNavigationButtons();
};

#endif // TEMPORARYEDITORCONTEXTVIEW_H
