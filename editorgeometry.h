#ifndef EDITORGEOMETRY_H
#define EDITORGEOMETRY_H

#include <QtTypes>

class MyCodeEditor;
struct EditorBlockGeometry;

struct EditorCodeLineTailGeometry {
    qreal textRight = 0;
    qreal top = 0;
    qreal height = 0;
    qreal baseline = 0;
    int textPosition = -1;
    bool valid = false;
};

class EditorDocumentGeometry
{
public:
    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const;
    EditorCodeLineTailGeometry codeLineTailGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const;
    qreal documentHeightPx(const MyCodeEditor* editor) const;
};

#endif // EDITORGEOMETRY_H
