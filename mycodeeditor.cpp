#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "completionmodel.h"
#include "editorsemanticcontextservice.h"
#include "sourcenavigationservice.h"
#include "tsdocument.h"

#include "syminfo.h"

#include <QPainter>
#include <QScrollBar>
#include <QFileInfo>
#include <QDir>

#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextCursor>
#include <QApplication>
#include <QRect>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <QAbstractItemView>
#include <QCompleter>
#include <QCursor>
#include <QPixmap>
#include <QPen>
#include <QBrush>
#include <memory>

namespace {
constexpr int kPrimarySelectionProperty = QTextFormat::UserProperty;
constexpr int kScopeBackgroundSelectionMarker = 997;
constexpr int kCurrentLineSelectionMarker = 998;
constexpr int kCommandSelectionProperty = kPrimarySelectionProperty;
constexpr int kCommandSelectionMarker = 999;
constexpr int kHoveredSymbolSelectionProperty = QTextFormat::UserProperty + 1;
constexpr int kHoveredSymbolSelectionMarker = 1001;

void removeSelectionsByProperty(
    QList<QTextEdit::ExtraSelection>& selections,
    int property,
    int value)
{
    selections.erase(
        std::remove_if(selections.begin(), selections.end(),
            [property, value](const QTextEdit::ExtraSelection& selection) {
                return selection.format.property(property).toInt() == value;
            }),
        selections.end());
}

QList<QTextEdit::ExtraSelection> editorSelectionsWithout(
    QPlainTextEdit* editor,
    int property,
    int value)
{
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    removeSelectionsByProperty(selections, property, value);
    return selections;
}
}

class LineNumberWidget : public QWidget
{
public:
    explicit LineNumberWidget(
        MyCodeEditor *editor = nullptr,
        MyCodeEditorState *editorState = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    MyCodeEditor *codeEditor = nullptr;
    MyCodeEditorState *state = nullptr;
};

struct MyCodeEditorState
{
    struct EditorAppearance {
        void apply(MyCodeEditor* editor) const
        {
            editor->setFont(QFont("Consolas", 14));
            const int tabWidth =
                editor->fontMetrics().horizontalAdvance(' ') * 4;
            editor->setTabStopDistance(tabWidth);
            editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        }
    } appearance;

    struct GutterUi {
        LineNumberWidget *widget = nullptr;

        void init(MyCodeEditor* editor, MyCodeEditorState* state)
        {
            widget = new LineNumberWidget(editor, state);
        }

        void destroy()
        {
            delete widget;
            widget = nullptr;
        }

        int widthFor(MyCodeEditor* editor) const
        {
            return 8
                + QString::number(editor->blockCount() + 1).length()
                    * editor->fontMetrics().horizontalAdvance(QChar('0'));
        }

        void refresh(const QRect& rect, int dy, int width) const
        {
            if (!widget)
                return;

            if (dy)
                widget->scroll(0, dy);
            else
                widget->update(0, rect.y(), width, rect.height());
        }

        void handleUpdateRequest(
            MyCodeEditor* editor,
            const QRect& rect,
            int dy) const
        {
            refresh(rect, dy, widthFor(editor));
        }

        void updateViewportMargins(MyCodeEditor* editor) const
        {
            editor->setViewportMargins(widthFor(editor), 0, 0, 0);
        }

        void resizeTo(MyCodeEditor* editor, const QRect& contentsRect) const
        {
            if (widget)
                widget->setGeometry(
                    0,
                    0,
                    widthFor(editor),
                    contentsRect.height());
        }

        void paint(MyCodeEditor* editor, QPaintEvent *event) const
        {
            QPainter painter(widget);
            painter.fillRect(event->rect(), QColor(100, 100, 100, 20));

            QTextBlock block = editor->firstVisibleBlock();
            int blockNumber = block.blockNumber();
            const int cursorTop = editor->blockBoundingGeometry(
                editor->textCursor().block()).translated(
                    editor->contentOffset()).top();
            int top = editor->blockBoundingGeometry(block).translated(
                editor->contentOffset()).top();
            int bottom = top + editor->blockBoundingRect(block).height();

            while (block.isValid() && top <= event->rect().bottom()) {
                painter.setPen(cursorTop == top ? Qt::black : Qt::gray);
                painter.drawText(
                    0,
                    top,
                    widthFor(editor) - 3,
                    bottom - top,
                    Qt::AlignRight,
                    QString::number(blockNumber + 1));

                block = block.next();
                top = bottom;
                bottom = top + editor->blockBoundingRect(block).height();
                blockNumber++;
            }
        }

        void handleMousePress(MyCodeEditor* editor, QMouseEvent *event) const
        {
            QTextBlock block = editor->document()->findBlockByLineNumber(
                static_cast<int>(event->position().y())
                    / editor->fontMetrics().height()
                + editor->verticalScrollBar()->value());
            editor->setTextCursor(QTextCursor(block));
        }

        void handleWheel(MyCodeEditor* editor, QWheelEvent *event) const
        {
            const QPoint angle = event->angleDelta();
            if (!angle.isNull()) {
                const int dy = angle.y();
                const int dx = angle.x();
                if (dy != 0) {
                    QScrollBar* bar = editor->verticalScrollBar();
                    bar->setValue(bar->value() - dy);
                } else if (dx != 0) {
                    QScrollBar* bar = editor->horizontalScrollBar();
                    bar->setValue(bar->value() - dx);
                }
            }

            event->accept();
        }
    } gutter;

    struct DocumentGeometry {
        EditorBlockGeometry blockGeometry(
            const MyCodeEditor* editor,
            int blockNumber) const
        {
            QTextBlock block = editor->document()->findBlockByNumber(blockNumber);
            if (!block.isValid())
                return {0, qreal(editor->fontMetrics().height())};

            const qreal top = editor->blockBoundingGeometry(block).translated(
                editor->contentOffset()).top();
            return {top, editor->blockBoundingRect(block).height()};
        }

        qreal documentHeightPx(const MyCodeEditor* editor) const
        {
            QAbstractTextDocumentLayout* layout =
                editor->document()->documentLayout();
            return layout ? layout->documentSize().height() : 0;
        }
    } geometry;

    struct CursorNavigation {
        void moveMouseToCursor(MyCodeEditor* editor) const
        {
            if (editor->viewport() && editor->viewport()->isVisible()) {
                QCursor::setPos(
                    editor->viewport()->mapToGlobal(
                        editor->cursorRect().center()));
            }
        }

        void applyLineTarget(
            MyCodeEditor* editor,
            const SourceLineNavigationTarget& target) const
        {
            QTextCursor cursor = editor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            for (int i = 0; i < target.lineMoves; ++i)
                cursor.movePosition(QTextCursor::Down);
            if (target.columnMoves > 0) {
                cursor.movePosition(
                    QTextCursor::Right,
                    QTextCursor::MoveAnchor,
                    target.columnMoves);
            }
            editor->setTextCursor(cursor);
            editor->centerCursor();
            editor->setFocus();
            moveMouseToCursor(editor);
        }
    } cursorNavigation;

    struct SyntaxTreeState {
        std::unique_ptr<TSDocument> document;
        MyHighlighter *highlighter = nullptr;

        void init()
        {
            document = std::make_unique<TSDocument>();
        }

        void syncText(const QString& text)
        {
            document->setText(text);
        }

        void createHighlighter(QTextDocument* textDocument)
        {
            highlighter = new MyHighlighter(textDocument, document.get());
        }

        void attachToEditor(MyCodeEditor* editor)
        {
            syncText(editor->document()->toPlainText());
            QObject::connect(
                editor->document(),
                &QTextDocument::contentsChange,
                editor,
                [this, editor](int position,
                               int charsRemoved,
                               int charsAdded) {
                    applyEdit(position,
                              charsRemoved,
                              charsAdded,
                              editor->document()->toPlainText());
                });
            createHighlighter(editor->document());
        }

        void applyEdit(int position,
                       int charsRemoved,
                       int charsAdded,
                       const QString& text)
        {
            document->applyEditChars(position,
                                     position + charsRemoved,
                                     position + charsAdded,
                                     text);
        }

        QString moduleNameAt(int charPos) const
        {
            return document->enclosingModuleName(charPos < 0 ? 0 : charPos);
        }
    } syntax;

    struct FileIdentity {
        QString fileName;

        static QString normalized(QString fileName)
        {
            return fileName.isEmpty()
                ? QString()
                : QDir::cleanPath(QDir::fromNativeSeparators(
                    QFileInfo(fileName).absoluteFilePath()));
        }

        bool set(QString nextFileName)
        {
            const QString normalizedFileName = normalized(nextFileName);
            if (fileName == normalizedFileName)
                return false;

            fileName = normalizedFileName;
            return true;
        }
    } identity;

    struct SemanticRuntime {
        EditorSemanticContextService* service = nullptr;

        void init()
        {
            service = EditorSemanticContextService::getInstance();
        }

        void setService(EditorSemanticContextService* nextService)
        {
            service = nextService
                ? nextService
                : EditorSemanticContextService::getInstance();
        }

        EditorSemanticContextService* contextService() const
        {
            return service
                ? service
                : EditorSemanticContextService::getInstance();
        }

        EditorSemanticContext contextForDocument(
            QTextDocument* document,
            const QString& fileName,
            const QString& moduleName,
            int cursorPosition,
            bool includeDocumentText) const
        {
            EditorSemanticContext context;
            context.fileName = fileName;
            context.moduleName = moduleName;
            if (includeDocumentText)
                context.documentText = document->toPlainText();
            context.cursorPosition = cursorPosition;

            const QTextBlock block = document->findBlock(cursorPosition);
            if (block.isValid()) {
                context.lineText = block.text();
                context.column = cursorPosition - block.position();
                context.cursorLine = block.blockNumber() + 1;
                context.lineUpToCursor = context.lineText.left(context.column);
            }

            return context;
        }
    } semantic;

    struct ModeState {
        bool commandModeActive = false;
        bool alternateModeActive = false;
        QString alternateBuffer;
        bool commandModeExitedByDoubleSpace = false;
        int completionTimerLineNumber = -1;

        void setAlternateModeEnabled(bool enabled)
        {
            alternateModeActive = enabled;
        }

        void setCommandModeActive(bool active)
        {
            commandModeActive = active;
        }

        void noteCompletionTimerLine(int lineNumber)
        {
            if (completionTimerLineNumber == lineNumber)
                return;

            commandModeExitedByDoubleSpace = false;
            completionTimerLineNumber = lineNumber;
        }

        void markCommandModeExitedByDoubleSpace()
        {
            commandModeExitedByDoubleSpace = true;
        }

        void resetCommandModeExit()
        {
            commandModeExitedByDoubleSpace = false;
        }

        void clearCommandMode()
        {
            commandModeActive = false;
        }

        void setAlternateBuffer(const QString& input)
        {
            alternateBuffer = input;
        }

        QString alternateBufferWithoutLastChar() const
        {
            return alternateBuffer.left(alternateBuffer.size() - 1);
        }

        void clearAlternateBuffer()
        {
            alternateBuffer.clear();
        }
    } modes;

    struct CompletionUi {
        QCompleter *completer = nullptr;
        CompletionModel *model = nullptr;
        QTimer *timer = nullptr;
        int wordStartPos = 0;

        void init(MyCodeEditor* editor)
        {
            model = new CompletionModel(editor);
            completer = new QCompleter(editor);
            completer->setModel(model);
            completer->setWidget(editor);
            completer->setCompletionMode(QCompleter::PopupCompletion);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setMaxVisibleItems(15);
            timer = new QTimer(editor);
            timer->setSingleShot(true);
            timer->setInterval(0);
        }

        void attachToEditor(MyCodeEditor* editor, MyCodeEditorState* state)
        {
            init(editor);
            QObject::connect(
                timer,
                &QTimer::timeout,
                editor,
                [state, editor]() {
                    state->handleAutoCompleteTimer(editor);
                });
            QObject::connect(
                completer,
                QOverload<const QModelIndex &>::of(&QCompleter::activated),
                editor,
                [state, editor](const QModelIndex& index) {
                    state->handleCompletionActivated(editor, index);
                });
            QObject::connect(
                editor,
                &QPlainTextEdit::textChanged,
                editor,
                [state, editor]() {
                    state->handleTextChanged(editor);
                });
        }

        QAbstractItemView* popup() const { return completer->popup(); }
        bool popupVisible() const { return popup()->isVisible(); }
        void hidePopup() const { popup()->hide(); }
        void startTimer() const { timer->start(); }
        void stopTimer() const { timer->stop(); }
        int rowCount() const { return model->rowCount(); }
        bool hasRows() const { return rowCount() > 0; }
        QModelIndex currentIndex() const { return popup()->currentIndex(); }
        QModelIndex firstSelectableIndex() const
        {
            return model->firstSelectableIndex();
        }
        void activateIndex(const QModelIndex& index) const
        {
            emit completer->activated(index);
        }

        EditorCompletionActivationContext activationContextForIndex(
            const QModelIndex& index,
            const ModeState& modes) const
        {
            const CompletionModel::CompletionItem item = model->getItem(index);
            EditorCompletionActivationContext context;
            context.selectable = model->isSelectableIndex(index);
            context.alternateModeActive = modes.alternateModeActive;
            context.commandModeActive = modes.commandModeActive;
            context.itemText = item.text;
            context.defaultValue = item.defaultValue;
            return context;
        }

        EditorCompletionPopupKeyContext popupKeyContextForEvent(
            QKeyEvent *event,
            const ModeState& modes) const
        {
            EditorCompletionPopupKeyContext context;
            context.key = event->key();
            context.alternateModeActive = modes.alternateModeActive;
            context.commandModeActive = modes.commandModeActive;
            context.currentIndexValid = currentIndex().isValid();
            context.hasRows = hasRows();
            context.alternateBufferEmpty = modes.alternateBuffer.isEmpty();
            return context;
        }

        void updateCommandModeCompletions(
            const EditorCommandModeCompletionRefreshState& commandState) const
        {
            model->updateSymbolCompletions(
                commandState.completion.symbols,
                commandState.completion.completionPrefix,
                commandState.completion.command.symbolType);
        }

        void updateSymbolCompletions(
            const EditorCompletionState& completionState) const
        {
            model->updateCompletions(
                completionState.completion.names,
                completionState.completion.symbols,
                completionState.prefix,
                CompletionModel::SymbolCompletion);
        }

        void updateAlternateModeCompletions(
            const EditorAlternateModeCompletionDisplayState& displayState) const
        {
            model->updateCommandCompletions(
                displayState.matches,
                displayState.normalizedInput);
        }

        void setReplacementStart(
            int blockPosition,
            int replacementStartColumn)
        {
            wordStartPos = blockPosition + replacementStartColumn;
        }

        QString wordUnderCursor(MyCodeEditor* editor)
        {
            QTextCursor cursor = editor->textCursor();
            const int currentPosition = cursor.position();
            cursor.movePosition(QTextCursor::StartOfWord);
            wordStartPos = cursor.position();
            cursor.setPosition(currentPosition);
            cursor.movePosition(QTextCursor::EndOfWord);
            cursor.setPosition(wordStartPos);
            cursor.setPosition(currentPosition, QTextCursor::KeepAnchor);
            return cursor.selectedText();
        }

        void replaceWordAtCursor(MyCodeEditor* editor, const QString& text) const
        {
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(wordStartPos);
            cursor.setPosition(
                editor->textCursor().position(),
                QTextCursor::KeepAnchor);
            cursor.insertText(text);
        }

        void showForCursor(const QRect& cursorRectangle,
                           bool selectFirstCompletion) const
        {
            if (!hasRows())
                return;

            QRect popupRectangle = cursorRectangle;
            popupRectangle.setWidth(popup()->sizeHintForColumn(0) + 20);
            if (selectFirstCompletion) {
                const QModelIndex selectableIndex = firstSelectableIndex();
                if (selectableIndex.isValid())
                    popup()->setCurrentIndex(selectableIndex);
            }

            completer->complete(popupRectangle);
        }
    } completion;

    struct HighlightRefresh {
        QTimer *timer = nullptr;

        void init(MyCodeEditor* editor)
        {
            timer = new QTimer(editor);
            timer->setSingleShot(true);
        }

        void schedule() const
        {
            timer->start(0);
        }

        void attachToEditor(MyCodeEditor* editor, MyCodeEditorState* state)
        {
            init(editor);
            QObject::connect(
                timer,
                &QTimer::timeout,
                editor,
                [state, editor]() {
                    state->selections.highlightCurrentLine(editor);
                });

            auto scheduleHighlightRefresh = [this]() { schedule(); };
            QObject::connect(
                editor,
                &QPlainTextEdit::cursorPositionChanged,
                editor,
                scheduleHighlightRefresh);
            QObject::connect(
                editor,
                &QPlainTextEdit::textChanged,
                editor,
                scheduleHighlightRefresh);
        }
    } highlightRefresh;

    struct SourceNavigationHover {
        bool ctrlPressed = false;
        QString word;
        int startPos = -1;
        int endPos = -1;

        bool setCtrlPressed(bool pressed)
        {
            if (ctrlPressed == pressed)
                return false;

            ctrlPressed = pressed;
            return true;
        }

        bool isCtrlPressed() const
        {
            return ctrlPressed;
        }

        QCursor cursorForTarget(
            const EditorSourceNavigationTarget& target) const
        {
            return target.jumpable ? createJumpableCursor()
                                   : createNonJumpableCursor();
        }

        QCursor nonJumpableCursor() const
        {
            return createNonJumpableCursor();
        }

        bool matches(const EditorSourceNavigationTarget& target) const
        {
            return word == target.text
                && startPos == target.startPos
                && endPos == target.endPos;
        }

        void setTarget(const EditorSourceNavigationTarget& target)
        {
            word = target.text;
            startPos = target.startPos;
            endPos = target.endPos;
        }

        void clearTarget()
        {
            word.clear();
            clearRange();
        }

        void clearRange()
        {
            startPos = -1;
            endPos = -1;
        }

    private:
        QCursor createJumpableCursor() const
        {
            QPixmap pixmap(24, 24);
            pixmap.fill(Qt::transparent);

            QPainter painter(&pixmap);
            if (!painter.isActive())
                return QCursor(Qt::PointingHandCursor);

            painter.setRenderHint(QPainter::Antialiasing);

            QPen pen(QColor(0, 255, 0), 4);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);

            painter.drawLine(7, 12, 11, 16);
            painter.drawLine(11, 16, 18, 6);

            painter.end();

            return QCursor(pixmap, 12, 12);
        }

        QCursor createNonJumpableCursor() const
        {
            QPixmap pixmap(20, 20);
            pixmap.fill(Qt::transparent);

            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);

            QPen pen(QColor(255, 0, 0), 3);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);

            painter.drawLine(5, 5, 15, 15);
            painter.drawLine(15, 5, 5, 15);

            return QCursor(pixmap, 10, 10);
        }
    } sourceHover;

    struct SelectionUi {
        void highlightCurrentLine(MyCodeEditor* editor)
        {
            QList<QTextEdit::ExtraSelection> selections =
                editor->extraSelections();
            selections.erase(
                std::remove_if(selections.begin(), selections.end(),
                    [](const QTextEdit::ExtraSelection& selection) {
                        const int property = selection.format
                            .property(kPrimarySelectionProperty)
                            .toInt();
                        return property == kScopeBackgroundSelectionMarker
                            || property == kCurrentLineSelectionMarker;
                    }),
                selections.end());

            QTextEdit::ExtraSelection currentLine;
            currentLine.format.setBackground(QColor(0, 100, 100, 20));
            currentLine.format.setProperty(
                QTextFormat::FullWidthSelection,
                true);
            currentLine.format.setProperty(
                kPrimarySelectionProperty,
                kCurrentLineSelectionMarker);
            currentLine.cursor = editor->textCursor();
            selections.append(currentLine);

            clampSelectionsToDocument(editor->document(), selections);
            editor->setExtraSelections(selections);
        }

        void removeByProperty(QPlainTextEdit* editor, int property, int value)
        {
            QList<QTextEdit::ExtraSelection> selections =
                editorSelectionsWithout(editor, property, value);
            editor->setExtraSelections(selections);
        }

        void highlightCommand(MyCodeEditor* editor, int prefixPosition)
        {
            if (prefixPosition < 0)
                return;

            QList<QTextEdit::ExtraSelection> selections =
                editorSelectionsWithout(
                    editor,
                    kCommandSelectionProperty,
                    kCommandSelectionMarker);

            QTextEdit::ExtraSelection commandSelection;
            commandSelection.format.setBackground(QColor(60, 60, 60, 180));
            commandSelection.format.setForeground(QColor(255, 255, 255));
            commandSelection.format.setProperty(
                kCommandSelectionProperty,
                kCommandSelectionMarker);

            QTextCursor commandCursor = editor->textCursor();
            const int commandStartPosition =
                commandCursor.block().position() + prefixPosition;
            commandCursor.setPosition(commandStartPosition);
            commandCursor.setPosition(
                editor->textCursor().position(),
                QTextCursor::KeepAnchor);
            commandSelection.cursor = commandCursor;

            selections.append(commandSelection);
            editor->setExtraSelections(selections);
        }

        void clearCommand(QPlainTextEdit* editor)
        {
            removeByProperty(
                editor,
                kCommandSelectionProperty,
                kCommandSelectionMarker);
        }

        void highlightHoveredSymbol(
            MyCodeEditor* editor,
            const EditorSourceNavigationTarget& target)
        {
            if (target.text.isEmpty()
                || target.startPos < 0
                || target.endPos <= target.startPos) {
                return;
            }

            QTextEdit::ExtraSelection highlight;
            highlight.cursor = editor->textCursor();
            highlight.cursor.setPosition(target.startPos);
            highlight.cursor.setPosition(
                target.endPos,
                QTextCursor::KeepAnchor);
            highlight.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            highlight.format.setUnderlineColor(QColor(0, 100, 200));
            highlight.format.setForeground(QColor(0, 100, 200));
            highlight.format.setProperty(
                kHoveredSymbolSelectionProperty,
                kHoveredSymbolSelectionMarker);

            QList<QTextEdit::ExtraSelection> selections =
                editorSelectionsWithout(
                    editor,
                    kHoveredSymbolSelectionProperty,
                    kHoveredSymbolSelectionMarker);
            selections.append(highlight);
            editor->setExtraSelections(selections);
        }

        void clearHoveredSymbol(
            QPlainTextEdit* editor,
            SourceNavigationHover& hover)
        {
            removeByProperty(
                editor,
                kHoveredSymbolSelectionProperty,
                kHoveredSymbolSelectionMarker);
            hover.clearRange();
        }

    private:
        void clampSelectionsToDocument(
            QTextDocument* document,
            QList<QTextEdit::ExtraSelection>& selections)
        {
            const int docLen = document->characterCount();
            const int docEnd = (docLen > 0) ? docLen - 1 : 0;
            for (auto& selection : selections) {
                QTextCursor& cursor = selection.cursor;
                const int pos = qBound(0, cursor.position(), docEnd);
                const int anchor = qBound(0, cursor.anchor(), docEnd);
                cursor.setPosition(anchor);
                cursor.setPosition(pos, QTextCursor::KeepAnchor);
            }
        }
    } selections;

    void initializeCore(MyCodeEditor* editor)
    {
        semantic.init();
        syntax.init();
        gutter.init(editor, this);
        identity.set(QString());
        editor->setMouseTracking(true);
    }

    void shutdown()
    {
        gutter.destroy();
    }

    void attachEditorConnections(MyCodeEditor* editor)
    {
        highlightRefresh.attachToEditor(editor, this);
        QObject::connect(
            editor,
            &QPlainTextEdit::blockCountChanged,
            editor,
            [this, editor]() {
                gutter.updateViewportMargins(editor);
            });
        QObject::connect(
            editor,
            &QPlainTextEdit::updateRequest,
            editor,
            [this, editor](const QRect& rect, int dy) {
                gutter.handleUpdateRequest(editor, rect, dy);
            });
    }

    void attachToEditor(MyCodeEditor* editor)
    {
        initializeCore(editor);
        attachEditorConnections(editor);
        appearance.apply(editor);
        syntax.attachToEditor(editor);
        completion.attachToEditor(editor, this);
        selections.highlightCurrentLine(editor);
        gutter.updateViewportMargins(editor);
    }

    EditorSemanticContextService* semanticService() const
    {
        return semantic.contextService();
    }

    QString currentModuleNameAt(int charPos) const
    {
        return syntax.moduleNameAt(charPos);
    }

    QString currentModuleName(const MyCodeEditor* editor) const
    {
        return currentModuleNameAt(editor->textCursor().position());
    }

    EditorSemanticContext semanticContextForPosition(
        const MyCodeEditor* editor,
        int cursorPosition,
        bool includeDocumentText) const
    {
        const int semanticPosition = cursorPosition >= 0
            ? cursorPosition
            : editor->textCursor().position();

        return semantic.contextForDocument(
            editor->document(),
            identity.fileName,
            currentModuleNameAt(semanticPosition),
            semanticPosition,
            includeDocumentText);
    }

    EditorSemanticContext semanticContextForCursor(
        const MyCodeEditor* editor,
        const QTextCursor& cursor,
        bool includeDocumentText) const
    {
        return semanticContextForPosition(
            editor,
            cursor.position(),
            includeDocumentText);
    }

    void hideAutoComplete(MyCodeEditor* editor)
    {
        completion.hidePopup();

        if (modes.commandModeActive)
            selections.clearCommand(editor);
    }

    void showAutoComplete(MyCodeEditor* editor)
    {
        completion.showForCursor(
            editor->cursorRect(editor->textCursor()),
            modes.commandModeActive);
    }

    void applyAlternateModeCompletionDisplayState(
        MyCodeEditor* editor,
        const EditorAlternateModeCompletionDisplayState& displayState)
    {
        if (!displayState.updateCompletions)
            return;

        modes.setAlternateBuffer(displayState.normalizedInput);
        completion.updateAlternateModeCompletions(displayState);

        if (displayState.showPopup)
            showAutoComplete(editor);
    }

    void processAlternateModeInput(MyCodeEditor* editor, const QString& input)
    {
        if (!modes.alternateModeActive)
            return;

        const EditorAlternateModeCompletionDisplayState completionState =
            semanticService()->alternateModeCompletionDisplayState(input);
        applyAlternateModeCompletionDisplayState(editor, completionState);
    }

    void executeAlternateModeCommand(
        MyCodeEditor* editor,
        const QString& command)
    {
        if (!command.trimmed().isEmpty())
            emit editor->alternateCommandRequested(command);
        modes.clearAlternateBuffer();
        hideAutoComplete(editor);
    }

    void updateCompletionTriggerForTextChange(
        MyCodeEditor* editor,
        const QTextCursor& cursor)
    {
        EditorSemanticContext context =
            semanticContextForCursor(editor, cursor, false);
        context.moduleName = currentModuleNameAt(cursor.position() - 1);
        const EditorCompletionTextChangeState completionState =
            semanticService()->completionTextChangeState(context);
        modes.setCommandModeActive(completionState.commandModeActive);

        if (completionState.startCompletionTimer) {
            completion.startTimer();
        } else if (completionState.hidePopup) {
            hideAutoComplete(editor);
        }
    }

    void handleTextChanged(MyCodeEditor* editor)
    {
        completion.stopTimer();
        updateCompletionTriggerForTextChange(editor, editor->textCursor());
    }

    void applyCompletionActivationState(
        MyCodeEditor* editor,
        const CompletionActivationState& activationState)
    {
        if (activationState.action == CompletionActivationAction::None)
            return;

        QTextCursor cursor = editor->textCursor();

        if (activationState.action
            == CompletionActivationAction::ExecuteAlternateCommand) {
            executeAlternateModeCommand(editor, activationState.text);
            return;
        }

        if (activationState.action == CompletionActivationAction::ReplaceLine) {
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
            cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
            cursor.insertText(activationState.text);

            if (activationState.clearCommandMode) {
                modes.clearCommandMode();
                selections.clearCommand(editor);
            }
        } else if (activationState.action
                   == CompletionActivationAction::ReplaceWord) {
            completion.replaceWordAtCursor(editor, activationState.text);
        }

        if (activationState.hidePopup)
            hideAutoComplete(editor);
    }

    void handleCompletionActivated(
        MyCodeEditor* editor,
        const QModelIndex& index)
    {
        const EditorCompletionActivationContext activationContext =
            completion.activationContextForIndex(index, modes);
        const CompletionActivationState activationState =
            semanticService()->completionActivationState(activationContext);
        applyCompletionActivationState(editor, activationState);
    }

    bool handleSourceSymbolShortcut(MyCodeEditor* editor, QKeyEvent *event)
    {
        EditorSourceSymbolShortcutContext sourceShortcutContext;
        sourceShortcutContext.key = event->key();
        sourceShortcutContext.modifiers = int(event->modifiers());
        sourceShortcutContext.semanticContext =
            semanticContextForPosition(
                editor,
                editor->textCursor().position(),
                false);
        const EditorSourceSymbolShortcutState sourceShortcutState =
            semanticService()->sourceSymbolShortcutState(sourceShortcutContext);
        if (!sourceShortcutState.matched)
            return false;

        emit editor->sourceSymbolActionRequested(
            sourceShortcutState.action,
            sourceShortcutState.semanticContext);
        if (sourceShortcutState.acceptEvent)
            event->accept();
        return true;
    }

    void applyAlternateModeKeyState(
        MyCodeEditor* editor,
        const EditorAlternateModeKeyState& keyState)
    {
        switch (keyState.action) {
        case EditorAlternateModeKeyAction::UpdateInput:
        case EditorAlternateModeKeyAction::RefreshCompletions:
            applyAlternateModeCompletionDisplayState(
                editor,
                keyState.completion);
            break;
        case EditorAlternateModeKeyAction::ExecuteCommand:
            executeAlternateModeCommand(editor, keyState.command);
            break;
        case EditorAlternateModeKeyAction::ClearAndHide:
            if (keyState.hidePopup)
                hideAutoComplete(editor);
            if (keyState.clearBuffer)
                modes.clearAlternateBuffer();
            break;
        case EditorAlternateModeKeyAction::Consume:
            break;
        }
    }

    bool handleAlternateModeKey(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (handleCompletionPopupKey(editor, event))
            return true;

        EditorAlternateModeKeyContext alternateKeyContext;
        alternateKeyContext.key = event->key();
        alternateKeyContext.text = event->text();
        alternateKeyContext.buffer = modes.alternateBuffer;
        const EditorAlternateModeKeyState alternateKeyState =
            semanticService()->alternateModeKeyState(alternateKeyContext);
        applyAlternateModeKeyState(editor, alternateKeyState);
        return true;
    }

    bool handleCompletionPopupKey(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (!completion.popupVisible())
            return false;

        const CompletionPopupKeyState popupState =
            semanticService()->completionPopupKeyState(
                completion.popupKeyContextForEvent(event, modes));
        return applyCompletionPopupKeyState(editor, event, popupState);
    }

    bool applyCompletionPopupKeyState(
        MyCodeEditor* editor,
        QKeyEvent *event,
        const CompletionPopupKeyState& popupState)
    {
        switch (popupState.action) {
        case CompletionPopupKeyAction::ForwardToPopup:
            QApplication::sendEvent(completion.popup(), event);
            return true;
        case CompletionPopupKeyAction::ActivateCurrent:
            if (completion.currentIndex().isValid())
                completion.activateIndex(completion.currentIndex());
            return true;
        case CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable:
            {
                QModelIndex currentIndex = completion.currentIndex();
                if (!currentIndex.isValid() && completion.hasRows())
                    currentIndex = completion.firstSelectableIndex();
                if (currentIndex.isValid())
                    completion.activateIndex(currentIndex);
            }
            return true;
        case CompletionPopupKeyAction::HidePopup:
            hideAutoComplete(editor);
            return true;
        case CompletionPopupKeyAction::HidePopupAndClearAlternate:
            hideAutoComplete(editor);
            modes.clearAlternateBuffer();
            return true;
        case CompletionPopupKeyAction::BackspaceAlternateInput:
            if (!modes.alternateBuffer.isEmpty()) {
                const EditorAlternateModeCompletionDisplayState completionState =
                    semanticService()->alternateModeCompletionDisplayState(
                        modes.alternateBufferWithoutLastChar());
                applyAlternateModeCompletionDisplayState(
                    editor,
                    completionState);
            } else {
                hideAutoComplete(editor);
            }
            return true;
        case CompletionPopupKeyAction::Consume:
            return true;
        case CompletionPopupKeyAction::None:
            return false;
        }

        return false;
    }

    bool refreshCommandModeCompletion(
        MyCodeEditor* editor,
        const EditorSemanticContext& context)
    {
        const EditorCommandModeCompletionRefreshState commandState =
            semanticService()->commandModeCompletionRefreshState(
                context,
                modes.commandModeExitedByDoubleSpace);
        if (commandState.matched) {
            modes.setCommandModeActive(commandState.commandModeActive);
            if (commandState.suppressAfterExit)
                return true;

            if (commandState.exitRequested) {
                if (commandState.clearCommandHighlight)
                    selections.clearCommand(editor);
                if (commandState.markExitedByDoubleSpace)
                    modes.markCommandModeExitedByDoubleSpace();
                if (completion.popupVisible())
                    completion.hidePopup();
                return true;
            }

            if (commandState.highlightCommand)
                selections.highlightCommand(
                    editor,
                    commandState.completion.prefixPosition);

            if (commandState.hidePopup) {
                if (completion.popupVisible())
                    completion.hidePopup();
                return true;
            }

            if (commandState.showCompletions) {
                completion.updateCommandModeCompletions(commandState);
                showAutoComplete(editor);
            }
            return true;
        }

        if (commandState.resetExitedByDoubleSpace)
            modes.resetCommandModeExit();

        selections.clearCommand(editor);
        modes.clearCommandMode();

        return false;
    }

    void refreshSymbolCompletion(
        MyCodeEditor* editor,
        EditorSemanticContext context,
        const QTextBlock& currentBlock)
    {
        context.wordPrefix = completion.wordUnderCursor(editor);
        const EditorCompletionState completionState =
            semanticService()->editorCompletionState(context);
        if (completionState.available) {
            completion.updateSymbolCompletions(completionState);
            completion.setReplacementStart(
                currentBlock.position(),
                completionState.replacementStartColumn);
            showAutoComplete(editor);
        }
    }

    void handleAutoCompleteTimer(MyCodeEditor* editor)
    {
        const QTextCursor cursor = editor->textCursor();
        const QTextBlock currentBlock = cursor.block();

        modes.noteCompletionTimerLine(currentBlock.blockNumber());

        EditorSemanticContext context =
            semanticContextForCursor(editor, cursor, true);

        if (refreshCommandModeCompletion(editor, context))
            return;

        if (modes.alternateModeActive) {
            processAlternateModeInput(editor, context.lineUpToCursor);
            return;
        }

        refreshSymbolCompletion(editor, context, currentBlock);
    }

    EditorSourceNavigationTarget sourceNavigationTargetAtPosition(
        MyCodeEditor* editor,
        const QPoint& position) const
    {
        QTextCursor cursor = editor->cursorForPosition(position);
        QTextBlock block = cursor.block();
        if (!block.isValid())
            return {};

        return semanticService()->editorSourceNavigationTarget(
            semanticContextForPosition(editor, cursor.position(), false),
            block.position());
    }

    bool requestSourceNavigationAtPosition(
        MyCodeEditor* editor,
        const QPoint& position) const
    {
        const EditorSourceNavigationTarget target =
            sourceNavigationTargetAtPosition(editor, position);
        emit editor->sourceNavigationRequested(
            target,
            semanticContextForPosition(editor, target.cursorPosition, false));
        return target.matched && !target.text.isEmpty();
    }

    void refreshSourceNavigationHoverAt(
        MyCodeEditor* editor,
        const QPoint& position)
    {
        applySourceNavigationHover(
            editor,
            sourceNavigationTargetAtPosition(editor, position));
    }

    void applySourceNavigationHover(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target)
    {
        if (!target.matched) {
            clearSourceNavigationHover(editor);
            editor->viewport()->setCursor(sourceHover.nonJumpableCursor());
            return;
        }

        if (!sourceHover.matches(target)) {
            selections.clearHoveredSymbol(editor, sourceHover);
            sourceHover.setTarget(target);
            selections.highlightHoveredSymbol(editor, target);
        }

        editor->viewport()->setCursor(sourceHover.cursorForTarget(target));
    }

    void clearSourceNavigationHover(MyCodeEditor* editor)
    {
        editor->viewport()->setCursor(Qt::IBeamCursor);
        selections.clearHoveredSymbol(editor, sourceHover);
        sourceHover.clearTarget();
    }

    void handleControlKeyPress(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (event->key() != Qt::Key_Control
            || !sourceHover.setCtrlPressed(true)) {
            return;
        }

        const QPoint mousePos = editor->mapFromGlobal(QCursor::pos());
        if (editor->rect().contains(mousePos))
            refreshSourceNavigationHoverAt(editor, mousePos);
    }

    void handleControlKeyRelease(MyCodeEditor* editor, QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Control
            && sourceHover.setCtrlPressed(false)) {
            clearSourceNavigationHover(editor);
        }
    }

    bool handleSourceNavigationMousePress(
        MyCodeEditor* editor,
        QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton
            || !(event->modifiers() & Qt::ControlModifier)) {
            return false;
        }

        if (!requestSourceNavigationAtPosition(editor, event->pos()))
            return false;

        event->accept();
        return true;
    }

    void handleSourceNavigationMouseMove(
        MyCodeEditor* editor,
        QMouseEvent *event)
    {
        const bool isCtrlPressed =
            (event->modifiers() & Qt::ControlModifier);

        if (sourceHover.setCtrlPressed(isCtrlPressed)) {
            if (sourceHover.isCtrlPressed())
                refreshSourceNavigationHoverAt(editor, event->pos());
            else
                clearSourceNavigationHover(editor);
        } else if (sourceHover.isCtrlPressed()) {
            refreshSourceNavigationHoverAt(editor, event->pos());
        }
    }

    void handleLeave(MyCodeEditor* editor)
    {
        sourceHover.setCtrlPressed(false);
        clearSourceNavigationHover(editor);
    }

    void handleSourceSymbolContextMenu(
        MyCodeEditor* editor,
        QContextMenuEvent *event)
    {
        std::unique_ptr<QMenu> menu(
            editor->createStandardContextMenu(event->pos()));
        const QTextCursor cursorAtPos = editor->cursorForPosition(event->pos());
        emit editor->sourceSymbolContextMenuRequested(
            menu.get(),
            semanticContextForPosition(
                editor,
                cursorAtPos.position(),
                false));

        menu->exec(event->globalPos());
    }

};

LineNumberWidget::LineNumberWidget(
    MyCodeEditor *editor,
    MyCodeEditorState *editorState)
    : QWidget(editor)
    , codeEditor(editor)
    , state(editorState)
{
}

void LineNumberWidget::paintEvent(QPaintEvent *event)
{
    if (!codeEditor || !state)
        return;

    state->gutter.paint(codeEditor, event);
}

void LineNumberWidget::mousePressEvent(QMouseEvent *event)
{
    if (!codeEditor || !state)
        return;

    state->gutter.handleMousePress(codeEditor, event);
}

void LineNumberWidget::wheelEvent(QWheelEvent *event)
{
    if (!codeEditor || !state)
        return;

    state->gutter.handleWheel(codeEditor, event);
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
    state->selections.highlightCurrentLine(this);
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->modes.setAlternateModeEnabled(enabled);
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->semantic.setService(service);
}

EditorBlockGeometry MyCodeEditor::blockGeometry(int blockNumber) const
{
    return state->geometry.blockGeometry(this, blockNumber);
}

qreal MyCodeEditor::documentHeightPx() const
{
    return state->geometry.documentHeightPx(this);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->gutter.resizeTo(this, contentsRect());
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    state->handleSourceSymbolContextMenu(this, event);
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

void MyCodeEditor::setFileName(QString fileName)
{
    if (!state->identity.set(fileName))
        return;

    emit fileNameChanged(state->identity.fileName);
}

QString MyCodeEditor::getFileName() const
{
    return state->identity.fileName;
}

QString MyCodeEditor::currentModuleName() const
{
    return state->currentModuleName(this);
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    state->handleControlKeyPress(this, event);

    if (state->handleSourceSymbolShortcut(this, event))
        return;

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return;
    }

    if (state->modes.alternateModeActive) {
        state->handleAlternateModeKey(this, event);
        return;
    }

    if (state->handleCompletionPopupKey(this, event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

void MyCodeEditor::executeAlternateModeCommand(const QString &command)
{
    state->executeAlternateModeCommand(this, command);
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    state->handleControlKeyRelease(this, event);

    if (event->key() == Qt::Key_Shift) {
        QPlainTextEdit::keyReleaseEvent(event);
        return;
    }

    if (state->modes.alternateModeActive) {
        return;
    }

    QPlainTextEdit::keyReleaseEvent(event);
}

void MyCodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (state->handleSourceNavigationMousePress(this, event))
        return;

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    state->handleSourceNavigationMouseMove(this, event);

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->handleLeave(this);

    QPlainTextEdit::leaveEvent(event);
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    state->cursorNavigation.applyLineTarget(this, target);
}
