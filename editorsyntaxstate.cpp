#include "editorsyntaxstate.h"

#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "tsdocument.h"

#include <QObject>
#include <QTextDocument>

namespace {
constexpr int kMaxInteractiveSyntaxCharacters = 2 * 1024 * 1024;
}

EditorSyntaxState::EditorSyntaxState() = default;

EditorSyntaxState::~EditorSyntaxState() = default;

void EditorSyntaxState::init()
{
    document = std::make_unique<TSDocument>();
}

void EditorSyntaxState::syncText(const QString& text)
{
    if (text.size() > kMaxInteractiveSyntaxCharacters) {
        interactiveSyntaxEnabled = false;
        document->setText(QString());
        return;
    }

    interactiveSyntaxEnabled = true;
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
            applyDocumentChange(
                position,
                charsRemoved,
                charsAdded,
                editor->document());
        });
    createHighlighter(editor->document());
}

void EditorSyntaxState::applyDocumentChange(
    int position,
    int charsRemoved,
    int charsAdded,
    QTextDocument* textDocument)
{
    if (!textDocument)
        return;

    if (textDocument->characterCount() > kMaxInteractiveSyntaxCharacters) {
        if (interactiveSyntaxEnabled)
            syncText(QString());
        interactiveSyntaxEnabled = false;
        return;
    }

    const QString text = textDocument->toPlainText();
    if (!interactiveSyntaxEnabled) {
        syncText(text);
        return;
    }

    applyEdit(position, charsRemoved, charsAdded, text);
}

void EditorSyntaxState::applyEdit(
    int position,
    int charsRemoved,
    int charsAdded,
    const QString& text)
{
    if (!interactiveSyntaxEnabled) {
        syncText(text);
        return;
    }

    document->applyEditChars(
        position,
        position + charsRemoved,
        position + charsAdded,
        text);
}

QString EditorSyntaxState::moduleNameAt(int charPos) const
{
    if (!interactiveSyntaxEnabled)
        return QString();
    return document->enclosingModuleName(charPos < 0 ? 0 : charPos);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return interactiveSyntaxEnabled ? document.get() : nullptr;
}
