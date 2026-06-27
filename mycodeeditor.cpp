#include "mycodeeditor.h"
#include "editorruntime.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"

#include <QCheckBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <memory>
#include <utility>

namespace {
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

void showFindDialog(MyCodeEditor* editor)
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
}

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    state->attachToEditor(this);
}

MyCodeEditor::~MyCodeEditor()
{
    state->shutdown();
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    state->refreshScopeAndCurrentLineHighlight(this);
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->setAlternateModeEnabled(enabled);
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

EditorBlockGeometry MyCodeEditor::blockGeometry(int blockNumber) const
{
    return state->blockGeometry(this, blockNumber);
}

qreal MyCodeEditor::documentHeightPx() const
{
    return state->documentHeightPx(this);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->handleResize(this);
}

void MyCodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    state->paintFoldPlaceholders(this, event);
    state->paintGhostAnnotations(this, event);
    state->paintColumnSelection(this, event);
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
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

bool MyCodeEditor::executeComPortAppend(QString* message)
{
    return state->executeComPortAppend(this, message);
}

bool MyCodeEditor::executeComSignalInsert(QString* message)
{
    return state->executeComSignalInsert(this, message);
}

bool MyCodeEditor::executeComInstanceInsert(QString* message)
{
    return state->executeComInstanceInsert(this, message);
}

bool MyCodeEditor::executeComAssignInsert(QString* message)
{
    return state->executeComAssignInsert(this, message);
}

bool MyCodeEditor::executeComParameterInsert(QString* message)
{
    return state->executeComParameterInsert(this, message);
}

bool MyCodeEditor::executeComModuleEndInsert(QString* message)
{
    return state->executeComModuleEndInsert(this, message);
}

bool MyCodeEditor::comModeActive() const
{
    return state->comModeActive();
}

QString MyCodeEditor::comModeBuffer() const
{
    return state->comModeBuffer();
}

void MyCodeEditor::enterComMode(const QString& message)
{
    state->enterComMode(this, message);
}

void MyCodeEditor::exitComMode()
{
    state->exitComMode(this);
}

void MyCodeEditor::showComModeMessage(const QString& message)
{
    state->showComModeMessage(this, message);
}

void MyCodeEditor::setDiagnosticHighlights(
    const QList<SemanticDiagnostic>& diagnostics)
{
    state->setDiagnosticHighlights(this, diagnostics);
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

void MyCodeEditor::setFormatOnSaveEnabled(bool enabled)
{
    if (state->formatOnSaveEnabled() == enabled)
        return;
    state->setFormatOnSaveEnabled(enabled);
    emit formatOnSaveChanged(enabled);
}

bool MyCodeEditor::formatOnSaveEnabled() const
{
    return state->formatOnSaveEnabled();
}

bool MyCodeEditor::formatDocumentForSave()
{
    return state->formatDocumentForSave(this);
}

void MyCodeEditor::formatDocument()
{
    state->formatDocument(this);
}

void MyCodeEditor::formatSelection()
{
    state->formatSelection(this);
}

void MyCodeEditor::commentSelectionOrLine()
{
    state->commentSelectionOrLine(this);
}

void MyCodeEditor::uncommentSelectionOrLine()
{
    state->uncommentSelectionOrLine(this);
}

void MyCodeEditor::indentSelectionOrLine()
{
    state->indentSelectionOrLine(this);
}

void MyCodeEditor::unindentSelectionOrLine()
{
    state->unindentSelectionOrLine(this);
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
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Find)) {
        showFindDialog(this);
        event->accept();
        return;
    }

    if (state->handleKeyPress(this, event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MyCodeEditor::executeAlternateModeCommand(const QString& command)
{
    state->executeAlternateModeCommand(command);
}

void MyCodeEditor::startFoldRegionMarkMode()
{
    state->startFoldRegionMarkMode(this);
}

void MyCodeEditor::cancelFoldRegionMarkMode()
{
    state->cancelFoldRegionMarkMode(this);
}

bool MyCodeEditor::foldRegionMarkModeActive() const
{
    return state->foldRegionMarkModeActive();
}

void MyCodeEditor::startFoldShelfMode()
{
    state->startFoldShelfMode(this);
}

void MyCodeEditor::cancelFoldShelfMode()
{
    state->cancelFoldShelfMode(this);
}

bool MyCodeEditor::foldShelfModeActive() const
{
    return state->foldShelfModeActive();
}

bool MyCodeEditor::insertCustomFoldMarkersForTest(
    int startLine,
    int endLine,
    const QString& alias)
{
    return state->insertCustomFoldMarkers(this, startLine, endLine, alias);
}

FoldShelfItem MyCodeEditor::foldShelfItemAtLineForTest(
    int line,
    FoldShelfOriginKind origin) const
{
    return state->foldShelfItemAtLine(const_cast<MyCodeEditor*>(this), line, origin);
}

bool MyCodeEditor::deleteCustomFoldAtLineForTest(int line)
{
    return state->deleteCustomFoldAtLine(this, line);
}

bool MyCodeEditor::insertFoldShelfItemAtLineForTest(
    const FoldShelfItem& item,
    int line)
{
    return state->insertFoldShelfItemAtLine(this, item, line);
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
    if (state->handleDrop(this, event))
        return;

    QPlainTextEdit::dropEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
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

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (state->handleMouseDoubleClick(this, event))
        return;

    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (state->handleMouseMove(this, event))
        return;

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (state->handleMouseRelease(this, event))
        return;

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

    QPlainTextEdit::wheelEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->handleLeaveEvent(this);

    QPlainTextEdit::leaveEvent(event);
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    state->applyLineNavigationTarget(this, target);
}
