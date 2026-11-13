#pragma once
#include <QList>
#include <QHash>
#include <QMap>
#include <QRectF>
#include <QString>
#include <QTextBlock>

class MyCodeEditor;
class QTextDocument;
struct EditorBlockGap {
    QString id;
    int sourceLine = 0, row = 0, order = 0;
    qreal offset = 0, height = 0;
    bool hiddenAnchor = false;
};

// Sparse display segments for the association-enabled projection. Unvisited
// source rows have an estimated height; only demanded rows are laid out by Qt.
class EditorBlockProjection {
public:
    bool active() const { return enabled; }
    void ensure(MyCodeEditor* editor, const QList<QPair<int,int>>& folds);
    void invalidate() { dirty = true; }
    int rowForSource(int source) const;
    int sourceForRow(int row) const;
    int rowCount() const { return count; }
    qreal rowTop(int row) const;
    qreal rowHeight(int row) const;
    qreal height() const;
    int rowAtY(qreal y) const;
    qreal offset(MyCodeEditor* editor) const;
    void configureScrollBar(MyCodeEditor* editor) const;
    void measureVisible(MyCodeEditor* editor);
    void ensureCursorVisible(MyCodeEditor* editor, bool center) const;
    QList<QPair<QString,QRectF>> visibleGaps(MyCodeEditor* editor, int prefetch = 0) const;
    QRectF gapRect(MyCodeEditor* editor, const QString& id) const;
    QString adjacentBlock(int sourceLine, bool forward) const;
    QString neighboringBlock(const QString& id, bool forward) const;
    int sourceLineForBlock(const QString& id) const;
    int ordinalForBlock(const QString& id) const { return ordinals.value(id); }
    quint64 rowsMeasured() const { return measured; }
private:
    struct Run { int source, rows, firstRow; };
    struct Measured { qreal height; int revision; };
    QList<Run> runs;
    QList<EditorBlockGap> gaps;
    QHash<QString,int> ordinals;
    QMap<int,Measured> heights;
    struct NoteMeasure { quint64 revision = 0, style = 0; int width = 0; qreal dpr = 0, height = 0; };
    QHash<QString, NoteMeasure> noteHeights;
    quint64 styleRevision = 0;
    bool enabled = false, dirty = true;
    const QTextDocument* document = nullptr;
    quint64 modelRevision = 0, measured = 0;
    int sourceCount = 0, count = 0, width = 0;
    qreal estimate = 1;
    QString fontKey;
    QList<QPair<int,int>> collapsed;
};
