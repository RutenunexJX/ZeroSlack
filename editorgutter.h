#ifndef EDITORGUTTER_H
#define EDITORGUTTER_H

#include <QRect>

class MyCodeEditor;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;
class LineNumberWidget;

class EditorGutter
{
public:
    void init(MyCodeEditor* editor);
    void destroy();
    void handleUpdateRequest(MyCodeEditor* editor, const QRect& rect, int dy) const;
    void updateViewportMargins(MyCodeEditor* editor) const;
    void resizeTo(MyCodeEditor* editor, const QRect& contentsRect) const;
    void paint(MyCodeEditor* editor, QPaintEvent* event) const;
    void handleMousePress(MyCodeEditor* editor, QMouseEvent* event) const;
    void handleMouseMove(MyCodeEditor* editor, QMouseEvent* event) const;
    void handleWheel(MyCodeEditor* editor, QWheelEvent* event) const;

private:
    int widthFor(MyCodeEditor* editor) const;
    void refresh(const QRect& rect, int dy, int width) const;

    LineNumberWidget* widget = nullptr;
};

#endif // EDITORGUTTER_H
