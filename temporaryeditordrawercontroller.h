#ifndef TEMPORARYEDITORDRAWERCONTROLLER_H
#define TEMPORARYEDITORDRAWERCONTROLLER_H

#include "zeroslackexport.h"

#include "editorlocation.h"
#include "editorsearchcandidate.h"
#include "shareddocument.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class MyCodeEditor;
class QSettings;
class TabManager;
class TemporaryEditorDrawer;
class QWidget;

class ZEROSLACK_API TemporaryEditorDrawerController final : public QObject
{
    Q_OBJECT

public:
    using SearchProvider =
        std::function<EditorSearchCandidates(const QString&)>;

    explicit TemporaryEditorDrawerController(
        TabManager* tabManager,
        QWidget* editorRegion,
        QObject* parent = nullptr);
    TemporaryEditorDrawerController(
        TabManager* tabManager,
        QWidget* editorRegion,
        std::unique_ptr<QSettings> settings,
        QObject* parent = nullptr);
    ~TemporaryEditorDrawerController() override;

    TemporaryEditorDrawer* drawer() const;
    MyCodeEditor* editor() const;
    EditorLocation currentLocation() const;
    bool isOpen() const;
    bool canGoBack() const;
    bool canGoForward() const;
    int historyCount() const;
    int historyIndex() const;

    void setSearchProvider(SearchProvider provider);
    bool saveCurrent(bool forceSaveAs = false);

public slots:
    bool openLocation(const EditorLocation& location);
    void goBack();
    void goForward();
    void closeDrawer();

signals:
    void currentLocationChanged(const EditorLocation& location);
    void navigationAvailabilityChanged(bool canGoBack,
                                       bool canGoForward);
    void openFailed(const EditorLocation& location);
    void searchRequested(const QString& query);
    void pinnedChanged(bool pinned);
    void closed();

private:
    struct HistoryEntry {
        EditorLocation location;
        SharedDocumentViewState viewState;
        QTextCursor trackedCursor;
        bool hasViewState = false;
    };

    QPointer<TabManager> tabManagerValue;
    QPointer<TemporaryEditorDrawer> drawerValue;
    QPointer<MyCodeEditor> editorValue;
    std::unique_ptr<QSettings> settingsValue;
    SearchProvider searchProvider;
    QList<HistoryEntry> history;
    int currentHistoryIndex = -1;
    bool closing = false;

    void connectDrawer();
    void loadPreferences();
    void persistGeometryPreference();
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

#endif // TEMPORARYEDITORDRAWERCONTROLLER_H
