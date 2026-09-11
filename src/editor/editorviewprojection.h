#ifndef EDITORVIEWPROJECTION_H
#define EDITORVIEWPROJECTION_H

#include <QList>
#include <QPair>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTextBlock>
#include <QTextCursor>
#include <QVector>

#include <cstdint>

class MyCodeEditor;
class QPaintEvent;
class QTextDocument;

struct EditorProjectionGeometry {
    qreal top = 0.0;
    qreal height = 0.0;
};

struct EditorViewProjectionMetrics {
    std::uint64_t rebuilds = 0;
    std::uint64_t sourceRowsVisitedDuringRebuild = 0;
    std::uint64_t rowsPainted = 0;
};

class EditorViewProjection
{
public:
    void invalidate();
    void ensure(MyCodeEditor* editor,
                const QList<QPair<int, int>>& collapsedRanges);

    bool active() const;
    bool sourceLineVisible(int sourceLine) const;
    int visibleRowForSourceLine(int sourceLine) const;
    int sourceLineForVisibleRow(int visibleRow) const;
    int visibleRowCount() const;
    QTextBlock nextVisibleBlock(MyCodeEditor* editor,
                                const QTextBlock& block) const;

    QTextBlock firstVisibleBlock(MyCodeEditor* editor) const;
    EditorProjectionGeometry blockGeometry(MyCodeEditor* editor,
                                           int sourceLine) const;
    QRectF blockBoundingGeometry(MyCodeEditor* editor,
                                 const QTextBlock& block) const;
    QRectF blockBoundingRect(MyCodeEditor* editor,
                             const QTextBlock& block) const;
    QPointF contentOffset(MyCodeEditor* editor) const;
    qreal documentHeight() const;

    QTextCursor cursorForPosition(MyCodeEditor* editor,
                                  const QPoint& position) const;
    QRect cursorRect(MyCodeEditor* editor,
                     const QTextCursor& cursor) const;
    void ensureCursorVisible(MyCodeEditor* editor, bool center) const;
    void paint(MyCodeEditor* editor, QPaintEvent* event);

    EditorViewProjectionMetrics metrics() const;

private:
    bool dirty = true;
    bool projectionActive = false;
    const QTextDocument* cachedDocument = nullptr;
    int cachedBlockCount = -1;
    int cachedViewportHeight = -1;
    int cachedViewportWidth = -1;
    qreal cachedRowHeight = -1.0;
    QList<QPair<int, int>> cachedCollapsedRanges;
    QVector<int> visibleSourceLines;
    QVector<int> sourceToVisibleRows;
    QVector<qreal> visibleRowTops;
    QVector<qreal> visibleRowHeights;
    qreal projectedDocumentHeight = 0.0;
    EditorViewProjectionMetrics metricValues;

    void rebuild(MyCodeEditor* editor,
                 const QList<QPair<int, int>>& collapsedRanges);
    void configureVerticalScrollBar(MyCodeEditor* editor) const;
    void restoreCanonicalVerticalScrollBar(MyCodeEditor* editor) const;
    int firstVisibleRow(MyCodeEditor* editor) const;
    int rowAtViewportY(MyCodeEditor* editor, int y) const;
};

#endif // EDITORVIEWPROJECTION_H
