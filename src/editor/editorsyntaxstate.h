#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include "documentchange.h"

#include <QList>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <memory>
#include <functional>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;
class TSUTF16Text;
class QTimer;
struct EditorDocumentSyntax;
struct TSChangedRange;
struct TSPortAppendTarget;
struct TSSignalInsertTarget;
struct TSParameterInsertTarget;
struct TSModuleEndNavigationTarget;
struct TSAlwaysScopeTarget;
struct TSModuleScopeTarget;
struct TSBeginEndInsideTarget;
struct TSStructuralNewlineTarget;
struct TSKeywordCompletionTarget;
struct TSKeywordPairTarget;
struct TSIdentifierTarget;
struct TSExpressionAtomTarget;
struct TSCompletionContextTarget;
struct TSIdentifierOccurrenceSet;
struct TSAssignmentNavigationTarget;
struct TSConditionalBranchNavigationTarget;
struct TSStructuralNavigationTarget;
enum class TSStructuralNavigationDirection;
struct EditorLargeFileSyntaxSnapshot {
    int startPosition = 0;
    int endPosition = 0;
    int startLine = 0;
    int documentLength = 0;
    int syntaxTextLength = 0;
    bool fullDocumentSyntax = false;
    std::uint64_t fullBuildCount = 0;
    std::uint64_t incrementalEditCount = 0;
    std::uintptr_t syntaxIdentity = 0;
    int sharedViewCount = 0;
    // Raw ranges returned by Tree-sitter for the most recent parse. These do
    // not include the local repaint range synthesized when the tree shape is
    // unchanged.
    int lastChangedRangeCount = 0;
    int lastChangedCharacterCount = 0;

    bool valid() const
    {
        return fullDocumentSyntax
            && startPosition == 0
            && startLine == 0
            && endPosition == syntaxTextLength
            && documentLength == syntaxTextLength;
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
    void createHighlighter(QTextDocument* textDocument, std::uint64_t revision = 0);
    void detachHighlighter();
    void attachToEditor(MyCodeEditor* editor);
    const TSUTF16Text& text() const;
    const DocumentChange& lastChange() const;
    QPair<int, int> oldChangedLineBounds() const;
    const QList<TSChangedRange>& changedRanges() const;
    std::uint64_t revision() const;
    void setRevision(std::uint64_t revision);
    void setDeferredParsing(bool deferred);
    void flushPendingEdits();
    void setReparseFinishedCallback(std::function<void()> callback);
    QString moduleNameAt(int charPos) const;
    TSPortAppendTarget portAppendTargetAt(int charPos) const;
    TSSignalInsertTarget signalInsertTargetAt(int charPos) const;
    TSSignalInsertTarget blockSignalInsertTargetAt(
        int charPos) const;
    TSParameterInsertTarget parameterInsertTargetAt(int charPos) const;
    TSModuleEndNavigationTarget moduleEndNavigationTargetAt(
        int charPos) const;
    TSAlwaysScopeTarget alwaysScopeTargetAt(
        int cursorChar,
        int selectionStartChar,
        int selectionEndChar) const;
    TSModuleScopeTarget moduleScopeTargetAt(
        int cursorChar,
        int selectionStartChar,
        int selectionEndChar) const;
    TSBeginEndInsideTarget beginEndInsideTargetAt(int cursorChar) const;
    TSStructuralNewlineTarget structuralNewlineTargetAt(
        int cursorChar,
        int indentWidth = 4) const;
    TSKeywordCompletionTarget uniqueKeywordCompletionAt(
        int cursorChar,
        int minimumPrefixLength = 3) const;
    TSKeywordPairTarget matchingKeywordPairAt(int cursorChar) const;
    TSIdentifierTarget identifierAt(int cursorChar) const;
    TSExpressionAtomTarget expressionAtomAt(int cursorChar) const;
    TSCompletionContextTarget completionContextAt(int cursorChar) const;
    TSIdentifierOccurrenceSet identifierOccurrencesAt(
        int cursorChar) const;
    TSAssignmentNavigationTarget assignmentNavigationTargetAt(
        int cursorChar,
        bool previous) const;
    TSConditionalBranchNavigationTarget
    conditionalBranchNavigationTargetAt(
        int cursorChar,
        bool previous) const;
    TSStructuralNavigationTarget structuralNavigationTargetAt(
        int cursorChar,
        TSStructuralNavigationDirection direction) const;
    TSInstantiationTarget instantiationAt(int cursorChar) const;
    TSUndefinedSignalContext undefinedSignalContextAt(
        int cursorChar) const;
    const TSDocument* tsDocument() const;
    EditorLargeFileSyntaxSnapshot largeFileSnapshotForTest() const;
    bool isLargeDocument() const;
    static int liveDocumentCountForTest();

private:
    friend struct EditorDocumentSyntax;
    std::shared_ptr<EditorDocumentSyntax> shared;
    std::function<void()> reparseFinished;
};

#endif // EDITORSYNTAXSTATE_H
