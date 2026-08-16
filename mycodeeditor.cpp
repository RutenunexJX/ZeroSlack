#include "mycodeeditor.h"
#include "actionregistry.h"
#include "editorruntime.h"
#include "editorsemanticcontextservice.h"
#include "shareddocument.h"
#include "sourcenavigationservice.h"
#include "tsdocument.h"

#include <QCheckBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cstdio>
#include <QWheelEvent>

#include <memory>
#include <utility>

namespace {
void lifecycleTrace(const char* marker)
{
    if (!qEnvironmentVariableIsSet(
            "ZEROSLACK_EDITOR_LIFECYCLE_TRACE")) {
        return;
    }
    std::fprintf(stderr, "lifecycle.%s\n", marker);
    std::fflush(stderr);
}

int wheelNotchSteps(QWheelEvent* event)
{
    if (!event)
        return 0;

    const int angleY = event->angleDelta().y();
    if (angleY == 0)
        return event->pixelDelta().y() > 0
            ? 1
            : (event->pixelDelta().y() < 0 ? -1 : 0);

    const int absoluteSteps = qMax(1, qAbs(angleY) / 120);
    return angleY > 0 ? absoluteSteps : -absoluteSteps;
}

bool handleControlWheelFastScroll(MyCodeEditor* editor, QWheelEvent* event)
{
    if (!editor || !event)
        return false;

    QScrollBar* bar = editor->verticalScrollBar();
    if (!bar)
        return false;

    if (!event->pixelDelta().isNull()) {
        bar->setValue(bar->value() - event->pixelDelta().y() * 3);
        event->accept();
        return true;
    }

    const int steps = wheelNotchSteps(event);
    if (steps == 0)
        return false;

    const int fastStep = qMax(bar->singleStep() * 6, bar->pageStep() / 2);
    bar->setValue(bar->value() - steps * fastStep);
    event->accept();
    return true;
}

QString selectedPlainText(const QTextCursor& cursor)
{
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

bool stringsEqual(const QString& lhs,
                  const QString& rhs,
                  Qt::CaseSensitivity sensitivity)
{
    return QString::compare(lhs, rhs, sensitivity) == 0;
}

int occurrenceCount(const QString& text,
                    const QString& needle,
                    Qt::CaseSensitivity sensitivity)
{
    if (needle.isEmpty())
        return 0;

    int count = 0;
    int index = text.indexOf(needle, 0, sensitivity);
    while (index >= 0) {
        ++count;
        index = text.indexOf(needle, index + needle.size(), sensitivity);
    }
    return count;
}

bool findNextInEditor(MyCodeEditor* editor,
                      const QString& needle,
                      bool caseSensitive)
{
    if (!editor || needle.isEmpty())
        return false;

    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;

    const QTextCursor originalCursor = editor->textCursor();
    if (editor->find(needle, flags))
        return true;

    QTextCursor wrappedCursor = originalCursor;
    wrappedCursor.movePosition(QTextCursor::Start);
    editor->setTextCursor(wrappedCursor);
    if (editor->find(needle, flags))
        return true;

    editor->setTextCursor(originalCursor);
    return false;
}

void showFindDialogFor(MyCodeEditor* editor)
{
    if (!editor)
        return;

    auto* dialog = new QDialog(editor);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Find"));

    auto* layout = new QVBoxLayout(dialog);
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(QObject::tr("Find:"), dialog);
    auto* input = new QLineEdit(dialog);
    auto* caseSensitive = new QCheckBox(QObject::tr("Match case"), dialog);
    auto* previous = new QPushButton(QObject::tr("Previous"), dialog);
    auto* next = new QPushButton(QObject::tr("Next"), dialog);

    row->addWidget(label);
    row->addWidget(input, 1);
    row->addWidget(caseSensitive);
    row->addWidget(previous);
    row->addWidget(next);
    layout->addLayout(row);

    const QString selectedText = editor->textCursor().selectedText();
    if (!selectedText.isEmpty())
        input->setText(selectedText);

    auto findText = [editor, input, caseSensitive](bool backwards) {
        const QString needle = input->text();
        if (needle.isEmpty())
            return;

        QTextDocument::FindFlags flags;
        if (backwards)
            flags |= QTextDocument::FindBackward;
        if (caseSensitive->isChecked())
            flags |= QTextDocument::FindCaseSensitively;

        if (editor->find(needle, flags))
            return;

        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        editor->setTextCursor(cursor);
        editor->find(needle, flags);
    };

    QObject::connect(next, &QPushButton::clicked, dialog, [findText]() {
        findText(false);
    });
    QObject::connect(previous, &QPushButton::clicked, dialog, [findText]() {
        findText(true);
    });
    QObject::connect(input, &QLineEdit::returnPressed, dialog, [findText]() {
        findText(false);
    });
    QObject::connect(input, &QLineEdit::textChanged, dialog, [editor, input, caseSensitive]() {
        editor->highlightSearchMatches(input->text(), caseSensitive->isChecked());
    });
    QObject::connect(caseSensitive, &QCheckBox::toggled, dialog, [editor, input](bool checked) {
        editor->highlightSearchMatches(input->text(), checked);
    });
    QObject::connect(dialog, &QDialog::finished, dialog, [editor]() {
        editor->clearSearchMatches();
    });

    dialog->resize(520, dialog->sizeHint().height());
    dialog->show();
    input->setFocus();
    input->selectAll();
    editor->highlightSearchMatches(input->text(), caseSensitive->isChecked());
}

void showReplaceDialogFor(MyCodeEditor* editor)
{
    if (!editor)
        return;

    auto* dialog = new QDialog(editor);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Replace"));

    auto* layout = new QVBoxLayout(dialog);
    auto* findRow = new QHBoxLayout;
    auto* replaceRow = new QHBoxLayout;
    auto* buttonRow = new QHBoxLayout;
    auto* findLabel = new QLabel(QObject::tr("Find:"), dialog);
    auto* findInput = new QLineEdit(dialog);
    auto* replaceLabel = new QLabel(QObject::tr("Replace:"), dialog);
    auto* replaceInput = new QLineEdit(dialog);
    auto* caseSensitive = new QCheckBox(QObject::tr("Match case"), dialog);
    auto* findNext = new QPushButton(QObject::tr("Find Next"), dialog);
    auto* replace = new QPushButton(QObject::tr("Replace"), dialog);
    auto* replaceAll = new QPushButton(QObject::tr("Replace All"), dialog);

    findRow->addWidget(findLabel);
    findRow->addWidget(findInput, 1);
    findRow->addWidget(caseSensitive);
    replaceRow->addWidget(replaceLabel);
    replaceRow->addWidget(replaceInput, 1);
    buttonRow->addStretch(1);
    buttonRow->addWidget(findNext);
    buttonRow->addWidget(replace);
    buttonRow->addWidget(replaceAll);
    layout->addLayout(findRow);
    layout->addLayout(replaceRow);
    layout->addLayout(buttonRow);

    const QString selectedText = selectedPlainText(editor->textCursor());
    if (!selectedText.isEmpty())
        findInput->setText(selectedText);

    auto findNextMatch = [editor, findInput, caseSensitive]() {
        const QString needle = findInput->text();
        if (needle.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                QObject::tr("Find text is empty"));
            return;
        }
        if (!findNextInEditor(editor, needle, caseSensitive->isChecked())) {
            emit editor->editorStatusMessageRequested(
                QObject::tr("No match for \"%1\"").arg(needle));
        }
    };

    QObject::connect(findNext, &QPushButton::clicked, dialog, findNextMatch);
    QObject::connect(replace, &QPushButton::clicked, dialog, [editor,
                                                              findInput,
                                                              replaceInput,
                                                              caseSensitive]() {
        editor->replaceNextText(findInput->text(),
                                replaceInput->text(),
                                caseSensitive->isChecked());
    });
    QObject::connect(replaceAll, &QPushButton::clicked, dialog, [editor,
                                                                 findInput,
                                                                 replaceInput,
                                                                 caseSensitive]() {
        editor->replaceAllText(findInput->text(),
                               replaceInput->text(),
                               caseSensitive->isChecked());
    });
    QObject::connect(findInput, &QLineEdit::returnPressed,
                     dialog, findNextMatch);
    QObject::connect(replaceInput, &QLineEdit::returnPressed,
                     dialog, [editor,
                              findInput,
                              replaceInput,
                              caseSensitive]() {
        editor->replaceNextText(findInput->text(),
                                replaceInput->text(),
                                caseSensitive->isChecked());
    });
    QObject::connect(findInput, &QLineEdit::textChanged,
                     dialog, [editor, findInput, caseSensitive]() {
        editor->highlightSearchMatches(findInput->text(),
                                       caseSensitive->isChecked());
    });
    QObject::connect(caseSensitive, &QCheckBox::toggled,
                     dialog, [editor, findInput](bool checked) {
        editor->highlightSearchMatches(findInput->text(), checked);
    });
    QObject::connect(dialog, &QDialog::finished, dialog, [editor]() {
        editor->clearSearchMatches();
    });

    dialog->resize(620, dialog->sizeHint().height());
    dialog->show();
    findInput->setFocus();
    findInput->selectAll();
    editor->highlightSearchMatches(findInput->text(),
                                   caseSensitive->isChecked());
}
}

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    setProperty("codeEditorSurface", true);
    state->attachToEditor(this);
}

MyCodeEditor::SynchronousEditTransaction::SynchronousEditTransaction(
    MyCodeEditor* targetEditor)
    : editor(targetEditor)
{
    if (editor)
        editor->state->beginSynchronousEditTransaction(editor);
}

MyCodeEditor::SynchronousEditTransaction::~SynchronousEditTransaction()
{
    if (editor)
        editor->state->endSynchronousEditTransaction(editor);
}

MyCodeEditor::~MyCodeEditor()
{
    state->shutdown(this);
}

void MyCodeEditor::attachSharedDocument(
    QTextDocument* sharedDocument,
    std::uint64_t textRevision)
{
    state->rebindDocument(this, sharedDocument, textRevision);
}

void MyCodeEditor::setPlainText(const QString& text)
{
    {
        auto edit = beginSynchronousEditTransaction();
        QPlainTextEdit::setPlainText(text);
    }
    emit wavePreviewScopeChanged();
}

void MyCodeEditor::insertPlainText(const QString& text)
{
    auto edit = beginSynchronousEditTransaction();
    QPlainTextEdit::insertPlainText(text);
}

void MyCodeEditor::clear()
{
    auto edit = beginSynchronousEditTransaction();
    QPlainTextEdit::clear();
}

void MyCodeEditor::copy()
{
    state->executeClipboardAction(
        this, QStringLiteral("edit.copy"));
}

void MyCodeEditor::cut()
{
    auto edit = beginSynchronousEditTransaction();
    state->executeClipboardAction(
        this, QStringLiteral("edit.cut"));
}

void MyCodeEditor::paste()
{
    auto edit = beginSynchronousEditTransaction();
    if (isReadOnly() || !document())
        return;

    QTextCursor transaction(document());
    transaction.beginEditBlock();
    state->executeClipboardAction(
        this, QStringLiteral("edit.paste"));
    transaction.endEditBlock();
}

void MyCodeEditor::undo()
{
    auto edit = beginSynchronousEditTransaction();
    const int beforeUndoSteps = document()
        ? document()->availableUndoSteps()
        : 0;
    state->beginUndoRedo();
    QPlainTextEdit::undo();
    const int afterUndoSteps = document()
        ? document()->availableUndoSteps()
        : 0;
    state->restoreCursorAfterUndoRedo(
        this, false, beforeUndoSteps, afterUndoSteps);
}

void MyCodeEditor::redo()
{
    auto edit = beginSynchronousEditTransaction();
    const int beforeUndoSteps = document()
        ? document()->availableUndoSteps()
        : 0;
    state->beginUndoRedo();
    QPlainTextEdit::redo();
    const int afterUndoSteps = document()
        ? document()->availableUndoSteps()
        : 0;
    state->restoreCursorAfterUndoRedo(
        this, true, beforeUndoSteps, afterUndoSteps);
}

bool MyCodeEditor::find(const QString& expression,
                        QTextDocument::FindFlags options)
{
    const bool found = QPlainTextEdit::find(expression, options);
    if (!found)
        return false;
    state->folding.revealLine(this, textCursor().blockNumber());
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        state->projection.ensureCursorVisible(this, true);
    return true;
}

void MyCodeEditor::showFindDialog()
{
    showFindDialogFor(this);
}

bool MyCodeEditor::duplicateLines(
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    const EditorLineOperationResult result =
        state->executeLineOperation(
            this,
            EditorLineOperation::DuplicateLines);
    if (failureReason)
        *failureReason = result.failureReason;
    return result.succeeded;
}

bool MyCodeEditor::deleteLines(
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    const EditorLineOperationResult result =
        state->executeLineOperation(
            this,
            EditorLineOperation::DeleteLines);
    if (failureReason)
        *failureReason = result.failureReason;
    return result.succeeded;
}

bool MyCodeEditor::joinLines(
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    const EditorLineOperationResult result =
        state->executeLineOperation(
            this,
            EditorLineOperation::JoinWithNextLine);
    if (failureReason)
        *failureReason = result.failureReason;
    return result.succeeded;
}

bool MyCodeEditor::moveLinesUp(
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    const EditorLineOperationResult result =
        state->executeLineOperation(
            this,
            EditorLineOperation::MoveLinesUp);
    if (failureReason)
        *failureReason = result.failureReason;
    return result.succeeded;
}

bool MyCodeEditor::moveLinesDown(
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    const EditorLineOperationResult result =
        state->executeLineOperation(
            this,
            EditorLineOperation::MoveLinesDown);
    if (failureReason)
        *failureReason = result.failureReason;
    return result.succeeded;
}

bool MyCodeEditor::addNextSymbolOccurrence(
    QString* failureReason)
{
    return state->selectSymbolOccurrences(
        this,
        false,
        failureReason);
}

bool MyCodeEditor::selectAllSymbolOccurrences(
    QString* failureReason)
{
    return state->selectSymbolOccurrences(
        this,
        true,
        failureReason);
}

bool MyCodeEditor::expandSmartSelection(
    QString* message)
{
    return state->expandSmartSelection(
        this, message);
}

bool MyCodeEditor::goToNextSelectedSymbolOccurrence(
    QString* message)
{
    return state->navigateSelectedSymbolOccurrence(
        this, false, message);
}

bool MyCodeEditor::goToPreviousSelectedSymbolOccurrence(
    QString* message)
{
    return state->navigateSelectedSymbolOccurrence(
        this, true, message);
}

void MyCodeEditor::setTextCursor(const QTextCursor& cursor)
{
    state->folding.revealLine(this, cursor.blockNumber());
    auto edit = beginSynchronousEditTransaction();
    QPlainTextEdit::setTextCursor(cursor);
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        state->projection.ensureCursorVisible(this, false);
}

void MyCodeEditor::centerCursor()
{
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        state->projection.ensureCursorVisible(this, true);
    else
        QPlainTextEdit::centerCursor();
}

void MyCodeEditor::ensureCursorVisible()
{
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        state->projection.ensureCursorVisible(this, false);
    else
        QPlainTextEdit::ensureCursorVisible();
}

QTextCursor MyCodeEditor::cursorForPosition(const QPoint& position) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.cursorForPosition(editor, position)
        : QPlainTextEdit::cursorForPosition(position);
}

QRect MyCodeEditor::cursorRect(const QTextCursor& cursor) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.cursorRect(editor, cursor)
        : QPlainTextEdit::cursorRect(cursor);
}

QRect MyCodeEditor::cursorRect() const
{
    return cursorRect(textCursor());
}

QTextBlock MyCodeEditor::firstVisibleBlock() const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.firstVisibleBlock(editor)
        : QPlainTextEdit::firstVisibleBlock();
}

QTextBlock MyCodeEditor::nextVisibleBlock(const QTextBlock& block) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.nextVisibleBlock(editor, block)
        : block.next();
}

QRectF MyCodeEditor::blockBoundingGeometry(const QTextBlock& block) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.blockBoundingGeometry(editor, block)
        : QPlainTextEdit::blockBoundingGeometry(block);
}

QRectF MyCodeEditor::blockBoundingRect(const QTextBlock& block) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.blockBoundingRect(editor, block)
        : QPlainTextEdit::blockBoundingRect(block);
}

QPointF MyCodeEditor::contentOffset() const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active()
        ? state->projection.contentOffset(editor)
        : QPlainTextEdit::contentOffset();
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    state->refreshScopeAndCurrentLineHighlight(this);
}

void MyCodeEditor::refreshSemanticPresentation()
{
    state->refreshSemanticPresentation(this);
}

std::uint64_t MyCodeEditor::semanticDocumentRevision() const
{
    return state->semanticDocumentRevision();
}

const TSDocument* MyCodeEditor::syntaxDocument() const
{
    return state->syntax.tsDocument();
}

QString MyCodeEditor::toPlainText() const
{
    return state->materializeDocumentText(this);
}

const QString& MyCodeEditor::cachedDocumentText() const
{
    return state->cachedDocumentText();
}

int MyCodeEditor::cachedDocumentLength() const
{
    return state->cachedDocumentLength();
}

QString MyCodeEditor::cachedDocumentSlice(int position, int length) const
{
    return state->cachedDocumentSlice(position, length);
}

MyCodeEditor::SynchronousEditTransaction
MyCodeEditor::beginSynchronousEditTransaction()
{
    return SynchronousEditTransaction(this);
}

EditorSynchronousEditState MyCodeEditor::synchronousEditStateForTest() const
{
    return state->synchronousEditStateForTest();
}

void MyCodeEditor::acceptLoadedTextAsSemanticBaseline()
{
    state->acceptLoadedTextAsSemanticBaseline(this);
}

EditorHotPathMetrics MyCodeEditor::hotPathMetricsForTest() const
{
    return state->hotPathMetricsForTest();
}

TSTextStorageMetrics MyCodeEditor::cachedTextStorageMetricsForTest() const
{
    return state->cachedTextStorageMetricsForTest();
}
bool MyCodeEditor::inlineFilterTextOverlayActiveForTest() const
{
    return state->inlineFilterTextOverlayActiveForTest();
}


EditorOccurrenceIndexStats MyCodeEditor::occurrenceIndexStatsForTest() const
{
    return state->occurrenceIndexStatsForTest();
}

QList<int> MyCodeEditor::occurrencePositionsForTest(
    const QString& word) const
{
    return state->occurrencePositionsForTest(word);
}

void MyCodeEditor::resetHotPathMetricsForTest()
{
    state->resetHotPathMetricsForTest();
}

void MyCodeEditor::setIncludeFileCompletionProvider(
    std::function<QStringList(const QString& currentFile)> provider)
{
    state->setIncludeFileProvider(std::move(provider));
}

void MyCodeEditor::setIncludeNewHeaderCreator(
    std::function<IncludeNewHeaderResult(
        const IncludeNewHeaderRequest& request)> creator)
{
    state->setIncludeNewHeaderCreator(std::move(creator));
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->setSemanticContextService(service);
}

void MyCodeEditor::setHierarchyInstanceContext(
    const HierarchyInstanceContext& context)
{
    if (state->hierarchyInstanceContext() == context)
        return;

    state->closeSemanticPopup(this);
    state->setHierarchyInstanceContext(context);
    emit hierarchyInstanceContextChanged(context);
    state->refreshGhostAnnotations(this);
}

HierarchyInstanceContext MyCodeEditor::hierarchyInstanceContext() const
{
    return state->hierarchyInstanceContext();
}

void MyCodeEditor::closeSemanticPopup()
{
    state->closeSemanticPopup(this);
}

EditorBlockGeometry MyCodeEditor::blockGeometry(int blockNumber) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    if (state->projection.active()) {
        const EditorProjectionGeometry geometry =
            state->projection.blockGeometry(editor, blockNumber);
        return {geometry.top, geometry.height};
    }
    return state->blockGeometry(this, blockNumber);
}

qreal MyCodeEditor::documentHeightPx() const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    if (state->projection.active())
        return state->projection.documentHeight();
    return state->documentHeightPx(this);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    state->handleResize(this);
}

void MyCodeEditor::paintEvent(QPaintEvent *event)
{
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        state->projection.paint(this, event);
    else
        QPlainTextEdit::paintEvent(event);
    state->paintFoldPlaceholders(this, event);
    state->paintGhostAnnotations(this, event);
    state->paintMultiCursor(this, event);
    state->paintColumnSelection(this, event);
    state->paintDiagnosticOverview(this, event);
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    auto edit = beginSynchronousEditTransaction();
    state->handleContextMenu(this, event);
}

EditorSemanticContext MyCodeEditor::editorSemanticContextForPosition(
    int cursorPosition,
    bool includeDocumentText) const
{
    return state->semanticContextForPosition(
        this,
        cursorPosition,
        includeDocumentText);
}

bool MyCodeEditor::syntaxCommentAt(int cursorPosition) const
{
    const TSDocument* document = state->syntax.tsDocument();
    return document && document->isCommentAt(cursorPosition);
}

GhostNumericLiteralReport MyCodeEditor::numericLiteralAt(
    int cursorPosition) const
{
    return GhostAnnotationService::getInstance()->numericLiteralAt(
        GhostNumericLiteralQuery{
            state->syntax.tsDocument(),
            cursorPosition
        });
}

void MyCodeEditor::setDocumentFileName(QString fileName)
{
    state->setDocumentFileName(this, fileName);
}

QString MyCodeEditor::documentFileName() const
{
    return state->documentFileName();
}

QString MyCodeEditor::currentModuleName() const
{
    return state->currentModuleName(this);
}

EditorAlwaysScopeTarget MyCodeEditor::currentAlwaysScopeTarget() const
{
    return state->currentAlwaysScopeTarget(this);
}

EditorModuleScopeTarget MyCodeEditor::currentModuleScopeTarget() const
{
    return state->currentModuleScopeTarget(this);
}

bool MyCodeEditor::addPortRow(QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->addPortRow(this, message);
}

bool MyCodeEditor::addSignalRow(QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->addSignalRow(this, message);
}

bool MyCodeEditor::addParameterRow(QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->addParameterRow(this, message);
}

bool MyCodeEditor::goToFinalEndmodule(QString* message)
{
    return state->goToFinalEndmodule(this, message);
}

EditorPackageToolAvailability MyCodeEditor::currentPackageToolAvailability()
    const
{
    return state->currentPackageToolAvailability(this);
}

bool MyCodeEditor::executePackageToolInsert(PackageToolKind kind,
                                            QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->executePackageToolInsert(this, kind, message);
}

bool MyCodeEditor::selectInsideBeginEnd(QString* message)
{
    return state->selectInsideBeginEnd(this, message);
}

void MyCodeEditor::startTemplateSlotMode(
    int insertionStart,
    int insertedLength,
    const CodeTemplateSlotList& slotMetadata)
{
    state->startTemplateSlotMode(this,
                                 insertionStart,
                                 insertedLength,
                                 slotMetadata);
}

bool MyCodeEditor::templateSlotModeActive() const
{
    return state->templateSlotModeActive();
}

int MyCodeEditor::templateSlotModeActiveIndex() const
{
    return state->templateSlotModeActiveIndex();
}

int MyCodeEditor::templateSlotModeSlotCount() const
{
    return state->templateSlotModeSlotCount();
}

bool MyCodeEditor::templateSlotModeBlinkOnForTest() const
{
    return state->templateSlotModeBlinkOn();
}

bool MyCodeEditor::columnSelectionActive() const
{
    return state->columnSelectionActiveForCommand();
}

EditorColumnModeSnapshot MyCodeEditor::columnModeSnapshotForTest() const
{
    return state->columnModeSnapshotForTest();
}

bool MyCodeEditor::virtualCursorActiveForTest() const
{
    return state->virtualCursorActiveForTest();
}

int MyCodeEditor::virtualCursorLineForTest() const
{
    return state->virtualCursorLineForTest();
}

int MyCodeEditor::virtualCursorColumnForTest() const
{
    return state->virtualCursorColumnForTest();
}

QStringList MyCodeEditor::columnSelectionTexts() const
{
    return state->columnSelectionRowTexts(const_cast<MyCodeEditor*>(this));
}

bool MyCodeEditor::applyColumnSelectionTexts(const QStringList& rows,
                                             bool replaceSelection,
                                             QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->applyColumnSelectionRowTexts(this,
                                               rows,
                                               replaceSelection,
                                               message);
}

void MyCodeEditor::setDiagnosticHighlights(
    const QList<SemanticDiagnostic>& diagnostics)
{
    state->setDiagnosticHighlights(this, diagnostics);
}

QString MyCodeEditor::diagnosticTooltipForLineForTest(
    int zeroBasedLine) const
{
    return state->diagnosticTooltipForLine(zeroBasedLine);
}

QList<int> MyCodeEditor::diagnosticOverviewLinesForTest() const
{
    return state->diagnosticOverviewLinesForTest();
}

SemanticDiagnostic::Severity
MyCodeEditor::diagnosticSeverityForLineForTest(
    int zeroBasedLine,
    bool* available) const
{
    return state->diagnosticSeverityForLineForTest(
        zeroBasedLine, available);
}

void MyCodeEditor::setSemanticDecorations(
    const QList<SemanticDecoration>& decorations)
{
    state->setSemanticDecorations(this, decorations);
}

void MyCodeEditor::setGhostAnnotations(
    const QList<GhostAnnotation>& annotations)
{
    state->setGhostAnnotations(this, annotations);
}

void MyCodeEditor::setAnnotationDisplayOptions(
    const EditorAnnotationDisplayOptions& options)
{
    state->setAnnotationDisplayOptions(this, options);
}

EditorAnnotationDisplayOptions
MyCodeEditor::annotationDisplayOptions() const
{
    return state->currentAnnotationDisplayOptions();
}

void MyCodeEditor::setFormatterProfile(FormatterProfile profile)
{
    if (state->formatterProfile() == profile)
        return;
    state->setFormatterProfile(profile);
    emit formatterProfileChanged(profile);
}

FormatterProfile MyCodeEditor::formatterProfile() const
{
    return state->formatterProfile();
}

QList<MyCodeEditor*>
MyCodeEditor::sharedDocumentViewsForFormatting() const
{
    auto* sharedDocument = qobject_cast<SharedDocument*>(
        document() ? document()->parent() : nullptr);
    QList<MyCodeEditor*> views = sharedDocument
        ? sharedDocument->views()
        : QList<MyCodeEditor*>();
    views.removeAll(const_cast<MyCodeEditor*>(this));
    views.append(const_cast<MyCodeEditor*>(this));
    return views;
}

FormatterReport MyCodeEditor::formatDocument()
{
    auto edit = beginSynchronousEditTransaction();
    return state->formatDocument(this);
}

FormatterReport MyCodeEditor::formatSelection()
{
    auto edit = beginSynchronousEditTransaction();
    return state->formatSelection(this);
}

bool MyCodeEditor::goToLineNumber(int lineNumber)
{
    const int maxLine = document() ? document()->blockCount() : 0;
    if (lineNumber < 1 || lineNumber > maxLine) {
        emit editorStatusMessageRequested(
            tr("Line must be between 1 and %1").arg(qMax(1, maxLine)));
        return false;
    }

    const QTextBlock block = document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) {
        emit editorStatusMessageRequested(
            tr("Line must be between 1 and %1").arg(qMax(1, maxLine)));
        return false;
    }

    QTextCursor cursor(block);
    cursor.setPosition(block.position());
    setTextCursor(cursor);
    centerCursor();
    flashLine(lineNumber);
    emit editorStatusMessageRequested(tr("Line %1").arg(lineNumber));
    return true;
}

bool MyCodeEditor::goToPreviousAssignmentForSelectedSignal(
    QString* message)
{
    return state->navigateSelectedSignalAssignment(
        this, true, message);
}

bool MyCodeEditor::goToNextAssignmentForSelectedSignal(
    QString* message)
{
    return state->navigateSelectedSignalAssignment(
        this, false, message);
}

bool MyCodeEditor::goToPreviousConditionalBranch(
    QString* message)
{
    return state->navigateConditionalBranch(
        this, true, message);
}

bool MyCodeEditor::goToNextConditionalBranch(
    QString* message)
{
    return state->navigateConditionalBranch(
        this, false, message);
}

bool MyCodeEditor::replaceNextText(const QString& needle,
                                   const QString& replacement,
                                   bool caseSensitive)
{
    auto edit = beginSynchronousEditTransaction();
    if (needle.isEmpty()) {
        emit editorStatusMessageRequested(tr("Find text is empty"));
        return false;
    }

    const Qt::CaseSensitivity sensitivity = caseSensitive
        ? Qt::CaseSensitive
        : Qt::CaseInsensitive;
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()
        || !stringsEqual(selectedPlainText(cursor), needle, sensitivity)) {
        if (!findNextInEditor(this, needle, caseSensitive)) {
            emit editorStatusMessageRequested(
                tr("No match for \"%1\"").arg(needle));
            return false;
        }
        cursor = textCursor();
    }

    if (!cursor.hasSelection()) {
        emit editorStatusMessageRequested(
            tr("No match for \"%1\"").arg(needle));
        return false;
    }

    cursor.beginEditBlock();
    cursor.insertText(replacement);
    cursor.endEditBlock();
    setTextCursor(cursor);
    emit editorStatusMessageRequested(tr("Replaced next match"));
    return true;
}

int MyCodeEditor::replaceAllText(const QString& needle,
                                 const QString& replacement,
                                 bool caseSensitive)
{
    auto edit = beginSynchronousEditTransaction();
    if (needle.isEmpty()) {
        emit editorStatusMessageRequested(tr("Find text is empty"));
        return 0;
    }

    const Qt::CaseSensitivity sensitivity = caseSensitive
        ? Qt::CaseSensitive
        : Qt::CaseInsensitive;
    const QString originalText = toPlainText();
    const int count = occurrenceCount(originalText, needle, sensitivity);
    if (count == 0) {
        emit editorStatusMessageRequested(
            tr("No match for \"%1\"").arg(needle));
        return 0;
    }

    QString replacedText = originalText;
    replacedText.replace(needle, replacement, sensitivity);

    const int oldPosition = textCursor().position();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(replacedText);
    cursor.endEditBlock();

    const int restoredPosition =
        qBound(0, oldPosition, document()->characterCount() - 1);
    cursor.setPosition(restoredPosition);
    setTextCursor(cursor);
    emit editorStatusMessageRequested(
        tr("Replaced %1 match(es)").arg(count));
    return count;
}

int MyCodeEditor::showGotoLineDialog()
{
    const int maxLine = qMax(1, document() ? document()->blockCount() : 1);
    bool accepted = false;
    const int lineNumber = QInputDialog::getInt(
        this,
        tr("Go to Line"),
        tr("Line:"),
        qBound(1, textCursor().blockNumber() + 1, maxLine),
        1,
        maxLine,
        1,
        &accepted);
    if (accepted && goToLineNumber(lineNumber))
        return lineNumber;
    return -1;
}

void MyCodeEditor::showReplaceDialog()
{
    showReplaceDialogFor(this);
}

void MyCodeEditor::commentSelectionOrLine()
{
    auto edit = beginSynchronousEditTransaction();
    state->commentSelectionOrLine(this);
}

void MyCodeEditor::uncommentSelectionOrLine()
{
    auto edit = beginSynchronousEditTransaction();
    state->uncommentSelectionOrLine(this);
}

void MyCodeEditor::indentSelectionOrLine()
{
    auto edit = beginSynchronousEditTransaction();
    state->indentSelectionOrLine(this);
}

void MyCodeEditor::unindentSelectionOrLine()
{
    auto edit = beginSynchronousEditTransaction();
    state->unindentSelectionOrLine(this);
}

bool MyCodeEditor::clearSelectedAssignmentRhs(QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->clearSelectedAssignmentRhs(this, message);
}

EditorStructuralContextMenuState
MyCodeEditor::structuralContextMenuState(
    int cursorPosition) const
{
    return state->structuralContextMenuState(
        this, cursorPosition);
}

bool MyCodeEditor::editInstanceSlotsAt(
    int cursorPosition,
    QString* message)
{
    return state->editInstanceSlotsAt(
        this, cursorPosition, message);
}

bool MyCodeEditor::beginSignalDefinitionEditorAt(
    int cursorPosition,
    QString* failureReason)
{
    return state->beginSignalDefinitionEditor(
        this, cursorPosition, failureReason);
}

bool MyCodeEditor::editInstanceSlotsAtForTest(
    int cursorPosition,
    QString* message)
{
    return editInstanceSlotsAt(cursorPosition, message);
}

QStringList MyCodeEditor::structuralContextMenuActionsForTest(
    int cursorPosition)
{
    QStringList actions;
    const EditorStructuralContextMenuState contextState =
        structuralContextMenuState(cursorPosition);
    if (contextState.signalDefinitionAvailable) {
        const ActionDescriptor* descriptor =
            findActionById(
                QStringLiteral("refactor.createSignalDefinition"));
        const ActionAliasDescriptor menuAlias = descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ContextMenu)
            : ActionAliasDescriptor();
        actions.append(
            menuAlias.label.isEmpty()
                ? QStringLiteral("Create Signal Definition...")
                : menuAlias.label);
    }
    if (contextState.signalDefinitionAvailable
        && contextState.instanceSlotsAvailable) {
        actions.append(QStringLiteral("<separator>"));
    }
    if (contextState.instanceSlotsAvailable) {
        const ActionDescriptor* descriptor =
            findActionById(
                QStringLiteral("refactor.editInstanceSlots"));
        const ActionAliasDescriptor menuAlias = descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ContextMenu)
            : ActionAliasDescriptor();
        actions.append(
            menuAlias.label.isEmpty()
                ? QStringLiteral("Edit Instance Slots")
                : menuAlias.label);
    }
    return actions;
}

QString MyCodeEditor::signalDefinitionCandidateForTest(
    int cursorPosition,
    QString* failureReason) const
{
    return state->signalDefinitionCandidateAt(
        this, cursorPosition, failureReason);
}

bool MyCodeEditor::beginSignalDefinitionEditorForTest(
    int cursorPosition,
    QString* failureReason)
{
    return beginSignalDefinitionEditorAt(
        cursorPosition, failureReason);
}

bool MyCodeEditor::confirmSignalDefinitionForTest(
    const QString& declaration,
    QString* failureReason)
{
    auto edit = beginSynchronousEditTransaction();
    return state->confirmSignalDefinition(
        this, declaration, failureReason);
}

bool MyCodeEditor::startSignalSelectionMode(QString* message)
{
    return state->startSignalSelectionMode(this, message);
}

bool MyCodeEditor::signalSelectionModeActiveForTest() const
{
    return state->signalSelectionModeActive();
}

QStringList MyCodeEditor::selectedSignalNames() const
{
    return state->selectedSignalNames();
}

QStringList MyCodeEditor::selectedSignalNamesForTest() const
{
    return selectedSignalNames();
}

bool MyCodeEditor::toggleSignalSelectionAtForTest(
    int cursorPosition)
{
    return state->toggleSignalSelectionAt(
        this, cursorPosition, true);
}

bool MyCodeEditor::createAssignmentQueueAtForTest(
    int cursorPosition,
    QString* message)
{
    return createAssignmentQueueAt(
        cursorPosition, message);
}

bool MyCodeEditor::createAssignmentQueueAt(
    int cursorPosition,
    QString* message)
{
    auto edit = beginSynchronousEditTransaction();
    return state->createAssignmentQueueAt(
        this, cursorPosition, message);
}

EditorModeSnapshot MyCodeEditor::editorModeSnapshot() const
{
    return state->modeSnapshot();
}

bool MyCodeEditor::editorModeActiveForTest(EditorModeId id) const
{
    return state->modes.isActive(id);
}

void MyCodeEditor::exitInteractionModes(EditorModeExitReason reason)
{
    state->exitInteractionModes(reason);
}

void MyCodeEditor::highlightSearchMatches(
    const QString& text,
    bool caseSensitive)
{
    state->highlightSearchMatches(this, text, caseSensitive);
}

void MyCodeEditor::clearSearchMatches()
{
    state->clearSearchMatches(this);
}

void MyCodeEditor::flashLine(int lineNumber)
{
    state->flashLine(this, lineNumber);
}

void MyCodeEditor::applyAppearanceSettings(
    const EditorAppearanceOptions& options)
{
    state->applyAppearanceSettings(this, options);
    state->projection.invalidate();
    state->projection.ensure(this, state->folding.collapsedLineRanges());
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    lifecycleTrace("key.enter");
    auto edit = beginSynchronousEditTransaction();
    lifecycleTrace("key.transaction");
    lifecycleTrace("key.before-dispatch");
    if (state->handleKeyPress(this, event)) {
        lifecycleTrace("key.handled");
        return;
    }

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()
        && event
        && !(event->modifiers()
                 & (Qt::ControlModifier
                    | Qt::AltModifier
                    | Qt::MetaModifier))
        && (event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Down
            || event->key() == Qt::Key_PageUp
            || event->key() == Qt::Key_PageDown)) {
        const QTextCursor current = textCursor();
        const QTextBlock currentBlock = current.block();
        const int row = state->projection.visibleRowForSourceLine(
            currentBlock.blockNumber());
        if (row >= 0) {
            const int page = qMax(1, verticalScrollBar()->pageStep());
            const int delta = event->key() == Qt::Key_Up
                ? -1
                : (event->key() == Qt::Key_Down
                       ? 1
                       : (event->key() == Qt::Key_PageUp ? -page : page));
            const int targetRow = qBound(
                0,
                row + delta,
                state->projection.visibleRowCount() - 1);
            const int targetLine =
                state->projection.sourceLineForVisibleRow(targetRow);
            const QTextBlock targetBlock =
                document()->findBlockByNumber(targetLine);
            const int column = qMax(
                0, current.position() - currentBlock.position());
            QTextCursor target(document());
            target.setPosition(
                targetBlock.position()
                + qMin(column, targetBlock.text().size()));
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                const int position = target.position();
                target.setPosition(current.anchor());
                target.setPosition(position, QTextCursor::KeepAnchor);
            }
            QPlainTextEdit::setTextCursor(target);
            state->projection.ensureCursorVisible(this, false);
            viewport()->update();
            event->accept();
            return;
        }
    }

    const int previousProjectedScroll = state->projection.active()
        ? verticalScrollBar()->value() : -1;
    const int originalAnchor = textCursor().anchor();
    lifecycleTrace("key.before-base");
    QPlainTextEdit::keyPressEvent(event);
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()) {
        if (previousProjectedScroll >= 0)
            verticalScrollBar()->setValue(previousProjectedScroll);
        QTextCursor corrected = textCursor();
        int sourceLine = corrected.blockNumber();
        if (!state->projection.sourceLineVisible(sourceLine)) {
            const bool forward = event
                && (event->key() == Qt::Key_Right
                    || event->key() == Qt::Key_Down
                    || event->key() == Qt::Key_PageDown);
            int candidate = sourceLine;
            while (candidate >= 0 && candidate < document()->blockCount()
                   && !state->projection.sourceLineVisible(candidate)) {
                candidate += forward ? 1 : -1;
            }
            if (candidate < 0 || candidate >= document()->blockCount()) {
                candidate = qBound(0,
                                   sourceLine,
                                   document()->blockCount() - 1);
                while (candidate > 0
                       && !state->projection.sourceLineVisible(candidate)) {
                    --candidate;
                }
            }
            const QTextBlock visibleBlock =
                document()->findBlockByNumber(candidate);
            if (visibleBlock.isValid()) {
                const int correctedPosition = forward
                    ? visibleBlock.position()
                    : visibleBlock.position() + visibleBlock.text().size();
                corrected = QTextCursor(document());
                if (event
                    && event->modifiers().testFlag(Qt::ShiftModifier)) {
                    corrected.setPosition(originalAnchor);
                    corrected.setPosition(correctedPosition,
                                          QTextCursor::KeepAnchor);
                } else {
                    corrected.setPosition(correctedPosition);
                }
                QPlainTextEdit::setTextCursor(corrected);
            }
        }
        state->projection.ensureCursorVisible(this, false);
        viewport()->update();
    }
    lifecycleTrace("key.after-base");
}

void MyCodeEditor::inputMethodEvent(QInputMethodEvent* event)
{
    auto edit = beginSynchronousEditTransaction();
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    const int previousProjectedScroll = state->projection.active()
        ? verticalScrollBar()->value() : -1;
    if (event
        && (!event->commitString().isEmpty()
            || !event->preeditString().isEmpty())) {
        state->prepareVirtualCursorInput(this);
    }
    QPlainTextEdit::inputMethodEvent(event);
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()) {
        if (previousProjectedScroll >= 0)
            verticalScrollBar()->setValue(previousProjectedScroll);
        state->projection.ensureCursorVisible(this, false);
        viewport()->update();
    }
}

void MyCodeEditor::startFoldRegionMarkMode()
{
    state->folding.startFoldRegionMarkMode(this);
    state->gutter.handleUpdateRequest(
        this,
        viewport()->rect(),
        0);
}

void MyCodeEditor::cancelFoldRegionMarkMode()
{
    state->folding.cancelFoldRegionMarkMode(this);
    state->gutter.handleUpdateRequest(
        this,
        viewport()->rect(),
        0);
}

bool MyCodeEditor::foldRegionMarkModeActive() const
{
    return state->folding.foldRegionMarkModeActive();
}

void MyCodeEditor::startFoldShelfMode()
{
    state->folding.startFoldShelfMode(this);
    state->gutter.handleUpdateRequest(
        this,
        viewport()->rect(),
        0);
}

void MyCodeEditor::cancelFoldShelfMode()
{
    state->folding.cancelFoldShelfMode(this);
    state->gutter.handleUpdateRequest(
        this,
        viewport()->rect(),
        0);
}

bool MyCodeEditor::foldShelfModeActive() const
{
    return state->folding.foldShelfModeActive();
}

bool MyCodeEditor::insertCustomFoldMarkersForTest(
    int startLine,
    int endLine,
    const QString& alias)
{
    auto edit = beginSynchronousEditTransaction();
    return state->folding.insertCustomFoldMarkers(
        this,
        startLine,
        endLine,
        alias);
}

bool MyCodeEditor::toggleFoldAtLineForTest(int line)
{
    state->clearPendingColumnAnchor();
    return state->folding.toggleFoldAtLine(this, line);
}

bool MyCodeEditor::foldCollapsedAtLineForTest(int line) const
{
    return state->folding.isCollapsedAtLine(line);
}

bool MyCodeEditor::foldLineVisibleForTest(int line) const
{
    return sourceLineVisible(line);
}

bool MyCodeEditor::sourceLineVisible(int line) const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.sourceLineVisible(line);
}

bool MyCodeEditor::viewProjectionActive() const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.active();
}

void MyCodeEditor::invalidateViewProjection()
{
    state->projection.invalidate();
}

EditorViewProjectionMetrics MyCodeEditor::viewProjectionMetricsForTest() const
{
    auto* editor = const_cast<MyCodeEditor*>(this);
    state->projection.ensure(editor, state->folding.collapsedLineRanges());
    return state->projection.metrics();
}

EditorFoldViewState MyCodeEditor::foldingViewState() const
{
    return state->folding.captureViewState(
        const_cast<MyCodeEditor*>(this));
}

void MyCodeEditor::restoreFoldingViewState(
    const EditorFoldViewState& foldingState)
{
    state->clearPendingColumnAnchor();
    state->folding.restoreViewState(this, foldingState);
}

QList<GhostAnnotation> MyCodeEditor::ghostAnnotationsForTest() const
{
    return state->ghostAnnotationsForTest();
}

AnnotationLayerReport MyCodeEditor::annotationLayerReportForTest(
    const AnnotationLayerQuery& query) const
{
    return state->annotationLayerReportForTest(query);
}

QString MyCodeEditor::syntaxTextForTest() const
{
    return state->syntaxTextForTest();
}

EditorLargeFileSyntaxSnapshot
MyCodeEditor::largeFileSyntaxSnapshotForTest() const
{
    return state->largeFileSyntaxSnapshotForTest();
}

FoldShelfItem MyCodeEditor::foldShelfItemAtLineForTest(
    int line,
    FoldShelfOriginKind origin) const
{
    return state->folding.foldShelfItemAtLine(
        const_cast<MyCodeEditor*>(this),
        line,
        origin);
}

bool MyCodeEditor::deleteCustomFoldAtLineForTest(int line)
{
    auto edit = beginSynchronousEditTransaction();
    return state->folding.deleteCustomFoldAtLine(
        this,
        line);
}

bool MyCodeEditor::insertFoldShelfItemAtLineForTest(
    const FoldShelfItem& item,
    int line)
{
    auto edit = beginSynchronousEditTransaction();
    return state->folding.insertShelfItemAtLine(
        this,
        item,
        line);
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (state->handleKeyRelease(this, event))
        return;

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::dragEnterEvent(QDragEnterEvent* event)
{
    if (state->handleDragEnter(this, event))
        return;

    QPlainTextEdit::dragEnterEvent(event);
}

void MyCodeEditor::dragMoveEvent(QDragMoveEvent* event)
{
    if (state->handleDragMove(this, event))
        return;

    QPlainTextEdit::dragMoveEvent(event);
}

void MyCodeEditor::dropEvent(QDropEvent* event)
{
    auto edit = beginSynchronousEditTransaction();
    if (state->handleDrop(this, event))
        return;

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()
        && event
        && event->mimeData()
        && event->mimeData()->hasText()
        && !isReadOnly()) {
        if (event->proposedAction() == Qt::MoveAction
            && (event->source() == this
                || event->source() == viewport())) {
            event->ignore();
            return;
        }
        QTextCursor cursor = cursorForPosition(
            event->position().toPoint());
        cursor.insertText(event->mimeData()->text());
        QPlainTextEdit::setTextCursor(cursor);
        state->projection.ensure(this, state->folding.collapsedLineRanges());
        state->projection.ensureCursorVisible(this, false);
        event->acceptProposedAction();
        viewport()->update();
        return;
    }

    QPlainTextEdit::dropEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    auto edit = beginSynchronousEditTransaction();
    if (event->button() == Qt::BackButton) {
        emit navigationBackRequested();
        event->accept();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        emit navigationForwardRequested();
        event->accept();
        return;
    }

    if (state->handleMousePress(this, event))
        return;

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()
        && event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        QTextCursor target = cursorForPosition(event->position().toPoint());
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            const int position = target.position();
            target.setPosition(textCursor().anchor());
            target.setPosition(position, QTextCursor::KeepAnchor);
        }
        QPlainTextEdit::setTextCursor(target);
        viewport()->update();
        event->accept();
        return;
    }

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (state->handleMouseDoubleClick(this, event))
        return;

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()
        && event->button() == Qt::LeftButton) {
        QTextCursor target = cursorForPosition(event->position().toPoint());
        target.select(QTextCursor::WordUnderCursor);
        QPlainTextEdit::setTextCursor(target);
        state->selections.activateCurrentSymbolReferences(this);
        viewport()->update();
        event->accept();
        return;
    }

    QPlainTextEdit::mouseDoubleClickEvent(event);
    if (event && event->button() == Qt::LeftButton)
        state->selections.activateCurrentSymbolReferences(this);
}

void MyCodeEditor::focusOutEvent(QFocusEvent* event)
{
    state->clearPendingColumnAnchor();
    QPlainTextEdit::focusOutEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    auto edit = beginSynchronousEditTransaction();
    if (state->handleMouseMove(this, event))
        return;

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active()
        && event->buttons().testFlag(Qt::LeftButton)) {
        QTextCursor target = cursorForPosition(event->position().toPoint());
        const int position = target.position();
        target.setPosition(textCursor().anchor());
        target.setPosition(position, QTextCursor::KeepAnchor);
        QPlainTextEdit::setTextCursor(target);
        viewport()->update();
        event->accept();
        return;
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::mouseReleaseEvent(QMouseEvent *event)
{
    auto edit = beginSynchronousEditTransaction();
    if (state->handleMouseRelease(this, event))
        return;

    if (viewProjectionActive()
        && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }

    QPlainTextEdit::mouseReleaseEvent(event);
}

void MyCodeEditor::wheelEvent(QWheelEvent* event)
{
    if (event
        && event->modifiers().testFlag(Qt::ControlModifier)
        && event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int steps = wheelNotchSteps(event);
        if (steps != 0)
            emit fontZoomRequested(steps);
        event->accept();
        return;
    }

    if (event
        && event->modifiers().testFlag(Qt::ControlModifier)
        && handleControlWheelFastScroll(this, event)) {
        return;
    }

    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active() && event) {
        const int steps = wheelNotchSteps(event);
        if (steps != 0) {
            QScrollBar* bar = verticalScrollBar();
            bar->setValue(bar->value() - steps * 3 * bar->singleStep());
        } else if (!event->pixelDelta().isNull()) {
            QScrollBar* bar = verticalScrollBar();
            const int lineHeight = qMax(1, fontMetrics().height());
            const int rows = event->pixelDelta().y() / lineHeight;
            bar->setValue(bar->value() - rows);
        }
        viewport()->update();
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->handleLeaveEvent(this);

    QPlainTextEdit::leaveEvent(event);
}

QVariant MyCodeEditor::inputMethodQuery(
    Qt::InputMethodQuery query) const
{
    if (viewProjectionActive()) {
        if (query == Qt::ImCursorRectangle)
            return cursorRect();
        if (query == Qt::ImAnchorRectangle) {
            QTextCursor anchor = textCursor();
            anchor.setPosition(anchor.anchor());
            return cursorRect(anchor);
        }
        if (query == Qt::ImInputItemClipRectangle)
            return viewport()->rect();
    }
    return QPlainTextEdit::inputMethodQuery(query);
}

void MyCodeEditor::scrollContentsBy(int dx, int dy)
{
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (!state->projection.active()) {
        QPlainTextEdit::scrollContentsBy(dx, dy);
        return;
    }
    Q_UNUSED(dx)
    viewport()->update();
    state->gutter.handleUpdateRequest(this, viewport()->rect(), 0);
    if (dy != 0) {
        state->refreshVisibleRegionPresentation(this);
        state->sourceNavigation.handleEditorScrolled(this, state->selections);
        state->sourceNavigation.syncMode();
    }
}

void MyCodeEditor::doSetTextCursor(const QTextCursor& cursor)
{
    QPlainTextEdit::doSetTextCursor(cursor);
    if (!state)
        return;
    state->projection.ensure(this, state->folding.collapsedLineRanges());
    if (state->projection.active())
        viewport()->update();
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    auto edit = beginSynchronousEditTransaction();
    state->applyLineNavigationTarget(this, target);
}
