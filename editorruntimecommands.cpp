#include "editorruntime.h"

#include "formattercursoranchor.h"
#include "formatterservice.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextCursor>

#include <utility>

void MyCodeEditorState::applyAppearanceSettings(
    MyCodeEditor* editor,
    const EditorAppearanceOptions& options)
{
    appearance.apply(editor, options);
    gutter.updateViewportMargins(editor);
    handleResize(editor);
}

EditorSemanticContextService* MyCodeEditorState::semanticService() const
{
    return semantic.contextService();
}

EditorSourceContextProvider MyCodeEditorState::sourceContextProvider(
    const MyCodeEditor* editor) const
{
    return [this, editor](int cursorPosition, bool includeDocumentText) {
        return semanticContextForPosition(
            editor,
            cursorPosition,
            includeDocumentText);
    };
}

QString MyCodeEditorState::currentModuleNameAt(int charPos) const
{
    return syntax.moduleNameAt(charPos);
}

QString MyCodeEditorState::currentModuleName(const MyCodeEditor* editor) const
{
    return currentModuleNameAt(editor->textCursor().position());
}

EditorAlwaysScopeTarget MyCodeEditorState::currentAlwaysScopeTarget(
    const MyCodeEditor* editor) const
{
    EditorAlwaysScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const TSAlwaysScopeTarget target =
        syntax.alwaysScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1);
    if (!target.ok()) {
        result.failureMessage =
            target.status == TSAlwaysScopeStatus::AmbiguousSelection
                ? QStringLiteral("Select only one always block to preview.")
                : QStringLiteral("Place the cursor in an always block to preview.");
        return result;
    }

    result.available = true;
    result.startPosition = target.startChar;
    result.endPosition = target.endChar;
    result.startLine = target.startLine;
    result.endLine = target.endLine;
    result.label = target.label;
    return result;
}

EditorModuleScopeTarget MyCodeEditorState::currentModuleScopeTarget(
    const MyCodeEditor* editor) const
{
    EditorModuleScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const TSModuleScopeTarget target =
        syntax.moduleScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1);
    if (!target.ok()) {
        result.failureMessage =
            target.status == TSModuleScopeStatus::AmbiguousSelection
                ? QStringLiteral("Select only one module to preview.")
                : QStringLiteral("Place the cursor in a module or always block to preview.");
        return result;
    }

    result.available = true;
    result.startPosition = target.startChar;
    result.endPosition = target.endChar;
    result.startLine = target.startLine;
    result.endLine = target.endLine;
    result.moduleName = target.moduleName;
    result.label = target.label;
    return result;
}

bool MyCodeEditorState::addPortRow(MyCodeEditor* editor,
                                   QString* message)
{
    if (!editor)
        return false;

    const TSPortAppendTarget target =
        syntax.portAppendTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSPortAppendStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear port append point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    if (target.needsTrailingComma
        && target.trailingCommaInsertChar >= 0) {
        cursor.setPosition(target.trailingCommaInsertChar);
        cursor.insertText(QStringLiteral(","));
    }
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

bool MyCodeEditorState::addSignalRow(MyCodeEditor* editor,
                                     QString* message)
{
    if (!editor)
        return false;

    const TSSignalInsertTarget target =
        syntax.signalInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSSignalInsertStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear signal insert point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

bool MyCodeEditorState::addParameterRow(MyCodeEditor* editor,
                                        QString* message)
{
    if (!editor)
        return false;

    const TSParameterInsertTarget target =
        syntax.parameterInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status
                    == TSParameterInsertStatus::NoCurrentParameterScope
                ? QStringLiteral("No current parameter scope")
                : QStringLiteral("No clear parameter insert point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    if (target.needsTrailingComma
        && target.trailingCommaInsertChar >= 0) {
        cursor.setPosition(target.trailingCommaInsertChar);
        cursor.insertText(QStringLiteral(","));
    }
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

namespace {
QString packageToolFailureMessage(TSPackageToolInsertStatus status)
{
    switch (status) {
    case TSPackageToolInsertStatus::NoCurrentPackage:
        return QStringLiteral("No current package");
    case TSPackageToolInsertStatus::InsideRtlScope:
        return QStringLiteral(
            "Package tools are unavailable inside module/interface scope");
    case TSPackageToolInsertStatus::PackageHasSyntaxError:
        return QStringLiteral("Current package has syntax errors");
    case TSPackageToolInsertStatus::NoEndpackage:
        return QStringLiteral("No endpackage found");
    case TSPackageToolInsertStatus::NoClearPackageInsertPoint:
        return QStringLiteral("No clear package insert point");
    case TSPackageToolInsertStatus::Ok:
        return QString();
    }
    return QStringLiteral("No clear package insert point");
}
} // namespace

EditorPackageToolAvailability MyCodeEditorState::currentPackageToolAvailability(
    const MyCodeEditor* editor) const
{
    EditorPackageToolAvailability availability;
    if (!editor)
        return availability;

    const TSPackageToolInsertTarget target =
        syntax.packageToolInsertTargetAt(editor->textCursor().position(),
                                         PackageToolKind::Parameter);
    availability.available = target.ok();
    availability.packageName = target.packageName;
    availability.failureMessage = packageToolFailureMessage(target.status);
    return availability;
}

bool MyCodeEditorState::executePackageToolInsert(MyCodeEditor* editor,
                                                 PackageToolKind kind,
                                                 QString* message)
{
    if (!editor)
        return false;

    const TSPackageToolInsertTarget target =
        syntax.packageToolInsertTargetAt(editor->textCursor().position(), kind);
    if (!target.ok()) {
        const QString failure = packageToolFailureMessage(target.status);
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    const PackageToolService service;
    const CodeTemplateItem packageTemplate =
        service.templateForInsertion(kind,
                                     target.lineIndent,
                                     target.insertAfterLine);
    if (packageTemplate.insertText.isEmpty()) {
        const QString failure = QStringLiteral("No package template available");
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    clearTemplateSlotMode(editor);
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(packageTemplate.insertText);
    cursor.endEditBlock();

    startTemplateSlotMode(editor,
                          target.insertChar,
                          packageTemplate.insertText.size(),
                          packageTemplate.templateSlots);

    const QString packageName =
        target.packageName.isEmpty()
            ? QStringLiteral("package")
            : QStringLiteral("package %1").arg(target.packageName);
    const QString success =
        QStringLiteral("Inserted %1 in %2")
            .arg(PackageToolService::labelForKind(kind), packageName);
    if (message)
        *message = success;
    emit editor->editorStatusMessageRequested(success);
    return true;
}

bool MyCodeEditorState::goToFinalEndmodule(MyCodeEditor* editor,
                                                  QString* message)
{
    if (!editor)
        return false;

    const TSModuleEndNavigationTarget target =
        syntax.moduleEndNavigationTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status
                    == TSModuleEndNavigationStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No endmodule found");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(target.caretChar);
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    if (message)
        *message = QStringLiteral("Moved to final endmodule");
    return true;
}
bool MyCodeEditorState::selectInsideBeginEnd(MyCodeEditor* editor,
                                             QString* message)
{
    if (!editor || !editor->document())
        return false;

    const TSBeginEndInsideTarget target =
        syntax.beginEndInsideTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status == TSBeginEndInsideStatus::EmptyBeginEndBlock
                    ? QStringLiteral("No begin-end body")
                    : QStringLiteral("No begin-end block");
        }
        return false;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(target.startChar);
    cursor.setPosition(target.endChar, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    if (message) {
        *message = QStringLiteral("Selected inside begin-end");
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Selected inside begin-end lines %1-%2")
            .arg(target.startLine + 1)
            .arg(target.endLine + 1));
    return true;
}

EditorSemanticContext MyCodeEditorState::semanticContextForPosition(
    const MyCodeEditor* editor,
    int cursorPosition,
    bool includeDocumentText) const
{
    const int semanticPosition = cursorPosition >= 0
        ? cursorPosition
        : editor->textCursor().position();

    EditorSemanticContext context = semantic.contextForDocument(
        editor->document(),
        identity.current(),
        currentModuleNameAt(semanticPosition),
        semanticPosition,
        false,
        semanticDocumentRevision());
    context.packageName = syntax.packageNameAt(semanticPosition);
    if (includeDocumentText)
        context.documentText = editor->cachedDocumentText();
    context.hierarchyInstance = hierarchyInstance;
    return context;
}

void MyCodeEditorState::handleControlKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    sourceNavigation.handleControlKeyPress(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
}

void MyCodeEditorState::handleControlKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    sourceNavigation.handleControlKeyRelease(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
}

bool MyCodeEditorState::handleKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    handleControlKeyRelease(editor, event);
    Q_UNUSED(event)
    return false;
}

bool MyCodeEditorState::executeClipboardAction(
    MyCodeEditor* editor,
    const QString& actionId,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!editor || !editor->document()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "No editor is available.");
        }
        return false;
    }

    const bool copy =
        actionId == QStringLiteral("edit.copy");
    const bool cut =
        actionId == QStringLiteral("edit.cut");
    const bool paste =
        actionId == QStringLiteral("edit.paste");
    if (!copy && !cut && !paste) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unsupported clipboard action.");
        }
        return false;
    }
    if ((cut || paste) && editor->isReadOnly()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The editor is read-only.");
        }
        return false;
    }

    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The system clipboard is unavailable.");
        }
        return false;
    }

    if (multiCursor.active()) {
        if (copy || cut) {
            const QString copied =
                multiCursor.copySelections();
            if (copied.isNull()) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "No multi-cursor selection is available.");
                }
                return false;
            }
            clipboard->setText(copied);
            if (cut)
                multiCursor.cutSelections();
        } else {
            multiCursor.pasteText(clipboard->text());
        }
        return true;
    }

    if (columnMode.selectionActive()) {
        const int key = copy
            ? Qt::Key_C
            : cut ? Qt::Key_X : Qt::Key_V;
        QKeyEvent event(
            QEvent::KeyPress,
            key,
            Qt::ControlModifier);
        if (columnMode.handleClipboard(
                editor, &event)) {
            return true;
        }
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The column selection could not consume the "
                "clipboard action.");
        }
        return false;
    }

    if (copy || cut) {
        const EditorLineOperationResult result =
            executeLineOperation(
                editor,
                copy ? EditorLineOperation::Copy
                     : EditorLineOperation::Cut);
        if (!result.succeeded
            && failureReason) {
            *failureReason = result.failureReason;
        }
        return result.succeeded;
    }

    editor->QPlainTextEdit::paste();
    return true;
}

EditorLineOperationResult
MyCodeEditorState::executeLineOperation(
    MyCodeEditor* editor,
    EditorLineOperation operation)
{
    EditorLineOperationResult result;
    if (!editor) {
        result.handled = true;
        result.failureReason =
            QStringLiteral("No editor is available.");
        return result;
    }
    const bool movesLines =
        operation == EditorLineOperation::MoveLinesUp
        || operation == EditorLineOperation::MoveLinesDown;
    if (movesLines && columnMode.selectionActive()) {
        result.handled = true;
        result.failureReason = QStringLiteral(
            "Column selection: moving logical lines is disabled.");
        emit editor->editorStatusMessageRequested(
            result.failureReason);
        return result;
    }
    const bool modifiesDocument =
        operation == EditorLineOperation::Cut
        || operation == EditorLineOperation::DeleteLines
        || operation
               == EditorLineOperation::JoinWithNextLine
        || movesLines;
    if (modifiesDocument && editor->isReadOnly()) {
        result.handled = true;
        result.failureReason =
            QStringLiteral("The editor is read-only.");
        emit editor->editorStatusMessageRequested(
            result.failureReason);
        return result;
    }

    QTextCursor cursor = editor->textCursor();
    result = lineOperations.execute(
        operation,
        cursor,
        QApplication::clipboard());
    const QTextCursor current =
        editor->textCursor();
    if (result.succeeded
        && (result.documentChanged
            || cursor.position() != current.position()
            || cursor.anchor() != current.anchor())) {
        editor->setTextCursor(cursor);
    }
    if (!result.succeeded
        && !result.failureReason.isEmpty()) {
        emit editor->editorStatusMessageRequested(
            result.failureReason);
    }
    return result;
}

bool MyCodeEditorState::selectSymbolOccurrences(
    MyCodeEditor* editor,
    bool allInScope,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!editor) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "No editor is available.");
        }
        return false;
    }

    const QTextCursor cursor =
        editor->textCursor();
    const int probe =
        cursor.hasSelection()
        ? cursor.selectionStart()
        : cursor.position();
    const TSIdentifierOccurrenceSet set =
        syntax.identifierOccurrencesAt(probe);
    if (!set.ok()) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "Place the cursor on a structural "
                    "SystemVerilog identifier.");
        }
        return false;
    }

    QList<EditorMultiCursorOccurrence> occurrences;
    occurrences.reserve(
        set.occurrences.size());
    for (const TSIdentifierTarget& occurrence :
         set.occurrences) {
        occurrences.append({
            occurrence.startChar,
            occurrence.endChar,
        });
    }
    if (!multiCursor.active())
        multiCursor.resetToEditorCursor();

    if (allInScope) {
        const int count =
            multiCursor.selectAllOccurrences(
                occurrences,
                set.scopeStartChar,
                set.scopeEndChar);
        if (count <= 0) {
            if (failureReason) {
                *failureReason =
                    QStringLiteral(
                        "No matching occurrence exists "
                        "in the current lexical scope.");
            }
            return false;
        }
        emit editor->editorStatusMessageRequested(
            QStringLiteral(
                "Selected %1 occurrences of %2")
                .arg(count)
                .arg(set.selected.text));
        return true;
    }

    const bool changed =
        multiCursor.addNextOccurrence(
            occurrences,
            set.scopeStartChar,
            set.scopeEndChar);
    if (!changed) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "All occurrences of %1 in the "
                    "current lexical scope are selected.")
                    .arg(set.selected.text);
        }
        return false;
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral(
            "Selected %1 occurrence(s) of %2")
            .arg(multiCursor.caretCount())
            .arg(set.selected.text));
    return true;
}

bool MyCodeEditorState::expandSmartSelection(
    MyCodeEditor* editor,
    QString* message)
{
    return selections.expandSmartSelection(
        editor, message);
}

bool MyCodeEditorState::navigateSelectedSymbolOccurrence(
    MyCodeEditor* editor,
    bool previous,
    QString* message)
{
    return selections.navigateSelectedSymbolOccurrence(
        editor, previous, message);
}


void MyCodeEditorState::startTemplateSlotMode(
    MyCodeEditor* editor,
    int insertionStart,
    int insertedLength,
    const CodeTemplateSlotList& slotMetadata)
{
    templateSlots.start(editor,
                        insertionStart,
                        insertedLength,
                        slotMetadata);
}

bool MyCodeEditorState::templateSlotModeActive() const
{
    return templateSlots.active();
}

int MyCodeEditorState::templateSlotModeActiveIndex() const
{
    return templateSlots.activeIndex();
}

int MyCodeEditorState::templateSlotModeSlotCount() const
{
    return templateSlots.slotCount();
}

bool MyCodeEditorState::templateSlotModeBlinkOn() const
{
    return templateSlots.blinkOn();
}

bool MyCodeEditorState::columnSelectionActiveForCommand() const
{
    return columnMode.selectionActive();
}

bool MyCodeEditorState::virtualCursorActiveForTest() const
{
    return columnMode.virtualCursorActive();
}

int MyCodeEditorState::virtualCursorLineForTest() const
{
    return columnMode.virtualCursorLine();
}

int MyCodeEditorState::virtualCursorColumnForTest() const
{
    return columnMode.virtualCursorColumn();
}

void MyCodeEditorState::clearVirtualCursor(
    MyCodeEditor* editor)
{
    columnMode.clearVirtualCursor(editor);
}

void MyCodeEditorState::clearPendingColumnAnchor()
{
    columnMode.clearPendingColumnAnchor();
}

void MyCodeEditorState::handleVirtualCursorChanged(
    MyCodeEditor* editor)
{
    columnMode.handleVirtualCursorChanged(editor);
}

void MyCodeEditorState::prepareVirtualCursorInput(
    MyCodeEditor* editor)
{
    columnMode.prepareVirtualCursorInput(editor);
}

bool MyCodeEditorState::handleVirtualCursorKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (templateSlotModeActive())
        return false;
    return columnMode.handleVirtualCursorKeyPress(
        editor,
        event);
}

QStringList MyCodeEditorState::columnSelectionRowTexts(
    MyCodeEditor* editor) const
{
    return columnMode.selectedRows(editor);
}

bool MyCodeEditorState::applyColumnSelectionRowTexts(
    MyCodeEditor* editor,
    const QStringList& rows,
    bool replaceSelection,
    QString* message)
{
    return columnMode.applyRows(
        editor,
        rows,
        replaceSelection,
        message);
}

void MyCodeEditorState::clearTemplateSlotMode(
    MyCodeEditor* editor,
    const QString& message,
    bool updatePresentation)
{
    templateSlots.clear(editor,
                        message,
                        updatePresentation);
}

bool MyCodeEditorState::handleTemplateSlotKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    return templateSlots.handleKeyPress(editor, event);
}

void MyCodeEditorState::handleTemplateSlotContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded,
    bool updatePresentation)
{
    templateSlots.handleContentsChange(
        editor,
        position,
        charsRemoved,
        charsAdded,
        updatePresentation);
}

void MyCodeEditorState::handleTemplateSlotCursorChanged(
    MyCodeEditor* editor)
{
    templateSlots.handleCursorChanged(editor);
}

void MyCodeEditorState::refreshScopeAndCurrentLineHighlight(
    MyCodeEditor* editor)
{
    selections.highlightCurrentSymbolReferences(editor);
    selections.highlightCurrentLine(editor);
}

void MyCodeEditorState::refreshSemanticPresentation(MyCodeEditor* editor)
{
    if (!editor)
        return;

    refreshScopeAndCurrentLineHighlight(editor);
    refreshDiagnosticPresentation(editor);
    refreshGhostAnnotations(editor);
}

std::uint64_t MyCodeEditorState::semanticDocumentRevision() const
{
    return semanticTextRevision;
}

QString MyCodeEditorState::materializeDocumentText(
    const MyCodeEditor* editor)
{
    ++hotPathMetrics.fullTextMaterializations;
    return editor ? editor->QPlainTextEdit::toPlainText() : QString();
}

const QString& MyCodeEditorState::cachedDocumentText()
{
    if (inlineFilterTextOverlayActive)
        ++hotPathMetrics.inlineFilterOverlayForcedTextReads;
    finishInlineFilterTextOverlay();
    const std::uint64_t before =
        semanticRevisionText.metricsForTest().materializationCount;
    const QString& text = semanticRevisionText.materialized();
    const std::uint64_t after =
        semanticRevisionText.metricsForTest().materializationCount;
    hotPathMetrics.fullTextMaterializations += after - before;
    return text;
}

int MyCodeEditorState::cachedDocumentLength() const
{
    if (!inlineFilterTextOverlayActive)
        return semanticRevisionText.size();
    return semanticRevisionText.size()
        - inlineFilterTextOverlayOriginalLength
        + inlineFilterTextOverlayCurrentText.size();
}

QString MyCodeEditorState::cachedDocumentSlice(int position, int length)
{
    const int documentLength = cachedDocumentLength();
    const int boundedPosition = qBound(0, position, documentLength);
    const int boundedLength = qBound(0,
                                     length,
                                     documentLength - boundedPosition);
    ++hotPathMetrics.cachedTextSliceReads;
    hotPathMetrics.cachedTextSliceCharacters +=
        static_cast<std::uint64_t>(boundedLength);
    if (!inlineFilterTextOverlayActive)
        return semanticRevisionText.mid(boundedPosition, boundedLength);

    const int requestEnd = boundedPosition + boundedLength;
    const int overlayStart = inlineFilterTextOverlayStart;
    const int overlayEnd =
        overlayStart + inlineFilterTextOverlayCurrentText.size();
    QString result;
    result.reserve(boundedLength);
    int cursor = boundedPosition;
    if (cursor < overlayStart) {
        const int beforeEnd = qMin(requestEnd, overlayStart);
        result += semanticRevisionText.mid(
            cursor, beforeEnd - cursor);
        cursor = beforeEnd;
    }
    if (cursor < requestEnd && cursor < overlayEnd) {
        const int currentStart = qMax(cursor, overlayStart);
        const int currentEnd = qMin(requestEnd, overlayEnd);
        result += inlineFilterTextOverlayCurrentText.mid(
            currentStart - overlayStart,
            currentEnd - currentStart);
        cursor = currentEnd;
    }
    if (cursor < requestEnd) {
        const int baseStart =
            cursor
            - inlineFilterTextOverlayCurrentText.size()
            + inlineFilterTextOverlayOriginalLength;
        result += semanticRevisionText.mid(
            baseStart, requestEnd - cursor);
    }
    return result;
}

bool MyCodeEditorState::beginInlineFilterTextOverlay(
    int startPosition,
    int endPosition)
{
    if (inlineFilterTextOverlayActive) {
        return startPosition == inlineFilterTextOverlayStart
            && endPosition
                   == inlineFilterTextOverlayStart
                       + inlineFilterTextOverlayCurrentText.size();
    }
    if (!syntax.isLargeDocument()
        || startPosition < 0
        || endPosition < startPosition
        || endPosition > semanticRevisionText.size()) {
        return false;
    }

    inlineFilterTextOverlayActive = true;
    inlineFilterTextOverlayStart = startPosition;
    inlineFilterTextOverlayOriginalLength =
        endPosition - startPosition;
    inlineFilterTextOverlayOriginalText =
        semanticRevisionText.mid(
            startPosition,
            inlineFilterTextOverlayOriginalLength);
    inlineFilterTextOverlayCurrentText =
        inlineFilterTextOverlayOriginalText;
    ++hotPathMetrics.inlineFilterOverlaySessions;
    return true;
}

void MyCodeEditorState::finishInlineFilterTextOverlay()
{
    if (!inlineFilterTextOverlayActive)
        return;
    syntax.flushPendingEdits();
    if (inlineFilterTextOverlayCurrentText
        != inlineFilterTextOverlayOriginalText) {
        semanticRevisionText.replace(
            inlineFilterTextOverlayStart,
            inlineFilterTextOverlayOriginalLength,
            inlineFilterTextOverlayCurrentText);
        ++hotPathMetrics.inlineFilterOverlayMaterializations;
    }
    inlineFilterTextOverlayActive = false;
    inlineFilterTextOverlayStart = -1;
    inlineFilterTextOverlayOriginalLength = 0;
    inlineFilterTextOverlayOriginalText.clear();
    inlineFilterTextOverlayCurrentText.clear();
}

void MyCodeEditorState::acceptLoadedTextAsSemanticBaseline(
    const MyCodeEditor* editor)
{
    Q_UNUSED(editor)
    semanticTextRevision = 0;
}

EditorHotPathMetrics MyCodeEditorState::hotPathMetricsForTest() const
{
    return hotPathMetrics;
}

TSTextStorageMetrics
MyCodeEditorState::cachedTextStorageMetricsForTest() const
{
    return semanticRevisionText.metricsForTest();
}
bool MyCodeEditorState::inlineFilterTextOverlayActiveForTest() const
{
    return inlineFilterTextOverlayActive;
}


EditorOccurrenceIndexStats
MyCodeEditorState::occurrenceIndexStatsForTest() const
{
    return selections.occurrenceIndexStatsForTest();
}

QList<int> MyCodeEditorState::occurrencePositionsForTest(
    const QString& word) const
{
    return selections.occurrencePositionsForTest(word);
}

void MyCodeEditorState::resetHotPathMetricsForTest()
{
    hotPathMetrics = {};
    semanticRevisionText.resetMetricsForTest();
    hotPathTimingEnabled = true;
}

void MyCodeEditorState::setIncludeFileProvider(
    EditorCompletionWorkflow::IncludeFileProvider provider)
{
    completionWorkflow.setIncludeFileProvider(std::move(provider));
}

void MyCodeEditorState::setIncludeNewHeaderCreator(
    EditorCompletionWorkflow::IncludeNewHeaderCreator creator)
{
    completionWorkflow.setIncludeNewHeaderCreator(std::move(creator));
}

void MyCodeEditorState::executeEditorActionCommand(
    MyCodeEditor* editor,
    const QString& command)
{
    if (command == QStringLiteral("format_document"))
        formatDocument(editor);
    else if (command == QStringLiteral("format_selection"))
        formatSelection(editor);
}

void MyCodeEditorState::setFormatterProfile(FormatterProfile profile)
{
    currentFormatterProfile = profile;
}

FormatterProfile MyCodeEditorState::formatterProfile() const
{
    return currentFormatterProfile;
}

void MyCodeEditorState::setFormatOnSaveEnabled(bool enabled)
{
    currentFormatOnSaveEnabled = enabled;
}

bool MyCodeEditorState::formatOnSaveEnabled() const
{
    return currentFormatOnSaveEnabled;
}

void MyCodeEditorState::formatDocument(MyCodeEditor* editor)
{
    if (!editor)
        return;

    const QString oldText = editor->toPlainText();
    const FormatterReport report =
        FormatterService::getInstance()->formatDocument(
            oldText,
            currentFormatterProfile);
    if (!report.changed) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Document already formatted"));
        return;
    }

    FormatterTriviaPositionMapper positionMapper(
        oldText,
        report.formattedText);
    FormatterCursorAnchor anchor;
    anchor.capture(editor, positionMapper);

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();
    anchor.restore(editor, positionMapper);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Formatted document (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(currentFormatterProfile)));
}

bool MyCodeEditorState::formatDocumentForSave(MyCodeEditor* editor)
{
    if (!editor || !currentFormatOnSaveEnabled)
        return false;

    const QString oldText = editor->toPlainText();
    const FormatterReport report =
        FormatterService::getInstance()->formatDocument(
            oldText,
            currentFormatterProfile);
    if (!report.changed)
        return false;

    FormatterTriviaPositionMapper positionMapper(
        oldText,
        report.formattedText);
    FormatterCursorAnchor anchor;
    anchor.capture(editor, positionMapper);

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();
    anchor.restore(editor, positionMapper);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Formatted document on save (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(currentFormatterProfile)));
    return true;
}

QList<GhostAnnotation> MyCodeEditorState::ghostAnnotationsForTest() const
{
    return ghostAnnotations.toList();
}

AnnotationLayerReport MyCodeEditorState::annotationLayerReportForTest(
    const AnnotationLayerQuery& query) const
{
    if (!annotationDisplayOptions.enabled)
        return {};
    AnnotationLayerQuery effective = query;
    effective.maxAnnotationsPerLine = qMin(
        effective.maxAnnotationsPerLine,
        annotationDisplayOptions.maxAnnotationsPerLine);
    effective.maxLanes = qMin(
        effective.maxLanes,
        annotationDisplayOptions.maxLanes);
    return annotationLayer.resolve(effective);
}

QString MyCodeEditorState::syntaxTextForTest() const
{
    const TSDocument* document = syntax.tsDocument();
    return document ? document->text() : QString();
}
EditorLargeFileSyntaxSnapshot
MyCodeEditorState::largeFileSyntaxSnapshotForTest() const
{
    return syntax.largeFileSnapshotForTest();
}


void MyCodeEditorState::setSemanticContextService(
    EditorSemanticContextService* service)
{
    semantic.setService(service);
}

void MyCodeEditorState::setHierarchyInstanceContext(
    const HierarchyInstanceContext& context)
{
    if (hierarchyInstance == context)
        return;
    hierarchyInstance = context;
    cancelSignalDefinitionEditor();
}

HierarchyInstanceContext MyCodeEditorState::hierarchyInstanceContext() const
{
    return hierarchyInstance;
}

void MyCodeEditorState::closeSemanticPopup(MyCodeEditor* editor)
{
    modes.exit(EditorModeId::InlineCandidates,
               EditorModeExitReason::Canceled);
    modes.exit(EditorModeId::CompletionCandidates,
               EditorModeExitReason::Canceled);
    if (!modes.exit(EditorModeId::SourceNavigation,
                    EditorModeExitReason::Canceled)) {
        sourceNavigation.closeForEditor(editor, selections);
    }
}

EditorBlockGeometry MyCodeEditorState::blockGeometry(
    const MyCodeEditor* editor,
    int blockNumber) const
{
    return geometry.blockGeometry(editor, blockNumber);
}

qreal MyCodeEditorState::documentHeightPx(const MyCodeEditor* editor) const
{
    return geometry.documentHeightPx(editor);
}

void MyCodeEditorState::setDocumentFileName(
    MyCodeEditor* editor,
    QString fileName)
{
    if (!identity.set(fileName))
        return;

    modes.exitAll(EditorModeExitReason::FileIdentityChanged);
    cancelSignalDefinitionEditor();
    diagnosticComputationRevision = 0;
    clearDiagnosticHighlights(editor);
    refreshGhostAnnotations(editor);
    emit editor->fileNameChanged(identity.current());
}

QString MyCodeEditorState::documentFileName() const
{
    return identity.current();
}

void MyCodeEditorState::highlightSearchMatches(
    MyCodeEditor* editor,
    const QString& text,
    bool caseSensitive)
{
    selections.highlightSearchMatches(editor, text, caseSensitive);
}

void MyCodeEditorState::clearSearchMatches(MyCodeEditor* editor)
{
    selections.clearSearchMatches(editor);
}

bool MyCodeEditorState::navigateSelectedSignalAssignment(
    MyCodeEditor* editor,
    bool previous,
    QString* message)
{
    if (message)
        message->clear();
    if (!editor)
        return false;

    const QTextCursor current = editor->textCursor();
    const int sourceChar = current.hasSelection()
        ? current.selectionStart()
        : current.position();
    const TSAssignmentNavigationTarget target =
        syntax.assignmentNavigationTargetAt(
            sourceChar, previous);
    if (!target.ok()) {
        const QString failure =
            target.status
                    == TSAssignmentNavigationStatus::
                        NoAssignment
                ? QStringLiteral(
                      "No assignment was found for the selected signal.")
                : QStringLiteral(
                      "Select a SystemVerilog signal identifier.");
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    exitInteractionModes(
        EditorModeExitReason::ExternalControl);
    QTextCursor cursor(editor->document());
    cursor.setPosition(target.targetChar);
    cursor.movePosition(
        QTextCursor::Right,
        QTextCursor::KeepAnchor,
        target.identifier.size());
    editor->setTextCursor(cursor);
    editor->centerCursor();
    selections.flashLine(editor);

    const QString result =
        QStringLiteral("%1 assignment%2")
            .arg(previous
                     ? QStringLiteral("Previous")
                     : QStringLiteral("Next"),
                 target.wrapped
                     ? QStringLiteral(" (wrapped)")
                     : QString());
    if (message)
        *message = result;
    emit editor->editorStatusMessageRequested(result);
    return true;
}

bool MyCodeEditorState::navigateConditionalBranch(
    MyCodeEditor* editor,
    bool previous,
    QString* message)
{
    if (message)
        message->clear();
    if (!editor)
        return false;

    const QTextCursor current = editor->textCursor();
    const int sourceChar = current.hasSelection()
        ? current.selectionStart()
        : current.position();
    const TSConditionalBranchNavigationTarget target =
        syntax.conditionalBranchNavigationTargetAt(
            sourceChar, previous);
    if (!target.ok()) {
        const QString failure =
            target.status
                    == TSConditionalBranchNavigationStatus::
                        IncompleteConditionalGroup
                ? QStringLiteral(
                      "The conditional compilation group is incomplete.")
                : QStringLiteral(
                      "The cursor is not inside a conditional compilation group.");
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    exitInteractionModes(
        EditorModeExitReason::ExternalControl);
    QTextCursor cursor(editor->document());
    cursor.setPosition(target.targetChar);
    cursor.movePosition(
        QTextCursor::Right,
        QTextCursor::KeepAnchor,
        target.targetDirective.size());
    editor->setTextCursor(cursor);
    editor->centerCursor();
    selections.flashLine(editor);

    const QString result =
        QStringLiteral("%1 conditional branch: %2%3")
            .arg(previous
                     ? QStringLiteral("Previous")
                     : QStringLiteral("Next"),
                 target.targetDirective,
                 target.wrapped
                     ? QStringLiteral(" (wrapped)")
                     : QString());
    if (message)
        *message = result;
    emit editor->editorStatusMessageRequested(result);
    return true;
}

void MyCodeEditorState::flashLine(MyCodeEditor* editor, int lineNumber)
{
    selections.flashLine(editor, lineNumber);
}

void MyCodeEditorState::applyLineNavigationTarget(
    MyCodeEditor* editor,
    const SourceLineNavigationTarget& target)
{
    cursorNavigation.applyLineTarget(editor, target);
    selections.flashLine(editor);
}
