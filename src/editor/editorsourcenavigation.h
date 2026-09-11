#ifndef EDITORSOURCENAVIGATION_H
#define EDITORSOURCENAVIGATION_H

#include "editorsemanticcontextservice.h"
#include "editorhoverpopup.h"
#include "editorsourcehover.h"

#include <functional>
#include <memory>
#include <QPoint>

class EditorSelection;
class EditorModeController;
class MyCodeEditor;
class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPoint;

using EditorSourceContextProvider =
    std::function<EditorSemanticContext(int, bool)>;

class EditorSourceNavigationUi
{
public:
    ~EditorSourceNavigationUi();

    void bindModeController(EditorModeController* modes,
                            MyCodeEditor* editor,
                            EditorSelection* selections);
    void syncMode();
    bool handleSourceSymbolShortcut(
        MyCodeEditor* editor,
        QKeyEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider);
    void handleControlKeyPress(
        MyCodeEditor* editor,
        QKeyEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    void handleControlKeyRelease(
        MyCodeEditor* editor,
        QKeyEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    bool handleMousePress(
        MyCodeEditor* editor,
        QMouseEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    bool handleMouseDoubleClick(
        MyCodeEditor* editor,
        QMouseEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    bool handleMouseRelease(MyCodeEditor* editor, QMouseEvent* event);
    bool handleMouseMove(
        MyCodeEditor* editor,
        QMouseEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    void handleLeave(MyCodeEditor* editor, EditorSelection& selections);
    bool handleEscape(MyCodeEditor* editor, EditorSelection& selections);
    bool active() const;
    EditorHoverPopup* beginExternalPeek(MyCodeEditor* editor,
                                        bool interactive);
    EditorHoverPopup* currentPeek() const;
    void closeExternalPeek();
    void handleEditorContentChanged(MyCodeEditor* editor,
                                    EditorSelection& selections);
    void handleEditorScrolled(MyCodeEditor* editor,
                              EditorSelection& selections);
    void closeForEditor(MyCodeEditor* editor,
                        EditorSelection& selections);
    void shutdown();
    void handleContextMenu(
        MyCodeEditor* editor,
        QContextMenuEvent* event,
        const EditorSourceContextProvider& contextProvider);

private:
    EditorSourceNavigationTarget targetAtPosition(
        MyCodeEditor* editor,
        const QPoint& position,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider) const;
    bool requestNavigationAtPosition(
        MyCodeEditor* editor,
        const QPoint& position,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider) const;
    void refreshHoverAt(
        MyCodeEditor* editor,
        const QPoint& position,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    void refreshPopupAt(
        MyCodeEditor* editor,
        const QPoint& position,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        const EditorSourceNavigationTarget& target);
    void applyHover(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target,
        EditorSelection& selections);
    void clearHover(MyCodeEditor* editor, EditorSelection& selections);
    void closePopup();
    bool hasActiveHover() const;
    EditorHoverPopup* ensurePopup(MyCodeEditor* editor);
    bool popupMatches(const EditorSourceNavigationTarget& target,
                      bool previewMode) const;
    bool numericPopupMatches(int startPosition, int endPosition) const;
    bool popupSelectionStillActive(MyCodeEditor* editor) const;

    EditorSourceHover sourceHover;
    std::unique_ptr<EditorHoverPopup> popup;
    bool popupNumericMode = false;
    bool popupPreviewMode = false;
    bool popupPinnedBySelection = false;
    bool popupExternalMode = false;
    bool popupExternalInteractive = false;
    bool consumeNextNavigationRelease = false;
    bool hasLastMousePosition = false;
    QPoint lastMousePosition;
    int popupStartPos = -1;
    int popupEndPos = -1;
    EditorModeController* modeController = nullptr;
    MyCodeEditor* boundEditor = nullptr;
    EditorSelection* boundSelections = nullptr;
};

#endif // EDITORSOURCENAVIGATION_H
