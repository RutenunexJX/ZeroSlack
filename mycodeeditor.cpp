#include "mycodeeditor.h"
#include "editorappearance.h"
#include "editorcompletionui.h"
#include "editorcompletionworkflow.h"
#include "editorcursornavigation.h"
#include "editorfileidentity.h"
#include "editorgeometry.h"
#include "editorgutter.h"
#include "editorselection.h"
#include "editorsourcenavigation.h"
#include "editorsemanticruntime.h"
#include "editorsyntaxstate.h"
#include "editorsemanticcontextservice.h"
#include "editormodestate.h"
#include "sourcenavigationservice.h"

#include "syminfo.h"

#include <QScrollBar>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QRect>
#include <QWheelEvent>
#include <QWidget>

#include <memory>

struct MyCodeEditorState
{
    EditorAppearance appearance;

    EditorGutter gutter;

    EditorDocumentGeometry geometry;
    EditorCursorNavigation cursorNavigation;

    EditorSyntaxState syntax;

    EditorFileIdentity identity;

    EditorSemanticRuntime semantic;
    EditorModeState modes;

    EditorCompletionUi completion;
    EditorCompletionWorkflow completionWorkflow;

    EditorHighlightRefresh highlightRefresh;

    EditorSourceNavigationUi sourceNavigation;

    EditorSelection selections;

    void initializeCore(MyCodeEditor* editor)
    {
        semantic.init();
        syntax.init();
        gutter.init(editor);
        identity.set(QString());
        editor->setMouseTracking(true);
    }

    void shutdown()
    {
        gutter.destroy();
    }

    void attachEditorConnections(MyCodeEditor* editor)
    {
        highlightRefresh.attachToEditor(editor, [this, editor]() {
            refreshScopeAndCurrentLineHighlight(editor);
        });
        QObject::connect(
            editor,
            &QPlainTextEdit::blockCountChanged,
            editor,
            [this, editor]() {
                gutter.updateViewportMargins(editor);
            });
        QObject::connect(
            editor,
            &QPlainTextEdit::updateRequest,
            editor,
            [this, editor](const QRect& rect, int dy) {
                gutter.handleUpdateRequest(editor, rect, dy);
            });
    }

    void attachToEditor(MyCodeEditor* editor)
    {
        initializeCore(editor);
        attachEditorConnections(editor);
        appearance.apply(editor);
        syntax.attachToEditor(editor);
        completionWorkflow.bind(
            editor,
            &completion,
            &modes,
            &selections,
            [this, editor](int cursorPosition, bool includeDocumentText) {
                return semanticContextForPosition(
                    editor,
                    cursorPosition,
                    includeDocumentText);
            },
            [this](int charPos) {
                return currentModuleNameAt(charPos);
            },
            [this]() {
                return semanticService();
            });
        completion.attachToEditor(
            editor,
            [this]() {
                completionWorkflow.handleAutoCompleteTimer();
            },
            [this](const QModelIndex& index) {
                completionWorkflow.handleCompletionActivated(index);
            },
            [this]() {
                completionWorkflow.handleTextChanged();
            });
        selections.highlightCurrentLine(editor);
        gutter.updateViewportMargins(editor);
    }

    EditorSemanticContextService* semanticService() const
    {
        return semantic.contextService();
    }

    EditorSourceContextProvider sourceContextProvider(
        const MyCodeEditor* editor) const
    {
        return [this, editor](int cursorPosition, bool includeDocumentText) {
            return semanticContextForPosition(
                editor,
                cursorPosition,
                includeDocumentText);
        };
    }

    QString currentModuleNameAt(int charPos) const
    {
        return syntax.moduleNameAt(charPos);
    }

    QString currentModuleName(const MyCodeEditor* editor) const
    {
        return currentModuleNameAt(editor->textCursor().position());
    }

    EditorSemanticContext semanticContextForPosition(
        const MyCodeEditor* editor,
        int cursorPosition,
        bool includeDocumentText) const
    {
        const int semanticPosition = cursorPosition >= 0
            ? cursorPosition
            : editor->textCursor().position();

        return semantic.contextForDocument(
            editor->document(),
            identity.current(),
            currentModuleNameAt(semanticPosition),
            semanticPosition,
            includeDocumentText);
    }

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        sourceNavigation.handleControlKeyPress(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor),
            selections);
    }

    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        sourceNavigation.handleControlKeyRelease(editor, event, selections);
    }

    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        handleControlKeyPress(editor, event);

        if (sourceNavigation.handleSourceSymbolShortcut(
                editor,
                event,
                semanticService(),
                sourceContextProvider(editor))) {
            return true;
        }

        if (event->key() == Qt::Key_Shift) {
            event->ignore();
            return true;
        }

        if (modes.alternateModeActive) {
            completionWorkflow.handleAlternateModeKey(event);
            return true;
        }

        return completionWorkflow.handleCompletionPopupKey(event);
    }

    bool handleKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        handleControlKeyRelease(editor, event);
        return event->key() != Qt::Key_Shift && modes.alternateModeActive;
    }

    void handleResize(MyCodeEditor* editor) const
    {
        gutter.resizeTo(editor, editor->contentsRect());
    }

    void handleContextMenu(MyCodeEditor* editor, QContextMenuEvent *event)
    {
        sourceNavigation.handleContextMenu(
            editor,
            event,
            sourceContextProvider(editor));
    }

    bool handleMousePress(MyCodeEditor* editor, QMouseEvent *event)
    {
        return sourceNavigation.handleMousePress(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor));
    }

    void handleMouseMove(MyCodeEditor* editor, QMouseEvent *event)
    {
        sourceNavigation.handleMouseMove(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor),
            selections);
    }

    void handleLeaveEvent(MyCodeEditor* editor)
    {
        sourceNavigation.handleLeave(editor, selections);
    }

    void refreshScopeAndCurrentLineHighlight(MyCodeEditor* editor)
    {
        selections.highlightCurrentLine(editor);
    }

    void setAlternateModeEnabled(bool enabled)
    {
        modes.setAlternateModeEnabled(enabled);
    }

    void executeAlternateModeCommand(const QString& command)
    {
        completionWorkflow.executeAlternateModeCommand(command);
    }

    void setSemanticContextService(EditorSemanticContextService* service)
    {
        semantic.setService(service);
    }

    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const
    {
        return geometry.blockGeometry(editor, blockNumber);
    }

    qreal documentHeightPx(const MyCodeEditor* editor) const
    {
        return geometry.documentHeightPx(editor);
    }

    void setDocumentFileName(MyCodeEditor* editor, QString fileName)
    {
        if (!identity.set(fileName))
            return;

        emit editor->fileNameChanged(identity.current());
    }

    QString documentFileName() const
    {
        return identity.current();
    }

    void applyLineNavigationTarget(
        MyCodeEditor* editor,
        const SourceLineNavigationTarget& target) const
    {
        cursorNavigation.applyLineTarget(editor, target);
    }

};

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    state->attachToEditor(this);
}

MyCodeEditor::~MyCodeEditor()
{
    state->shutdown();
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    state->refreshScopeAndCurrentLineHighlight(this);
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->setAlternateModeEnabled(enabled);
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->setSemanticContextService(service);
}

EditorBlockGeometry MyCodeEditor::blockGeometry(int blockNumber) const
{
    return state->blockGeometry(this, blockNumber);
}

qreal MyCodeEditor::documentHeightPx() const
{
    return state->documentHeightPx(this);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->handleResize(this);
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    state->handleContextMenu(this, event);
}

EditorSemanticContext MyCodeEditor::editorSemanticContextForPosition(
    int cursorPosition,
    bool includeDocumentText) const
{
    return state->semanticContextForPosition(
        this,
        cursorPosition,
        includeDocumentText);
}

void MyCodeEditor::setDocumentFileName(QString fileName)
{
    state->setDocumentFileName(this, fileName);
}

QString MyCodeEditor::documentFileName() const
{
    return state->documentFileName();
}

QString MyCodeEditor::currentModuleName() const
{
    return state->currentModuleName(this);
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (state->handleKeyPress(this, event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MyCodeEditor::executeAlternateModeCommand(const QString& command)
{
    state->executeAlternateModeCommand(command);
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (state->handleKeyRelease(this, event))
        return;

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (state->handleMousePress(this, event))
        return;

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    state->handleMouseMove(this, event);

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->handleLeaveEvent(this);

    QPlainTextEdit::leaveEvent(event);
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    state->applyLineNavigationTarget(this, target);
}
