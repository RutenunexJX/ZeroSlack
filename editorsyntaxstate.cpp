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

TSPortAppendTarget EditorSyntaxState::portAppendTargetAt(int charPos) const
{
    TSPortAppendTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->portAppendTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget EditorSyntaxState::signalInsertTargetAt(int charPos) const
{
    TSSignalInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->signalInsertTarget(charPos < 0 ? 0 : charPos);
}

TSInstanceInsertTarget EditorSyntaxState::instanceInsertTargetAt(int charPos) const
{
    TSInstanceInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->instanceInsertTarget(charPos < 0 ? 0 : charPos);
}

TSAssignInsertTarget EditorSyntaxState::assignInsertTargetAt(int charPos) const
{
    TSAssignInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->assignInsertTarget(charPos < 0 ? 0 : charPos);
}

TSParameterInsertTarget EditorSyntaxState::parameterInsertTargetAt(
    int charPos) const
{
    TSParameterInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->parameterInsertTarget(charPos < 0 ? 0 : charPos);
}

TSModuleEndInsertTarget EditorSyntaxState::moduleEndInsertTargetAt(
    int charPos) const
{
    TSModuleEndInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->moduleEndInsertTarget(charPos < 0 ? 0 : charPos);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return interactiveSyntaxEnabled ? document.get() : nullptr;
}
