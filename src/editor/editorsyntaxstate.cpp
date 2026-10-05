#include "editorsyntaxstate.h"
#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "tsdocument.h"
#include <QHash>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

namespace {
constexpr int kLargeFileCharacters = 2 * 1024 * 1024;
}

// One owner per QTextDocument, independent of any editor's lifetime. Views
// only keep callbacks and local interaction/projection state.
struct EditorDocumentSyntax : std::enable_shared_from_this<EditorDocumentSyntax> {
    std::unique_ptr<TSDocument> document = std::make_unique<TSDocument>();
    std::unique_ptr<QTimer> continuation = std::make_unique<QTimer>();
    QPointer<QTextDocument> source;
    QPointer<MyHighlighter> highlighter;
    QList<EditorSyntaxState*> views;
    QSet<EditorSyntaxState*> deferredViews;
    DocumentChange change;
    QPair<int, int> oldLineBounds;
    QList<TSChangedRange> ranges;
    std::uint64_t revision = 0;
    std::uint64_t fullBuildCount = 0;
    std::uint64_t incrementalEditCount = 0;
    int lastChangedRangeCount = 0;
    int lastChangedCharacterCount = 0;

    EditorDocumentSyntax()
    {
        document->setSynchronousParseBudget(3000);
        continuation->setSingleShot(true);
    }

    void notifyReparsed()
    {
        const auto registered = views;
        if (highlighter)
            highlighter->requestDeferredRefresh();
        for (auto* view : registered) {
            if (!views.contains(view))
                continue;
            const auto callback = view->reparseFinished;
            if (callback)
                callback();
        }
    }

    void bind(QTextDocument* textDocument, std::uint64_t initialRevision)
    {
        source = textDocument;
        revision = initialRevision;
        document->setText(source->toPlainText());
        ++fullBuildCount;
        const auto weak = weak_from_this();
        QObject::connect(continuation.get(), &QTimer::timeout, continuation.get(), [weak] {
            const auto self = weak.lock();
            if (!self || !self->source || !self->document->hasPendingEdits())
                return;
            if (!self->document->finishPendingEdits(3000)) {
                self->continuation->start(1);
                return;
            }
            self->notifyReparsed();
        });
        QObject::connect(source, &QTextDocument::contentsChange, continuation.get(),
            [weak](int position, int removed, int added) {
                if (const auto self = weak.lock())
                    self->applyChange(position, removed, added);
            });
        // Register after the document-owned parser; every view observes the
        // same revision, and formatting cannot read a view's obsolete tree.
        highlighter = new MyHighlighter(source, document.get());
        if (document->hasPendingEdits())
            continuation->start(1);
    }

    void applyChange(int position, int removed, int added)
    {
        if (!source)
            return;
        const auto& oldText = document->text();
        DocumentChange next;
        next.oldLength = oldText.size();
        next.newLength = qMax(0, source->characterCount() - 1);
        next.position = qBound(0, position, next.oldLength);
        next.removedLength = qBound(0, removed, next.oldLength - next.position);
        next.removedText = oldText.mid(next.position, next.removedLength);
        int inserted = next.newLength - (next.oldLength - next.removedLength);
        if (inserted < 0 || next.position + inserted > next.newLength)
            inserted = qBound(0, added, next.newLength - qMin(next.position, next.newLength));
        QTextCursor cursor(source);
        cursor.setPosition(qMin(next.position, next.newLength));
        cursor.setPosition(qMin(next.position + inserted, next.newLength), QTextCursor::KeepAnchor);
        next.insertedText = cursor.selectedText()
            .replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
            .replace(QChar::LineSeparator, QLatin1Char('\n'));
        if (!next.changesText() && next.oldLength == next.newLength) {
            change = {};
            return;
        }
        const auto block = source->findBlock(qMin(next.position, next.newLength));
        next.startLine = block.isValid() ? block.blockNumber() : 0;
        next.startColumn = block.isValid() ? next.position - block.position() : next.position;
        next.oldEndLine = next.startLine + next.removedText.count(QLatin1Char('\n'));
        next.newEndLine = next.startLine + next.insertedText.count(QLatin1Char('\n'));
        next.lineDelta = next.newEndLine - next.oldEndLine;
        next.revision = ++revision;
        oldLineBounds.first = next.position <= 0 ? 0
            : oldText.lastIndexOf(QLatin1Char('\n'), next.position - 1) + 1;
        const int oldBreak = oldText.indexOf(QStringLiteral("\n"), next.oldEnd());
        oldLineBounds.second = oldBreak < 0 ? oldText.size() : oldBreak;
        continuation->stop();
        const bool defer = !deferredViews.isEmpty();
        ranges = document->applyEdit(next, defer);
        ++incrementalEditCount;
        change = next;
        lastChangedRangeCount = ranges.size();
        lastChangedCharacterCount = 0;
        for (const auto& range : ranges)
            lastChangedCharacterCount += qMax(0, range.endChar - range.startChar);
        if (ranges.isEmpty()) {
            TSChangedRange local;
            local.startChar = change.position;
            local.endChar = change.newEnd();
            local.startLine = change.startLine;
            local.endLine = qMax(change.startLine, change.newEndLine);
            ranges.append(local);
        }
        if (!defer && document->hasPendingEdits())
            continuation->start(25);
        if (highlighter)
            for (const auto& range : ranges)
                highlighter->requestDeferredRefresh(range.startChar, range.endChar);
    }
};

namespace {
QHash<QTextDocument*, std::shared_ptr<EditorDocumentSyntax>>& documentSyntaxRegistry()
{
    static QHash<QTextDocument*, std::shared_ptr<EditorDocumentSyntax>> registry;
    return registry;
}
}

EditorSyntaxState::EditorSyntaxState() : shared(std::make_shared<EditorDocumentSyntax>()) {}
EditorSyntaxState::~EditorSyntaxState() { detachHighlighter(); }

void EditorSyntaxState::init()
{
    detachHighlighter();
    shared = std::make_shared<EditorDocumentSyntax>();
}

void EditorSyntaxState::setReparseFinishedCallback(std::function<void()> callback)
{
    reparseFinished = std::move(callback);
}

void EditorSyntaxState::createHighlighter(QTextDocument* source, std::uint64_t revision)
{
    if (!source || (shared->source == source && shared->views.contains(this)))
        return;
    detachHighlighter();
    auto& registry = documentSyntaxRegistry();
    auto it = registry.find(source);
    if (it == registry.end()) {
        auto owner = std::make_shared<EditorDocumentSyntax>();
        it = registry.insert(source, owner);
        owner->bind(source, revision);
        QObject::connect(source, &QObject::destroyed, [source] {
            const auto owner = documentSyntaxRegistry().take(source);
            if (owner) {
                owner->continuation->stop();
                owner->source.clear();
                delete owner->highlighter.data();
            }
        });
    }
    shared = it.value();
    shared->views.append(this);
}

void EditorSyntaxState::detachHighlighter()
{
    if (!shared)
        return;
    shared->views.removeAll(this);
    shared->deferredViews.remove(this);
    if (shared->deferredViews.isEmpty() && shared->document->hasPendingEdits() && shared->source)
        shared->continuation->start(1);
}

void EditorSyntaxState::attachToEditor(MyCodeEditor* editor)
{
    createHighlighter(editor->document());
}

const TSUTF16Text& EditorSyntaxState::text() const { return shared->document->text(); }
const DocumentChange& EditorSyntaxState::lastChange() const { return shared->change; }
QPair<int, int> EditorSyntaxState::oldChangedLineBounds() const { return shared->oldLineBounds; }
const QList<TSChangedRange>& EditorSyntaxState::changedRanges() const { return shared->ranges; }
std::uint64_t EditorSyntaxState::revision() const { return shared->revision; }
void EditorSyntaxState::setRevision(std::uint64_t revision) { shared->revision = revision; }
void EditorSyntaxState::setDeferredParsing(bool deferred)
{
    if (deferred)
        shared->deferredViews.insert(this);
    else
        shared->deferredViews.remove(this);
}

void EditorSyntaxState::flushPendingEdits()
{
    const auto owner = shared;
    owner->continuation->stop();
    const bool pending = owner->document->hasPendingEdits();
    owner->document->flushPendingEdits();
    if (pending)
        owner->notifyReparsed();
}

QString EditorSyntaxState::moduleNameAt(int charPos) const
{
    return shared->document->enclosingModuleName(charPos < 0 ? 0 : charPos);
}
QString EditorSyntaxState::packageNameAt(int charPos) const
{
    return shared->document->enclosingPackageName(charPos < 0 ? 0 : charPos);
}


TSPortAppendTarget EditorSyntaxState::portAppendTargetAt(int charPos) const
{
    return shared->document->portAppendTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget EditorSyntaxState::signalInsertTargetAt(int charPos) const
{
    return shared->document->signalInsertTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget
EditorSyntaxState::blockSignalInsertTargetAt(int charPos) const
{
    return shared->document->blockSignalInsertTarget(
        charPos < 0 ? 0 : charPos);
}

TSParameterInsertTarget EditorSyntaxState::parameterInsertTargetAt(
    int charPos) const
{
    return shared->document->parameterInsertTarget(charPos < 0 ? 0 : charPos);
}

TSModuleEndNavigationTarget EditorSyntaxState::moduleEndNavigationTargetAt(
    int charPos) const
{
    return shared->document->moduleEndNavigationTarget(charPos < 0 ? 0 : charPos);
}

TSAlwaysScopeTarget EditorSyntaxState::alwaysScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    return shared->document->alwaysScopeTarget(
        cursorChar < 0 ? 0 : cursorChar,
        selectionStartChar,
        selectionEndChar);
}

TSModuleScopeTarget EditorSyntaxState::moduleScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    return shared->document->moduleScopeTarget(
        cursorChar < 0 ? 0 : cursorChar,
        selectionStartChar,
        selectionEndChar);
}

TSBeginEndInsideTarget EditorSyntaxState::beginEndInsideTargetAt(
    int cursorChar) const
{
    return shared->document->beginEndInsideTarget(cursorChar < 0 ? 0 : cursorChar);
}

TSStructuralNewlineTarget
EditorSyntaxState::structuralNewlineTargetAt(
    int cursorChar,
    int indentWidth) const
{
    return shared->document->structuralNewlineTarget(
        cursorChar < 0 ? 0 : cursorChar,
        indentWidth);
}

TSKeywordCompletionTarget
EditorSyntaxState::uniqueKeywordCompletionAt(
    int cursorChar,
    int minimumPrefixLength) const
{
    return shared->document->uniqueKeywordCompletionAt(
        cursorChar < 0 ? 0 : cursorChar,
        minimumPrefixLength);
}

TSKeywordPairTarget
EditorSyntaxState::matchingKeywordPairAt(
    int cursorChar) const
{
    return shared->document->matchingKeywordPairAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSIdentifierTarget EditorSyntaxState::identifierAt(int cursorChar) const
{
    return shared->document->identifierAt(cursorChar < 0 ? 0 : cursorChar);
}

TSExpressionAtomTarget EditorSyntaxState::expressionAtomAt(
    int cursorChar) const
{
    return shared->document->expressionAtomAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSCompletionContextTarget EditorSyntaxState::completionContextAt(
    int cursorChar) const
{
    return shared->document->completionContextAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSIdentifierOccurrenceSet
EditorSyntaxState::identifierOccurrencesAt(
    int cursorChar) const
{
    return shared->document->identifierOccurrencesAt(
        cursorChar < 0 ? 0 : cursorChar);
}

TSAssignmentNavigationTarget
EditorSyntaxState::assignmentNavigationTargetAt(
    int cursorChar,
    bool previous) const
{
    return shared->document->assignmentNavigationTarget(
        cursorChar < 0 ? 0 : cursorChar,
        previous);
}

TSConditionalBranchNavigationTarget
EditorSyntaxState::conditionalBranchNavigationTargetAt(
    int cursorChar,
    bool previous) const
{
    return shared->document->conditionalBranchNavigationTarget(
        cursorChar < 0 ? 0 : cursorChar,
        previous);
}

TSStructuralNavigationTarget
EditorSyntaxState::structuralNavigationTargetAt(
    int cursorChar,
    TSStructuralNavigationDirection direction) const
{
    return shared->document->structuralNavigationTarget(
        cursorChar < 0 ? 0 : cursorChar,
        direction);
}

TSInstantiationTarget EditorSyntaxState::instantiationAt(
    int cursorChar) const
{
    return shared->document->instantiationAt(cursorChar < 0 ? 0 : cursorChar);
}

TSUndefinedSignalContext
EditorSyntaxState::undefinedSignalContextAt(int cursorChar) const
{
    return shared->document->undefinedSignalContextAt(
        cursorChar < 0 ? 0 : cursorChar);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return shared->document.get();
}

EditorLargeFileSyntaxSnapshot
EditorSyntaxState::largeFileSnapshotForTest() const
{
    EditorLargeFileSyntaxSnapshot snapshot;
    snapshot.syntaxIdentity = reinterpret_cast<std::uintptr_t>(shared->document.get());
    snapshot.sharedViewCount = shared->views.size();
    snapshot.fullBuildCount = shared->fullBuildCount;
    snapshot.incrementalEditCount = shared->incrementalEditCount;
    if (!isLargeDocument())
        return snapshot;

    snapshot.endPosition = shared->document->text().size();
    snapshot.documentLength = shared->document->text().size();
    snapshot.syntaxTextLength = shared->document->text().size();
    snapshot.fullDocumentSyntax = true;
    snapshot.lastChangedRangeCount = shared->lastChangedRangeCount;
    snapshot.lastChangedCharacterCount = shared->lastChangedCharacterCount;
    return snapshot;
}

bool EditorSyntaxState::isLargeDocument() const
{
    return shared->document->text().size() > kLargeFileCharacters;
}

int EditorSyntaxState::liveDocumentCountForTest()
{
    return documentSyntaxRegistry().size();
}
