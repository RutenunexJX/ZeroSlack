#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include "documentchange.h"

#include <QList>
#include <QString>

#include <memory>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;
struct TSChangedRange;
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
    QList<TSChangedRange> applyDocumentChange(
        const DocumentChange& change,
        const QString& currentText);
    QString moduleNameAt(int charPos) const;
    TSPortAppendTarget portAppendTargetAt(int charPos) const;
    TSSignalInsertTarget signalInsertTargetAt(int charPos) const;
    TSParameterInsertTarget parameterInsertTargetAt(int charPos) const;
    TSPackageToolInsertTarget packageToolInsertTargetAt(
        int charPos,
        PackageToolKind kind) const;
    TSModuleEndNavigationTarget moduleEndNavigationTargetAt(
        int charPos) const;
    TSAlwaysScopeTarget alwaysScopeTargetAt(
        int cursorChar,
        int selectionStartChar,
        int selectionEndChar,
        const QString& currentText,
        bool allowLargeFileScopeBuild) const;
    TSModuleScopeTarget moduleScopeTargetAt(
        int cursorChar,
        int selectionStartChar,
        int selectionEndChar,
        const QString& currentText,
        bool allowLargeFileScopeBuild) const;
    TSBeginEndInsideTarget beginEndInsideTargetAt(int cursorChar) const;
    const TSDocument* tsDocument() const;

private:
    std::unique_ptr<TSDocument> document;
    mutable std::unique_ptr<TSDocument> largeFileScopeDocument;
    MyHighlighter* highlighter = nullptr;
    bool interactiveSyntaxEnabled = true;
    mutable int largeFileScopeStartPosition = -1;
    mutable int largeFileScopeEndPosition = -1;
    mutable int largeFileScopeStartLine = 0;
    mutable int largeFileDocumentLength = 0;

    void invalidateLargeFileScope();
    void applyLargeFileScopeChange(const DocumentChange& change);
    bool ensureLargeFileScope(const QString& currentText,
                              int cursorChar,
                              bool allowBuild) const;
};

#endif // EDITORSYNTAXSTATE_H
