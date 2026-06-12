#ifndef EDITORGEOMETRY_H
#define EDITORGEOMETRY_H

#include <QtTypes>

class MyCodeEditor;
struct EditorBlockGeometry;

class EditorDocumentGeometry
{
public:
    EditorBlockGeometry blockGeometry(
        const MyCodeEditor* editor,
        int blockNumber) const;
    qreal documentHeightPx(const MyCodeEditor* editor) const;
};

#endif // EDITORGEOMETRY_H
