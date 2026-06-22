#include "editorsyntaxstate.h"

#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "tsdocument.h"

#include <QObject>
#include <QTextDocument>

EditorSyntaxState::EditorSyntaxState() = default;

EditorSyntaxState::~EditorSyntaxState() = default;

void EditorSyntaxState::init()
{
    document = std::make_unique<TSDocument>();
}

void EditorSyntaxState::syncText(const QString& text)
{
    document->setText(text);
}

void EditorSyntaxState::createHighlighter(QTextDocument* textDocument)
{
    highlighter = new MyHighlighter(textDocument, document.get());
}

void EditorSyntaxState::attachToEditor(MyCodeEditor* editor)
{
    syncText(editor->document()->toPlainText());
    QObject::connect(
        editor->document(),
        &QTextDocument::contentsChange,
        editor,
        [this, editor](int position, int charsRemoved, int charsAdded) {
            applyEdit(
                position,
                charsRemoved,
                charsAdded,
                editor->document()->toPlainText());
        });
    createHighlighter(editor->document());
}

void EditorSyntaxState::applyEdit(
    int position,
    int charsRemoved,
    int charsAdded,
    const QString& text)
{
    document->applyEditChars(
        position,
        position + charsRemoved,
        position + charsAdded,
        text);
}

QString EditorSyntaxState::moduleNameAt(int charPos) const
{
    return document->enclosingModuleName(charPos < 0 ? 0 : charPos);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return document.get();
}
