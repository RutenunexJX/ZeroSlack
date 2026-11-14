#ifndef TEMPORARYEDITORCONTEXTVIEW_H
#define TEMPORARYEDITORCONTEXTVIEW_H

#include "editorlocation.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

#include <memory>

class MyCodeEditor;
class QVBoxLayout;
class TabManager;
class TemporaryEditorSession;

class ZEROSLACK_API TemporaryEditorContextView final : public QWidget
{
    Q_OBJECT

public:

    explicit TemporaryEditorContextView(
        TabManager* tabManager,
        QWidget* parent = nullptr);
    ~TemporaryEditorContextView() override;

    MyCodeEditor* editor() const;
    EditorLocation currentLocation() const;
    bool canGoBack() const;
    bool canGoForward() const;
    int historyCount() const;

    bool goBack();
    bool goForward();

    bool openLocation(const EditorLocation& location);
    bool saveCurrent(bool forceSaveAs = false);
    QVariantMap saveState() const;

signals:
    void currentLocationChanged(const EditorLocation& location);
    void openFailed(const EditorLocation& location);

private:
    QWidget* contentHost = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    std::unique_ptr<TemporaryEditorSession> session;

    void buildUi();
    void connectSession();
    void attachEditor();
};

#endif // TEMPORARYEDITORCONTEXTVIEW_H
