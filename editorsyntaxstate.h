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
struct TSIdentifierTarget;
struct EditorLargeFileSyntaxScopeSnapshot {
    int startPosition = -1;
    int endPosition = -1;
    int startLine = 0;
    int documentLength = 0;
    QString text;

    bool valid() const
    {
        return startPosition >= 0
            && endPosition == startPosition + text.size()
            && documentLength >= endPosition;
    }
};

struct TSInstantiationTarget;
struct TSUndefinedSignalContext;

class EditorSyntaxState
{
public:
    EditorSyntaxState();
    ~EditorSyntaxState();

    void init();
    QString packageNameAt(int charPos) const;
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
        int currentTextLength,
        bool allowLargeFileScopeBuild) const;
    TSModuleScopeTarget moduleScopeTargetAt(
        int cursorChar,
        int selectionStartChar,
        int selectionEndChar,
        const QString& currentText,
        int currentTextLength,
        bool allowLargeFileScopeBuild) const;
    TSBeginEndInsideTarget beginEndInsideTargetAt(int cursorChar) const;
    TSIdentifierTarget identifierAt(int cursorChar) const;
    TSInstantiationTarget instantiationAt(int cursorChar) const;
    TSUndefinedSignalContext undefinedSignalContextAt(
        int cursorChar) const;
    const TSDocument* tsDocument() const;
    EditorLargeFileSyntaxScopeSnapshot largeFileScopeSnapshotForTest() const;
    bool usesLargeFileScopedSyntax() const;

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
                              int currentTextLength,
                              int cursorChar,
                              bool allowBuild) const;
};

#endif // EDITORSYNTAXSTATE_H
