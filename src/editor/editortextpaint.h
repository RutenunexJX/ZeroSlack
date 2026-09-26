#pragma once

#include <QAbstractTextDocumentLayout>

class MyCodeEditor;
class QPaintEvent;

namespace EditorTextPaint {
// Returns false before painting if the selection/typography needs Qt's general path.
bool paint(MyCodeEditor* editor, QPaintEvent* event,
           const QAbstractTextDocumentLayout::PaintContext& context);
}
