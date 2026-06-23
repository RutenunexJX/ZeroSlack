#ifndef EDITORSOURCENAVIGATION_H
#define EDITORSOURCENAVIGATION_H

#include "editorsemanticcontextservice.h"
#include "editorhoverpopup.h"
#include "editorsourcehover.h"

#include <functional>
#include <memory>

class EditorSelection;
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
        const EditorSourceContextProvider& contextProvider);
    void handleMouseMove(
        MyCodeEditor* editor,
        QMouseEvent* event,
        EditorSemanticContextService* service,
        const EditorSourceContextProvider& contextProvider,
        EditorSelection& selections);
    void handleLeave(MyCodeEditor* editor, EditorSelection& selections);
    bool handleEscape(MyCodeEditor* editor, EditorSelection& selections);
    void handleEditorContentChanged(MyCodeEditor* editor,
                                    EditorSelection& selections);
    void handleEditorScrolled(MyCodeEditor* editor,
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
    EditorHoverPopup* ensurePopup(MyCodeEditor* editor);
    bool popupMatches(const EditorSourceNavigationTarget& target,
                      bool previewMode) const;
    bool numericPopupMatches(int startPosition, int endPosition) const;

    EditorSourceHover sourceHover;
    std::unique_ptr<EditorHoverPopup> popup;
    bool popupNumericMode = false;
    bool popupPreviewMode = false;
    int popupStartPos = -1;
    int popupEndPos = -1;
};

#endif // EDITORSOURCENAVIGATION_H
