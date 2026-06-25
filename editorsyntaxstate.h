#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include <QString>

#include <memory>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;

class EditorSyntaxState
{
public:
    EditorSyntaxState();
    ~EditorSyntaxState();

    void init();
    void syncText(const QString& text);
    void createHighlighter(QTextDocument* textDocument);
    void attachToEditor(MyCodeEditor* editor);
    void applyDocumentChange(int position,
                             int charsRemoved,
                             int charsAdded,
                             QTextDocument* textDocument);
    void applyEdit(int position,
                   int charsRemoved,
                   int charsAdded,
                   const QString& text);
    QString moduleNameAt(int charPos) const;
    const TSDocument* tsDocument() const;

private:
    std::unique_ptr<TSDocument> document;
    MyHighlighter* highlighter = nullptr;
    bool interactiveSyntaxEnabled = true;
};

#endif // EDITORSYNTAXSTATE_H
