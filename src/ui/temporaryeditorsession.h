#ifndef TEMPORARYEDITORSESSION_H
#define TEMPORARYEDITORSESSION_H

#include "editorlocation.h"
#include "shareddocument.h"
#include "zeroslackexport.h"

#include <QList>
#include <QObject>
#include <QPointer>

class MyCodeEditor;
class TabManager;
class QWidget;

class ZEROSLACK_API TemporaryEditorSession final : public QObject
{
    Q_OBJECT

public:
    explicit TemporaryEditorSession(
        TabManager* tabManager,
        QObject* parent = nullptr);
    ~TemporaryEditorSession() override;

    MyCodeEditor* editor() const;
    EditorLocation currentLocation() const;
    bool isOpen() const;
    bool canGoBack() const;
    bool canGoForward() const;
    int historyCount() const;
    int historyIndex() const;

    void setViewParent(QWidget* parent);
    bool saveCurrent(bool forceSaveAs = false);
    bool openLocation(const EditorLocation& location);
    bool goBack();
    bool goForward();
    void close();

signals:
    void editorChanged(MyCodeEditor* editor);
    void currentLocationChanged(const EditorLocation& location);
    void navigationAvailabilityChanged(bool canGoBack,
                                       bool canGoForward);
    void openFailed(const EditorLocation& location);
    void closed();

private:
    struct HistoryEntry {
        EditorLocation location;
        SharedDocumentViewState viewState;
        QTextCursor trackedCursor;
        bool hasViewState = false;
    };

    QPointer<TabManager> tabManagerValue;
    QPointer<MyCodeEditor> editorValue;
    QPointer<QWidget> viewParentValue;
    QList<HistoryEntry> history;
    int currentHistoryIndex = -1;
    bool closing = false;

    void captureCurrentViewState();
    bool activateEntry(HistoryEntry* entry);
    bool ensureEditorFor(HistoryEntry* entry);
    void applyInitialLocation(const EditorLocation& location);
    void applyViewState(const SharedDocumentViewState& state,
                        const QTextCursor& trackedCursor);
    void handleAuxiliaryViewAboutToClose(MyCodeEditor* editor);
    void handleDocumentIdentityChanged(
        const QString& previousDocumentId,
        const QString& previousFileName,
        const QString& documentId,
        const QString& fileName);
    void normalizeLocation(HistoryEntry* entry) const;
    void updateNavigationAvailability();
};

#endif // TEMPORARYEDITORSESSION_H
