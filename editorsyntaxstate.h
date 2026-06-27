#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include <QString>

#include <memory>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;
struct TSPortAppendTarget;
struct TSSignalInsertTarget;
struct TSInstanceInsertTarget;
struct TSAssignInsertTarget;
struct TSParameterInsertTarget;
struct TSModuleEndInsertTarget;
struct TSAlwaysScopeTarget;
struct TSModuleScopeTarget;

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
    TSPortAppendTarget portAppendTargetAt(int charPos) const;
    TSSignalInsertTarget signalInsertTargetAt(int charPos) const;
    TSInstanceInsertTarget instanceInsertTargetAt(int charPos) const;
    TSAssignInsertTarget assignInsertTargetAt(int charPos) const;
    TSParameterInsertTarget parameterInsertTargetAt(int charPos) const;
    TSModuleEndInsertTarget moduleEndInsertTargetAt(int charPos) const;
    TSAlwaysScopeTarget alwaysScopeTargetAt(int cursorChar,
                                            int selectionStartChar = -1,
                                            int selectionEndChar = -1) const;
    TSModuleScopeTarget moduleScopeTargetAt(int cursorChar,
                                            int selectionStartChar = -1,
                                            int selectionEndChar = -1) const;
    const TSDocument* tsDocument() const;

private:
    std::unique_ptr<TSDocument> document;
    MyHighlighter* highlighter = nullptr;
    bool interactiveSyntaxEnabled = true;
};

#endif // EDITORSYNTAXSTATE_H
