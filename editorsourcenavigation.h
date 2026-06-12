#ifndef EDITORSOURCENAVIGATION_H
#define EDITORSOURCENAVIGATION_H

#include "editorsemanticcontextservice.h"
#include "editorsourcehover.h"

#include <functional>

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
    void applyHover(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target,
        EditorSelection& selections);
    void clearHover(MyCodeEditor* editor, EditorSelection& selections);

    EditorSourceHover sourceHover;
};

#endif // EDITORSOURCENAVIGATION_H
