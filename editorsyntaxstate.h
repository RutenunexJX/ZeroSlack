#ifndef EDITORSYNTAXSTATE_H
#define EDITORSYNTAXSTATE_H

#include "documentchange.h"

#include <QList>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <memory>

class MyCodeEditor;
class MyHighlighter;
class QTextDocument;
class TSDocument;
class TSUTF16Text;
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
struct TSStructuralNewlineTarget;
struct TSKeywordCompletionTarget;
struct TSKeywordPairTarget;
struct TSIdentifierTarget;
struct TSIdentifierOccurrenceSet;
struct TSAssignmentNavigationTarget;
struct TSConditionalBranchNavigationTarget;
struct EditorLargeFileSyntaxSnapshot {
    int startPosition = 0;
    int endPosition = 0;
    int startLine = 0;
    int documentLength = 0;
    int syntaxTextLength = 0;
    bool fullDocumentSyntax = false;
    std::uint64_t fullBuildCount = 0;
    std::uint64_t incrementalEditCount = 0;
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
    void syncText(const QString& text);
    void createHighlighter(QTextDocument* textDocument);
    void detachHighlighter();
    void attachToEditor(MyCodeEditor* editor);
    QList<TSChangedRange> applyDocumentChange(
        const DocumentChange& change,
        const TSUTF16Text& currentText);
    QString moduleNameAt(int charPos) const;
    TSPortAppendTarget portAppendTargetAt(int charPos) const;
    TSSignalInsertTarget signalInsertTargetAt(int charPos) const;
    TSSignalInsertTarget blockSignalInsertTargetAt(
        int charPos) const;
    TSParameterInsertTarget parameterInsertTargetAt(int charPos) const;
    TSPackageToolInsertTarget packageToolInsertTargetAt(
        int charPos,
        PackageToolKind kind) const;
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
    TSIdentifierOccurrenceSet identifierOccurrencesAt(
        int cursorChar) const;
    TSAssignmentNavigationTarget assignmentNavigationTargetAt(
        int cursorChar,
        bool previous) const;
    TSConditionalBranchNavigationTarget
    conditionalBranchNavigationTargetAt(
        int cursorChar,
        bool previous) const;
    TSInstantiationTarget instantiationAt(int cursorChar) const;
    TSUndefinedSignalContext undefinedSignalContextAt(
        int cursorChar) const;
    const TSDocument* tsDocument() const;
    EditorLargeFileSyntaxSnapshot largeFileSnapshotForTest() const;
    bool isLargeDocument() const;

private:
    std::unique_ptr<TSDocument> document;
    QPointer<QTextDocument> highlighterDocument;
    bool largeDocument = false;
    std::uint64_t fullBuildCount = 0;
    std::uint64_t incrementalEditCount = 0;
    int lastChangedRangeCount = 0;
    int lastChangedCharacterCount = 0;

    QList<TSChangedRange> fullDocumentRange(
        const QString& text) const;
    void recordChangedRanges(
        const QList<TSChangedRange>& ranges);
};

#endif // EDITORSYNTAXSTATE_H
