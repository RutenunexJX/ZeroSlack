#include "editorkeywordghostcontroller.h"

#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "editorsyntaxstate.h"
#include "mycodeeditor.h"

#include <QKeyEvent>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>

namespace {
constexpr const char* kKeywordGhostSource =
    "editor.keywordGhost";

bool plainTab(const QKeyEvent* event)
{
    if (!event || event->key() != Qt::Key_Tab)
        return false;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    return modifiers == Qt::NoModifier;
}

bool navigationKey(int key)
{
    return key == Qt::Key_Left
        || key == Qt::Key_Right
        || key == Qt::Key_Up
        || key == Qt::Key_Down
        || key == Qt::Key_Home
        || key == Qt::Key_End
        || key == Qt::Key_PageUp
        || key == Qt::Key_PageDown;
}
}

void EditorKeywordGhostController::bind(
    EditorModeController* modes,
    AnnotationLayer* annotations,
    MyCodeEditor* editor)
{
    modeController = modes;
    annotationLayer = annotations;
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::KeywordGhost,
        [this, target](EditorModeExitReason) {
            clear(target, false);
        });
}

void EditorKeywordGhostController::shutdown(
    MyCodeEditor* editor)
{
    clear(editor);
    modeController = nullptr;
    annotationLayer = nullptr;
}

void EditorKeywordGhostController::refresh(
    MyCodeEditor* editor,
    const EditorSyntaxState& syntax)
{
    if (!editor
        || !modeController
        || !annotationLayer) {
        return;
    }
    if (editor->isReadOnly()) {
        clear(editor);
        return;
    }
    const EditorModeId primary =
        modeController->primaryMode();
    if (primary != EditorModeId::None
        && primary != EditorModeId::KeywordGhost) {
        clear(editor);
        return;
    }

    const QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()) {
        clear(editor);
        return;
    }
    const TSKeywordCompletionTarget next =
        syntax.uniqueKeywordCompletionAt(
            cursor.position(), 3);
    if (!next.ok()) {
        clear(editor);
        return;
    }

    currentTarget = next;
    if (!modeController->isActive(
            EditorModeId::KeywordGhost)) {
        modeController->enter(
            EditorModeId::KeywordGhost,
            EditorModeEntryReason::KeyboardGesture);
    }
    modeController->updatePresentation(
        EditorModeId::KeywordGhost,
        QStringLiteral("Keyword: %1")
            .arg(next.keyword),
        QStringLiteral(
            "Tab accepts; Esc or navigation cancels"));
    publish(editor);
}

void EditorKeywordGhostController::clear(
    MyCodeEditor* editor,
    bool exitMode)
{
    currentTarget = {};
    if (annotationLayer) {
        annotationLayer->removeSource(
            QString::fromLatin1(kKeywordGhostSource));
    }
    if (editor)
        editor->viewport()->update();
    if (exitMode
        && modeController
        && modeController->isActive(
            EditorModeId::KeywordGhost)) {
        modeController->exit(
            EditorModeId::KeywordGhost,
            EditorModeExitReason::Canceled);
    }
}

bool EditorKeywordGhostController::handleKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor
        || !event
        || !currentTarget.ok()
        || !modeController
        || !modeController->isActive(
            EditorModeId::KeywordGhost)) {
        return false;
    }
    if (editor->isReadOnly()) {
        const bool consume = plainTab(event);
        clear(editor);
        if (consume)
            event->accept();
        return consume;
    }

    if (plainTab(event)) {
        const QString suffix = currentTarget.suffix;
        clear(editor);
        QTextCursor cursor = editor->textCursor();
        cursor.beginEditBlock();
        cursor.insertText(suffix);
        cursor.endEditBlock();
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Escape)
        return false;
    if (navigationKey(event->key())
        || event->key() == Qt::Key_Backspace
        || event->key() == Qt::Key_Delete
        || !event->text().isEmpty()) {
        clear(editor);
    }
    return false;
}

TSKeywordCompletionTarget
EditorKeywordGhostController::targetForTest() const
{
    return currentTarget;
}

void EditorKeywordGhostController::publish(
    MyCodeEditor* editor)
{
    if (!editor
        || !annotationLayer
        || !currentTarget.ok()) {
        return;
    }
    const QTextBlock block =
        editor->document()->findBlock(
            currentTarget.endChar);
    if (!block.isValid())
        return;

    EditorAnnotation annotation;
    annotation.kind =
        EditorAnnotationKind::KeywordGhost;
    annotation.placement =
        EditorAnnotationPlacement::InlineAfter;
    annotation.range.startPosition =
        currentTarget.endChar;
    annotation.range.endPosition =
        currentTarget.endChar;
    annotation.range.firstLine =
        block.blockNumber();
    annotation.range.lastLine =
        block.blockNumber();
    annotation.text = currentTarget.suffix;
    annotation.detail = currentTarget.keyword;
    annotation.semanticKey =
        QStringLiteral("%1:%2")
            .arg(currentTarget.startChar)
            .arg(currentTarget.keyword);
    annotation.priority =
        AnnotationLayer::defaultPriority(
            annotation.kind);
    annotation.sourceGeneration =
        editor->semanticDocumentRevision();
    annotationLayer->setSourceAnnotations(
        QString::fromLatin1(kKeywordGhostSource),
        {annotation});
    editor->viewport()->update();
}
