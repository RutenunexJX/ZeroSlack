#include "editorsyntaxstate.h"

#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "tsdocument.h"

#include <QObject>
#include <QTextDocument>

namespace {
constexpr int kMaxInteractiveSyntaxCharacters = 2 * 1024 * 1024;
constexpr int kLargeFileScopeRadiusCharacters = 32 * 1024;
}

EditorSyntaxState::EditorSyntaxState() = default;

EditorSyntaxState::~EditorSyntaxState() = default;

void EditorSyntaxState::init()
{
    document = std::make_unique<TSDocument>();
    largeFileScopeDocument = std::make_unique<TSDocument>();
}

void EditorSyntaxState::invalidateLargeFileScope()
{
    if (largeFileScopeDocument)
        largeFileScopeDocument->setText(QString());
    largeFileScopeStartPosition = -1;
    largeFileScopeEndPosition = -1;
    largeFileScopeStartLine = 0;
    largeFileDocumentLength = 0;
}

void EditorSyntaxState::syncText(const QString& text)
{
    invalidateLargeFileScope();
    if (text.size() > kMaxInteractiveSyntaxCharacters) {
        interactiveSyntaxEnabled = false;
        document->setText(QString());
        return;
    }

    interactiveSyntaxEnabled = true;
    document->setText(text);
}

void EditorSyntaxState::createHighlighter(QTextDocument* textDocument)
{
    highlighter = new MyHighlighter(textDocument, document.get());
}

void EditorSyntaxState::attachToEditor(MyCodeEditor* editor)
{
    createHighlighter(editor->document());
}

QList<TSChangedRange> EditorSyntaxState::applyDocumentChange(
    const DocumentChange& change,
    const QString& currentText)
{
    if (change.newLength > kMaxInteractiveSyntaxCharacters) {
        if (interactiveSyntaxEnabled)
            syncText(QString());
        interactiveSyntaxEnabled = false;
        applyLargeFileScopeChange(change);
        return {};
    }

    if (!interactiveSyntaxEnabled) {
        syncText(currentText);
        TSChangedRange range;
        range.endChar = currentText.size();
        range.endLine = currentText.count(QLatin1Char('\n'));
        return {range};
    }

    if (document->text().size() != change.oldLength
        || document->text().mid(change.position, change.removedLength)
               != change.removedText) {
        syncText(currentText);
        TSChangedRange range;
        range.endChar = currentText.size();
        range.endLine = currentText.count(QLatin1Char('\n'));
        return {range};
    }

    QList<TSChangedRange> ranges = document->applyEdit(change);
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

void EditorSyntaxState::applyLargeFileScopeChange(
    const DocumentChange& change)
{
    if (!largeFileScopeDocument || largeFileScopeStartPosition < 0)
        return;
    if (change.oldLength != largeFileDocumentLength) {
        invalidateLargeFileScope();
        return;
    }

    if (change.oldEnd() <= largeFileScopeStartPosition) {
        largeFileScopeStartPosition += change.characterDelta();
        largeFileScopeEndPosition += change.characterDelta();
        largeFileScopeStartLine += change.lineDelta;
        largeFileDocumentLength = change.newLength;
        return;
    }
    if (change.position >= largeFileScopeEndPosition) {
        largeFileDocumentLength = change.newLength;
        return;
    }
    if (change.position < largeFileScopeStartPosition
        || change.oldEnd() > largeFileScopeEndPosition) {
        invalidateLargeFileScope();
        return;
    }

    DocumentChange localChange = change;
    localChange.position -= largeFileScopeStartPosition;
    localChange.oldLength = largeFileScopeEndPosition
        - largeFileScopeStartPosition;
    localChange.newLength = localChange.oldLength
        - localChange.removedLength + localChange.insertedText.size();
    localChange.startLine -= largeFileScopeStartLine;
    localChange.oldEndLine -= largeFileScopeStartLine;
    localChange.newEndLine -= largeFileScopeStartLine;
    largeFileScopeDocument->applyEdit(localChange);
    largeFileScopeEndPosition += change.characterDelta();
    largeFileDocumentLength = change.newLength;
}

bool EditorSyntaxState::ensureLargeFileScope(
    const QString& currentText,
    int currentTextLength,
    int cursorChar,
    bool allowBuild) const
{
    const int boundedCursor = qBound(0, cursorChar, currentTextLength);
    if (largeFileScopeDocument
        && largeFileScopeStartPosition >= 0
        && largeFileDocumentLength == currentTextLength
        && boundedCursor >= largeFileScopeStartPosition
        && boundedCursor < largeFileScopeEndPosition) {
        return true;
    }
    if (!allowBuild || !largeFileScopeDocument
        || currentText.size() != currentTextLength
        || currentText.isEmpty()) {
        return false;
    }

    int windowStart = qMax(0,
                           boundedCursor
                               - kLargeFileScopeRadiusCharacters);
    if (windowStart > 0) {
        const int precedingNewline =
            currentText.lastIndexOf(QLatin1Char('\n'), windowStart - 1);
        windowStart = precedingNewline >= 0 ? precedingNewline + 1 : 0;
    }
    int windowEnd = qMin(currentText.size(),
                         boundedCursor
                             + kLargeFileScopeRadiusCharacters);
    if (windowEnd < currentText.size()) {
        const int followingNewline =
            currentText.indexOf(QLatin1Char('\n'), windowEnd);
        windowEnd = followingNewline >= 0 ? followingNewline + 1
                                          : currentText.size();
    }
    if (windowEnd <= windowStart)
        return false;

    int startLine = 0;
    for (int index = 0; index < windowStart; ++index) {
        if (currentText.at(index) == QLatin1Char('\n'))
            ++startLine;
    }
    largeFileScopeDocument->setText(
        currentText.mid(windowStart, windowEnd - windowStart));
    largeFileScopeStartPosition = windowStart;
    largeFileScopeEndPosition = windowEnd;
    largeFileScopeStartLine = startLine;
    largeFileDocumentLength = currentText.size();
    return true;
}

QString EditorSyntaxState::moduleNameAt(int charPos) const
{
    if (!interactiveSyntaxEnabled)
        return QString();
    return document->enclosingModuleName(charPos < 0 ? 0 : charPos);
}
QString EditorSyntaxState::packageNameAt(int charPos) const
{
    if (!interactiveSyntaxEnabled)
        return QString();
    return document->enclosingPackageName(charPos < 0 ? 0 : charPos);
}


TSPortAppendTarget EditorSyntaxState::portAppendTargetAt(int charPos) const
{
    TSPortAppendTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->portAppendTarget(charPos < 0 ? 0 : charPos);
}

TSSignalInsertTarget EditorSyntaxState::signalInsertTargetAt(int charPos) const
{
    TSSignalInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->signalInsertTarget(charPos < 0 ? 0 : charPos);
}

TSParameterInsertTarget EditorSyntaxState::parameterInsertTargetAt(
    int charPos) const
{
    TSParameterInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->parameterInsertTarget(charPos < 0 ? 0 : charPos);
}

TSPackageToolInsertTarget EditorSyntaxState::packageToolInsertTargetAt(
    int charPos,
    PackageToolKind kind) const
{
    TSPackageToolInsertTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->packageToolInsertTarget(charPos < 0 ? 0 : charPos, kind);
}

TSModuleEndNavigationTarget EditorSyntaxState::moduleEndNavigationTargetAt(
    int charPos) const
{
    TSModuleEndNavigationTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->moduleEndNavigationTarget(charPos < 0 ? 0 : charPos);
}

TSAlwaysScopeTarget EditorSyntaxState::alwaysScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar,
    const QString& currentText,
    int currentTextLength,
    bool allowLargeFileScopeBuild) const
{
    TSAlwaysScopeTarget target;
    if (interactiveSyntaxEnabled) {
        return document->alwaysScopeTarget(cursorChar < 0 ? 0 : cursorChar,
                                           selectionStartChar,
                                           selectionEndChar);
    }
    if (!ensureLargeFileScope(currentText,
                              currentTextLength,
                              cursorChar,
                              allowLargeFileScopeBuild)) {
        return target;
    }

    const int relativeCursor = qMax(0,
                                    cursorChar
                                        - largeFileScopeStartPosition);
    const int relativeSelectionStart = selectionStartChar >= 0
        ? selectionStartChar - largeFileScopeStartPosition
        : -1;
    const int relativeSelectionEnd = selectionEndChar >= 0
        ? selectionEndChar - largeFileScopeStartPosition
        : -1;
    target = largeFileScopeDocument->alwaysScopeTarget(
        relativeCursor,
        relativeSelectionStart,
        relativeSelectionEnd);
    if (target.ok()) {
        target.startChar += largeFileScopeStartPosition;
        target.endChar += largeFileScopeStartPosition;
        target.startLine += largeFileScopeStartLine;
        target.endLine += largeFileScopeStartLine;
    }
    return target;
}

TSModuleScopeTarget EditorSyntaxState::moduleScopeTargetAt(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar,
    const QString& currentText,
    int currentTextLength,
    bool allowLargeFileScopeBuild) const
{
    TSModuleScopeTarget target;
    if (interactiveSyntaxEnabled) {
        return document->moduleScopeTarget(cursorChar < 0 ? 0 : cursorChar,
                                           selectionStartChar,
                                           selectionEndChar);
    }
    if (!ensureLargeFileScope(currentText,
                              currentTextLength,
                              cursorChar,
                              allowLargeFileScopeBuild)) {
        return target;
    }

    const int relativeCursor = qMax(0,
                                    cursorChar
                                        - largeFileScopeStartPosition);
    const int relativeSelectionStart = selectionStartChar >= 0
        ? selectionStartChar - largeFileScopeStartPosition
        : -1;
    const int relativeSelectionEnd = selectionEndChar >= 0
        ? selectionEndChar - largeFileScopeStartPosition
        : -1;
    target = largeFileScopeDocument->moduleScopeTarget(
        relativeCursor,
        relativeSelectionStart,
        relativeSelectionEnd);
    if (target.ok()) {
        target.startChar += largeFileScopeStartPosition;
        target.endChar += largeFileScopeStartPosition;
        target.startLine += largeFileScopeStartLine;
        target.endLine += largeFileScopeStartLine;
    }
    return target;
}

TSBeginEndInsideTarget EditorSyntaxState::beginEndInsideTargetAt(
    int cursorChar) const
{
    TSBeginEndInsideTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->beginEndInsideTarget(cursorChar < 0 ? 0 : cursorChar);
}

TSIdentifierTarget EditorSyntaxState::identifierAt(int cursorChar) const
{
    TSIdentifierTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->identifierAt(cursorChar < 0 ? 0 : cursorChar);
}

TSInstantiationTarget EditorSyntaxState::instantiationAt(
    int cursorChar) const
{
    TSInstantiationTarget target;
    if (!interactiveSyntaxEnabled)
        return target;
    return document->instantiationAt(cursorChar < 0 ? 0 : cursorChar);
}

TSUndefinedSignalContext
EditorSyntaxState::undefinedSignalContextAt(int cursorChar) const
{
    TSUndefinedSignalContext context;
    if (!interactiveSyntaxEnabled)
        return context;
    return document->undefinedSignalContextAt(
        cursorChar < 0 ? 0 : cursorChar);
}

const TSDocument* EditorSyntaxState::tsDocument() const
{
    return interactiveSyntaxEnabled ? document.get() : nullptr;
}

EditorLargeFileSyntaxScopeSnapshot
EditorSyntaxState::largeFileScopeSnapshotForTest() const
{
    EditorLargeFileSyntaxScopeSnapshot snapshot;
    if (!largeFileScopeDocument || largeFileScopeStartPosition < 0)
        return snapshot;

    snapshot.startPosition = largeFileScopeStartPosition;
    snapshot.endPosition = largeFileScopeEndPosition;
    snapshot.startLine = largeFileScopeStartLine;
    snapshot.documentLength = largeFileDocumentLength;
    snapshot.text = largeFileScopeDocument->text();
    return snapshot;
}

bool EditorSyntaxState::usesLargeFileScopedSyntax() const
{
    return !interactiveSyntaxEnabled;
}
