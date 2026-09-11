#ifndef EDITORFOLDVIEWSTATE_H
#define EDITORFOLDVIEWSTATE_H

#include <QList>
#include <QTextCursor>

struct EditorFoldAnchorState {
    QTextCursor startAnchor;
    QTextCursor endAnchor;
    int fallbackStartLine = -1;
    int fallbackEndLine = -1;
};

struct EditorFoldViewState {
    QList<EditorFoldAnchorState> collapsedRanges;
};

#endif // EDITORFOLDVIEWSTATE_H
