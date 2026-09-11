#ifndef EDITORGEOMETRY_H
#define EDITORGEOMETRY_H

#include <QtTypes>
#include <QTextBlock>

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

enum class EditorVisualBoundary {
    Start,
    End
};

class EditorVisualColumnGeometry
{
public:
    static int tabStopColumns(const MyCodeEditor* editor);
    static int advanceForCharacter(QChar character,
                                   int visualColumn,
                                   int tabWidth);
    static qreal spaceAdvance(const MyCodeEditor* editor);
    static int visualColumnForOffset(const MyCodeEditor* editor,
                                     const QTextBlock& block,
                                     int offset);
    static int offsetForVisualColumn(const MyCodeEditor* editor,
                                     const QTextBlock& block,
                                     int visualColumn,
                                     EditorVisualBoundary boundary);
    static int viewportXForVisualColumn(const MyCodeEditor* editor,
                                        const QTextBlock& block,
                                        int visualColumn,
                                        EditorVisualBoundary boundary =
                                            EditorVisualBoundary::Start);
    static int visualColumnForViewportX(const MyCodeEditor* editor,
                                        const QTextBlock& block,
                                        qreal viewportX);
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
