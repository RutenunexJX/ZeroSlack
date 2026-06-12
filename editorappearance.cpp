#include "editorappearance.h"

#include "mycodeeditor.h"

#include <QFont>
#include <QPlainTextEdit>

void EditorAppearance::apply(MyCodeEditor* editor) const
{
    editor->setFont(QFont("Consolas", 14));
    const int tabWidth =
        editor->fontMetrics().horizontalAdvance(' ') * 4;
    editor->setTabStopDistance(tabWidth);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
}
