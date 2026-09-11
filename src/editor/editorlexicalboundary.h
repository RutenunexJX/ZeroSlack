#ifndef EDITORLEXICALBOUNDARY_H
#define EDITORLEXICALBOUNDARY_H

#include <QString>

namespace EditorLexicalBoundary {

struct Range {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end > start; }
    int length() const { return end - start; }
};

Range unitAt(const QString& text, int position);
Range identifierAt(const QString& text, int position);
Range horizontalWhitespaceAt(const QString& text, int position);

int moveLeft(const QString& text, int position);
int moveRight(const QString& text, int position);
Range deleteBackward(const QString& text, int position);
Range deleteForward(const QString& text, int position);

} // namespace EditorLexicalBoundary

#endif // EDITORLEXICALBOUNDARY_H
