#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include <QString>

#include <memory>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;
enum class PackageToolKind;
struct TSPortAppendTarget;
struct TSSignalInsertTarget;
struct TSParameterInsertTarget;
struct TSPackageToolInsertTarget;
struct TSModuleEndNavigationTarget;
struct TSAlwaysScopeTarget;
struct TSModuleScopeTarget;
struct TSBeginEndInsideTarget;

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
    TSParameterInsertTarget parameterInsertTargetAt(int charPos) const;
    TSPackageToolInsertTarget packageToolInsertTargetAt(
        int charPos,
        PackageToolKind kind) const;
    TSModuleEndNavigationTarget moduleEndNavigationTargetAt(
        int charPos) const;
    TSAlwaysScopeTarget alwaysScopeTargetAt(int cursorChar,
                                            int selectionStartChar = -1,
                                            int selectionEndChar = -1) const;
    TSModuleScopeTarget moduleScopeTargetAt(int cursorChar,
                                            int selectionStartChar = -1,
                                            int selectionEndChar = -1) const;
    TSBeginEndInsideTarget beginEndInsideTargetAt(int cursorChar) const;
    const TSDocument* tsDocument() const;

private:
    std::unique_ptr<TSDocument> document;
    MyHighlighter* highlighter = nullptr;
    bool interactiveSyntaxEnabled = true;
};

#endif // EDITORSYNTAXSTATE_H
