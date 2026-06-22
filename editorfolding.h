#ifndef EDITORFOLDING_H
#define EDITORFOLDING_H

#include "foldblockshelfmodel.h"
#include "tsdocument.h"

#include <QList>
#include <QPoint>
#include <QSet>
#include <QString>

class MyCodeEditor;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QPainter;
class QRect;

class EditorFoldingController
{
public:
    void refresh(MyCodeEditor* editor, const TSDocument* document);
    bool toggleFoldAtLine(MyCodeEditor* editor, int line);
    void startFoldRegionMarkMode(MyCodeEditor* editor);
    void cancelFoldRegionMarkMode(MyCodeEditor* editor);
    bool foldRegionMarkModeActive() const;
    void startFoldShelfMode(MyCodeEditor* editor);
    void cancelFoldShelfMode(MyCodeEditor* editor);
    bool foldShelfModeActive() const;
    bool handleFoldShelfMousePress(MyCodeEditor* editor, QMouseEvent* event);
    bool handleFoldShelfMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    void handleFoldShelfHover(MyCodeEditor* editor, QMouseEvent* event);
    bool handleFoldRegionHoverLine(MyCodeEditor* editor, int line);
    bool handleFoldRegionMouseMove(MyCodeEditor* editor, QMouseEvent* event);
    bool handleFoldShelfDragEnter(MyCodeEditor* editor, QDragEnterEvent* event) const;
    bool handleFoldShelfDragMove(MyCodeEditor* editor, QDragMoveEvent* event) const;
    bool handleFoldShelfDrop(MyCodeEditor* editor, QDropEvent* event);
    bool insertShelfItemAtLine(MyCodeEditor* editor,
                               const FoldShelfItem& item,
                               int line);
    bool handleFoldRegionGutterLine(MyCodeEditor* editor, int line);
    bool insertCustomFoldMarkers(MyCodeEditor* editor,
                                 int startLine,
                                 int endLine,
                                 const QString& alias = QString());
    FoldShelfItem foldShelfItemAtLine(MyCodeEditor* editor,
                                      int line,
                                      FoldShelfOriginKind origin) const;
    bool deleteCustomFoldAtLine(MyCodeEditor* editor, int line);
    bool hasFoldAtLine(int line) const;
    bool isCollapsedAtLine(int line) const;
    TSFoldRange foldAtLine(int line) const;
    void paintGutter(MyCodeEditor* editor, QPainter& painter, const QRect& rect) const;
    void paintPlaceholders(MyCodeEditor* editor, QPainter& painter) const;

private:
    enum class FoldRegionMarkMode {
        Inactive,
        WaitingForStart,
        WaitingForEnd
    };

    QList<TSFoldRange> ranges;
    QSet<int> collapsedStartLines;
    FoldRegionMarkMode markMode = FoldRegionMarkMode::Inactive;
    int pendingStartLine = -1;
    int foldRegionHoverLine = -1;
    int defaultAliasCounter = 0;
    bool shelfMode = false;
    TSFoldRange hoveredShelfRange;
    TSFoldRange dragShelfRange;
    QPoint dragStartPosition;

    void applyVisibility(MyCodeEditor* editor);
    void updateStatus(MyCodeEditor* editor, const QString& message) const;
    QString defaultAlias();
    TSFoldRange customFoldContainingLine(int line) const;
    QString rangeText(MyCodeEditor* editor, const TSFoldRange& range) const;
    bool deleteRange(MyCodeEditor* editor, const TSFoldRange& range);
    void paintCustomFoldBackgrounds(MyCodeEditor* editor, QPainter& painter) const;
    void paintFoldRegionPreview(MyCodeEditor* editor, QPainter& painter) const;
    void paintFoldShelfHighlight(MyCodeEditor* editor, QPainter& painter) const;
};

#endif // EDITORFOLDING_H
