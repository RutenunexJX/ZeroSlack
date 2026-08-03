#include "editorsyntaxstate.h"

#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "tsdocument.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTextDocument>

namespace {
constexpr int kLargeFileCharacters = 2 * 1024 * 1024;

struct SyntaxHighlighterRegistration {
    QList<EditorSyntaxState*> syntaxStates;
    EditorSyntaxState* owner = nullptr;
    QPointer<MyHighlighter> highlighter;
};

QHash<QTextDocument*, SyntaxHighlighterRegistration>&
syntaxHighlighterRegistry()
{
    static QHash<QTextDocument*, SyntaxHighlighterRegistration> registry;
    return registry;
}
}

EditorSyntaxState::EditorSyntaxState() = default;

EditorSyntaxState::~EditorSyntaxState()
{
    detachHighlighter();
}

void EditorSyntaxState::init()
{
    detachHighlighter();
    document = std::make_unique<TSDocument>();
    largeDocument = false;
    fullBuildCount = 0;
    incrementalEditCount = 0;
    lastChangedRangeCount = 0;
    lastChangedCharacterCount = 0;
}

QList<TSChangedRange> EditorSyntaxState::fullDocumentRange(
    const QString& text) const
{
    TSChangedRange range;
    range.endChar = text.size();
    range.endLine = text.count(QLatin1Char('\n'));
    return {range};
}

void EditorSyntaxState::recordChangedRanges(
    const QList<TSChangedRange>& ranges)
{
    lastChangedRangeCount = ranges.size();
    lastChangedCharacterCount = 0;
    for (const TSChangedRange& range : ranges) {
        lastChangedCharacterCount +=
            qMax(0, range.endChar - range.startChar);
    }
}

void EditorSyntaxState::syncText(const QString& text)
{
    document->setText(text);
    largeDocument =
        text.size() > kLargeFileCharacters;
    ++fullBuildCount;
    lastChangedRangeCount = 0;
    lastChangedCharacterCount = 0;
}

void EditorSyntaxState::createHighlighter(QTextDocument* textDocument)
{
    detachHighlighter();
    if (!textDocument || !document)
        return;

    auto& registry = syntaxHighlighterRegistry();
    auto registrationIt = registry.find(textDocument);
    const bool inserted = registrationIt == registry.end();
    if (inserted) {
        registrationIt = registry.insert(
            textDocument,
            SyntaxHighlighterRegistration{});
    }
    SyntaxHighlighterRegistration& registration =
        registrationIt.value();
    if (inserted) {
        QObject::connect(
            textDocument,
            &QObject::destroyed,
            [](QObject* destroyedDocument) {
                syntaxHighlighterRegistry().remove(
                    static_cast<QTextDocument*>(destroyedDocument));
            });
    }

    registration.syntaxStates.append(this);
    highlighterDocument = textDocument;
    if (!registration.highlighter) {
        registration.owner = this;
        registration.highlighter =
            new MyHighlighter(textDocument, document.get());
    }
}

void EditorSyntaxState::detachHighlighter()
{
    QTextDocument* textDocument = highlighterDocument.data();
    highlighterDocument.clear();
    if (!textDocument)
        return;

    auto& registry = syntaxHighlighterRegistry();
    auto registrationIt = registry.find(textDocument);
    if (registrationIt == registry.end())
        return;

    SyntaxHighlighterRegistration& registration =
        registrationIt.value();
    registration.syntaxStates.removeAll(this);
    if (registration.owner == this) {
        delete registration.highlighter.data();
        registration.highlighter.clear();
        registration.owner = nullptr;
    }

    if (registration.syntaxStates.isEmpty()) {
        delete registration.highlighter.data();
        registry.erase(registrationIt);
        return;
    }

    if (!registration.highlighter) {
        EditorSyntaxState* replacement =
            registration.syntaxStates.constFirst();
        registration.owner = replacement;
        registration.highlighter = new MyHighlighter(
            textDocument,
            replacement->document.get());
    }
}

void EditorSyntaxState::attachToEditor(MyCodeEditor* editor)
{
    createHighlighter(editor->document());
}

QList<TSChangedRange> EditorSyntaxState::applyDocumentChange(
    const DocumentChange& change,
    const TSUTF16Text& currentText)
{
    if (document->text().size() != change.oldLength
        || document->text().mid(change.position, change.removedLength)
               != change.removedText) {
        QString recoveredText;
        if (currentText.size() == change.newLength) {
            recoveredText = currentText.materialized();
        } else if (document->text().size() == change.oldLength
                   && change.position >= 0
                   && change.position + change.removedLength
                          <= document->text().size()) {
            recoveredText = document->text();
            recoveredText.replace(change.position,
                                  change.removedLength,
                                  change.insertedText);
        }
        if (recoveredText.size() != change.newLength)
            return {};

        syncText(recoveredText);
        const QList<TSChangedRange> ranges =
            fullDocumentRange(recoveredText);
        recordChangedRanges(ranges);
        return ranges;
    }

    QList<TSChangedRange> ranges = document->applyEdit(change);
    ++incrementalEditCount;
    largeDocument =
        document->text().size()
        > kLargeFileCharacters;
    recordChangedRanges(ranges);
    if (ranges.isEmpty()) {
        TSChangedRange range;
        range.startChar = change.position;
        range.endChar = change.newEnd();
        range.startLine = change.startLine;
        range.endLine = qMax(change.startLine, change.newEndLine);
        ranges.append(range);
    }
    return ranges;
}

QString EditorSyntaxState::moduleNameAt(int charPos) const
{
    return document->enclosingModuleName(charPos < 0 ? 0 : charPos);
}
QString EditorSyntaxState::packageNameAt(int charPos) const
{
    return document->enclosingPackageName(charPos < 0 ? 0 : charPos);
}


TSPortAppendTarget EditorSyntaxState::portAppendTargetAt(int charPos) const
{
    return document->portAppendTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget EditorSyntaxState::signalInsertTargetAt(int charPos) const
{
    return document->signalInsertTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget
EditorSyntaxState::blockSignalInsertTargetAt(int charPos) const
{
    return document->blockSignalInsertTarget(
        charPos < 0 ? 0 : charPos);
}

TSParameterInsertTarget EditorSyntaxState::parameterInsertTargetAt(
    int charPos) const
{
    return document->parameterInsertTarget(charPos < 0 ? 0 : charPos);
}

TSPackageToolInsertTarget EditorSyntaxState::packageToolInsertTargetAt(
    int charPos,
    PackageToolKind kind) const
{
    return document->packageToolInsertTarget(charPos < 0 ? 0 : charPos, kind);
}

TSModuleEndNavigationTarget EditorSyntaxState::moduleEndNavigationTargetAt(
    int charPos) const
{
    return document->moduleEndNavigationTarget(charPos < 0 ? 0 : charPos);
}

TSAlwaysScopeTarget EditorSyntaxState::alwaysScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    return document->alwaysScopeTarget(
        cursorChar < 0 ? 0 : cursorChar,
        selectionStartChar,
        selectionEndChar);
}

TSModuleScopeTarget EditorSyntaxState::moduleScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    return document->moduleScopeTarget(
        cursorChar < 0 ? 0 : cursorChar,
        selectionStartChar,
        selectionEndChar);
}

TSBeginEndInsideTarget EditorSyntaxState::beginEndInsideTargetAt(
    int cursorChar) const
{
    return document->beginEndInsideTarget(cursorChar < 0 ? 0 : cursorChar);
}

TSStructuralNewlineTarget
EditorSyntaxState::structuralNewlineTargetAt(
    int cursorChar,
    int indentWidth) const
{
    return document->structuralNewlineTarget(
        cursorChar < 0 ? 0 : cursorChar,
        indentWidth);
}

TSKeywordCompletionTarget
EditorSyntaxState::uniqueKeywordCompletionAt(
    int cursorChar,
    int minimumPrefixLength) const
{
    return document->uniqueKeywordCompletionAt(
        cursorChar < 0 ? 0 : cursorChar,
        minimumPrefixLength);
}

TSKeywordPairTarget
EditorSyntaxState::matchingKeywordPairAt(
    int cursorChar) const
{
    return document->matchingKeywordPairAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSIdentifierTarget EditorSyntaxState::identifierAt(int cursorChar) const
{
    return document->identifierAt(cursorChar < 0 ? 0 : cursorChar);
}

TSIdentifierOccurrenceSet
EditorSyntaxState::identifierOccurrencesAt(
    int cursorChar) const
{
    return document->identifierOccurrencesAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSAssignmentNavigationTarget
EditorSyntaxState::assignmentNavigationTargetAt(
    int cursorChar,
    bool previous) const
{
    return document->assignmentNavigationTarget(
        cursorChar < 0 ? 0 : cursorChar,
        previous);
}

TSConditionalBranchNavigationTarget
EditorSyntaxState::conditionalBranchNavigationTargetAt(
    int cursorChar,
    bool previous) const
{
    return document->conditionalBranchNavigationTarget(
        cursorChar < 0 ? 0 : cursorChar,
        previous);
}

TSInstantiationTarget EditorSyntaxState::instantiationAt(
    int cursorChar) const
{
    return document->instantiationAt(cursorChar < 0 ? 0 : cursorChar);
}

TSUndefinedSignalContext
EditorSyntaxState::undefinedSignalContextAt(int cursorChar) const
{
    return document->undefinedSignalContextAt(
        cursorChar < 0 ? 0 : cursorChar);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return document.get();
}

EditorLargeFileSyntaxSnapshot
EditorSyntaxState::largeFileSnapshotForTest() const
{
    EditorLargeFileSyntaxSnapshot snapshot;
    if (!largeDocument || !document)
        return snapshot;

    snapshot.endPosition = document->text().size();
    snapshot.documentLength = document->text().size();
    snapshot.syntaxTextLength = document->text().size();
    snapshot.fullDocumentSyntax = true;
    snapshot.fullBuildCount = fullBuildCount;
    snapshot.incrementalEditCount = incrementalEditCount;
    snapshot.lastChangedRangeCount = lastChangedRangeCount;
    snapshot.lastChangedCharacterCount = lastChangedCharacterCount;
    return snapshot;
}

bool EditorSyntaxState::isLargeDocument() const
{
    return largeDocument;
}
