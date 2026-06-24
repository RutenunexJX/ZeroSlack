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
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <memory>

namespace {
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
    state->setFormatterProfile(profile);
}

FormatterProfile MyCodeEditor::formatterProfile() const
{
    return state->formatterProfile();
}

void MyCodeEditor::formatDocument()
{
    state->formatDocument(this);
}

void MyCodeEditor::formatSelection()
{
    state->formatSelection(this);
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

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (state->handleMouseMove(this, event))
        return;

    QPlainTextEdit::mouseMoveEvent(event);
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
