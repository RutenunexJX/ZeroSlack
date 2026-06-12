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
    explicit LineNumberWidget(MyCodeEditor *editor = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    MyCodeEditor *codeEditor = nullptr;
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
        }
    } appearance;

    struct GutterUi {
        LineNumberWidget *widget = nullptr;

        void init(MyCodeEditor* editor)
        {
            widget = new LineNumberWidget(editor);
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

        void resizeTo(const QRect& contentsRect, int width) const
        {
            if (widget)
                widget->setGeometry(0, 0, width, contentsRect.height());
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
        qreal blockTopY(const MyCodeEditor* editor, int blockNumber) const
        {
            QTextBlock block = editor->document()->findBlockByNumber(blockNumber);
            if (!block.isValid())
                return 0;

            return editor->blockBoundingGeometry(block).translated(
                editor->contentOffset()).top();
        }

        qreal blockHeight(const MyCodeEditor* editor, int blockNumber) const
        {
            QTextBlock block = editor->document()->findBlockByNumber(blockNumber);
            if (!block.isValid())
                return editor->fontMetrics().height();

            return editor->blockBoundingRect(block).height();
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
                &MyCodeEditor::onTsContentsChange);
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

        void attachToEditor(MyCodeEditor* editor)
        {
            init(editor);
            QObject::connect(
                timer,
                &QTimer::timeout,
                editor,
                &MyCodeEditor::onAutoCompleteTimer);
            QObject::connect(
                completer,
                QOverload<const QModelIndex &>::of(&QCompleter::activated),
                editor,
                &MyCodeEditor::onCompletionActivated);
            QObject::connect(
                editor,
                &QPlainTextEdit::textChanged,
                editor,
                &MyCodeEditor::onTextChanged);
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

        void attachToEditor(MyCodeEditor* editor)
        {
            init(editor);
            QObject::connect(
                timer,
                &QTimer::timeout,
                editor,
                &MyCodeEditor::highlightCurrentLine);

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
        gutter.init(editor);
        identity.set(QString());
        editor->setMouseTracking(true);
    }

    void shutdown()
    {
        gutter.destroy();
    }

    void attachEditorConnections(MyCodeEditor* editor)
    {
        highlightRefresh.attachToEditor(editor);
        QObject::connect(
            editor,
            &QPlainTextEdit::blockCountChanged,
            editor,
            &MyCodeEditor::updateLineNumberWidgetWidth);
        QObject::connect(
            editor,
            &QPlainTextEdit::updateRequest,
            editor,
            &MyCodeEditor::updateLineNumberWidget);
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

};

LineNumberWidget::LineNumberWidget(MyCodeEditor *editor)
    : QWidget(editor)
    , codeEditor(editor)
{
}

void LineNumberWidget::paintEvent(QPaintEvent *event)
{
    if (!codeEditor)
        return;

    codeEditor->state->gutter.paint(codeEditor, event);
}

void LineNumberWidget::mousePressEvent(QMouseEvent *event)
{
    if (!codeEditor)
        return;

    codeEditor->state->gutter.handleMousePress(codeEditor, event);
}

void LineNumberWidget::wheelEvent(QWheelEvent *event)
{
    if (!codeEditor)
        return;

    codeEditor->state->gutter.handleWheel(codeEditor, event);
}

MyCodeEditor::MyCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , state(std::make_unique<MyCodeEditorState>())
{
    state->initializeCore(this);

    state->attachEditorConnections(this);
    state->appearance.apply(this);
    state->syntax.attachToEditor(this);
    state->completion.attachToEditor(this);

    highlightCurrentLine();
    updateLineNumberWidgetWidth();

    setLineWrapMode(QPlainTextEdit::NoWrap);

}

MyCodeEditor::~MyCodeEditor()
{
    state->shutdown();
}

void MyCodeEditor::onTsContentsChange(int position, int charsRemoved, int charsAdded)
{
    // Incrementally update the tree-sitter model (syntax keeps the pre-edit text, so it can derive
    // the old end point itself). Runs before the highlighter's reformat (connected later).
    state->syntax.applyEdit(position,
                            charsRemoved,
                            charsAdded,
                            document()->toPlainText());
}

int MyCodeEditor::getLineNumberWidgetWidth()
{
    return state->gutter.widthFor(this);
}

void MyCodeEditor::highlightCurrentLine()
{
    state->selections.highlightCurrentLine(this);
}

void MyCodeEditor::refreshScopeAndCurrentLineHighlight()
{
    highlightCurrentLine();
}

void MyCodeEditor::setAlternateModeEnabled(bool enabled)
{
    state->modes.setAlternateModeEnabled(enabled);
}

void MyCodeEditor::setSemanticContextService(EditorSemanticContextService* service)
{
    state->semantic.setService(service);
}

QString MyCodeEditor::currentModuleNameAt(int charPos) const
{
    // Live, error-tolerant enclosing module from the tree-sitter tree (A3). The editor keeps
    // syntax synced on every edit, so this is never stale (unlike the debounced Slang path).
    return state->currentModuleNameAt(charPos);
}

qreal MyCodeEditor::getBlockTopY(int blockNumber) const
{
    return state->geometry.blockTopY(this, blockNumber);
}

qreal MyCodeEditor::getBlockHeight(int blockNumber) const
{
    return state->geometry.blockHeight(this, blockNumber);
}

qreal MyCodeEditor::getDocumentHeightPx() const
{
    return state->geometry.documentHeightPx(this);
}

void MyCodeEditor::updateLineNumberWidget(QRect rect, int dy)
{
    state->gutter.refresh(rect, dy, getLineNumberWidgetWidth());
}

void MyCodeEditor::updateLineNumberWidgetWidth()
{
    setViewportMargins(getLineNumberWidgetWidth(),0,0,0);
}

void MyCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    state->gutter.resizeTo(contentsRect(), getLineNumberWidgetWidth());
}

void MyCodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    std::unique_ptr<QMenu> menu(createStandardContextMenu(event->pos()));
    const QTextCursor cursorAtPos = cursorForPosition(event->pos());
    emit sourceSymbolContextMenuRequested(
        menu.get(),
        editorSemanticContextForPosition(cursorAtPos.position()));

    menu->exec(event->globalPos());
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

EditorSemanticContext MyCodeEditor::semanticContextForCursor(
    const QTextCursor& cursor,
    bool includeDocumentText) const
{
    return state->semanticContextForCursor(
        this,
        cursor,
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

void MyCodeEditor::onTextChanged()
{
    state->completion.stopTimer();
    updateCompletionTriggerForTextChange(textCursor());
}

void MyCodeEditor::updateCompletionTriggerForTextChange(const QTextCursor& cursor)
{
    EditorSemanticContext context =
        semanticContextForCursor(cursor);
    context.moduleName = currentModuleNameAt(cursor.position() - 1);
    const EditorCompletionTextChangeState completionState =
        state->semanticService()->completionTextChangeState(context);
    state->modes.setCommandModeActive(completionState.commandModeActive);

    if (completionState.startCompletionTimer) {
        state->completion.startTimer();
    } else if (completionState.hidePopup) {
        hideAutoComplete();
    }
}

void MyCodeEditor::hideAutoComplete()
{
    state->hideAutoComplete(this);
}

void MyCodeEditor::onCompletionActivated(const QModelIndex &index)
{
    const EditorCompletionActivationContext activationContext =
        state->completion.activationContextForIndex(index, state->modes);
    const CompletionActivationState activationState =
        state->semanticService()->completionActivationState(activationContext);
    applyCompletionActivationState(activationState);
}

void MyCodeEditor::applyCompletionActivationState(
    const CompletionActivationState& activationState)
{
    if (activationState.action == CompletionActivationAction::None)
        return;

    QTextCursor cursor = textCursor();

    if (activationState.action
        == CompletionActivationAction::ExecuteAlternateCommand) {
        executeAlternateModeCommand(activationState.text);
        return;
    }

    if (activationState.action == CompletionActivationAction::ReplaceLine) {
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        cursor.insertText(activationState.text);

        if (activationState.clearCommandMode) {
            state->modes.clearCommandMode();
            state->selections.clearCommand(this);
        }
    } else if (activationState.action == CompletionActivationAction::ReplaceWord) {
        state->completion.replaceWordAtCursor(this, activationState.text);
    }

    if (activationState.hidePopup)
        hideAutoComplete();
}

void MyCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control
        && state->sourceHover.setCtrlPressed(true)) {
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        if (rect().contains(mousePos)) {
            refreshSourceNavigationHoverAt(mousePos);
        }
    }

    if (handleSourceSymbolShortcut(event))
        return;

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return;
    }

    if (state->modes.alternateModeActive) {
        handleAlternateModeKey(event);
        return;
    }

    if (handleCompletionPopupKey(event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

bool MyCodeEditor::handleSourceSymbolShortcut(QKeyEvent *event)
{
    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = event->key();
    sourceShortcutContext.modifiers = int(event->modifiers());
    sourceShortcutContext.semanticContext =
        editorSemanticContextForPosition(textCursor().position());
    const EditorSourceSymbolShortcutState sourceShortcutState =
        state->semanticService()->sourceSymbolShortcutState(sourceShortcutContext);
    if (!sourceShortcutState.matched)
        return false;

    emit sourceSymbolActionRequested(
        sourceShortcutState.action,
        sourceShortcutState.semanticContext);
    if (sourceShortcutState.acceptEvent)
        event->accept();
    return true;
}

bool MyCodeEditor::handleAlternateModeKey(QKeyEvent *event)
{
    if (handleCompletionPopupKey(event))
        return true;

    EditorAlternateModeKeyContext alternateKeyContext;
    alternateKeyContext.key = event->key();
    alternateKeyContext.text = event->text();
    alternateKeyContext.buffer = state->modes.alternateBuffer;
    const EditorAlternateModeKeyState alternateKeyState =
        state->semanticService()->alternateModeKeyState(alternateKeyContext);
    applyAlternateModeKeyState(alternateKeyState);
    return true;
}

void MyCodeEditor::applyAlternateModeKeyState(
    const EditorAlternateModeKeyState& keyState)
{
    switch (keyState.action) {
    case EditorAlternateModeKeyAction::UpdateInput:
    case EditorAlternateModeKeyAction::RefreshCompletions:
        state->applyAlternateModeCompletionDisplayState(
            this,
            keyState.completion);
        break;
    case EditorAlternateModeKeyAction::ExecuteCommand:
        executeAlternateModeCommand(keyState.command);
        break;
    case EditorAlternateModeKeyAction::ClearAndHide:
        if (keyState.hidePopup)
            hideAutoComplete();
        if (keyState.clearBuffer)
            state->modes.clearAlternateBuffer();
        break;
    case EditorAlternateModeKeyAction::Consume:
        break;
    }
}

bool MyCodeEditor::handleCompletionPopupKey(QKeyEvent *event)
{
    return state->handleCompletionPopupKey(this, event);
}

void MyCodeEditor::onAutoCompleteTimer()
{
    const QTextCursor cursor = textCursor();
    const QTextBlock currentBlock = cursor.block();

    state->modes.noteCompletionTimerLine(currentBlock.blockNumber());

    EditorSemanticContext context =
        semanticContextForCursor(cursor, true);

    if (refreshCommandModeCompletion(context))
        return;

    if (state->modes.alternateModeActive) {
        state->processAlternateModeInput(this, context.lineUpToCursor);
        return;
    }

    refreshSymbolCompletion(context, currentBlock);
}

bool MyCodeEditor::refreshCommandModeCompletion(
    const EditorSemanticContext& context)
{
    const EditorCommandModeCompletionRefreshState commandState =
        state->semanticService()->commandModeCompletionRefreshState(
            context,
            state->modes.commandModeExitedByDoubleSpace);
    if (commandState.matched) {
        state->modes.setCommandModeActive(commandState.commandModeActive);
        if (commandState.suppressAfterExit) {
            return true;
        }

        if (commandState.exitRequested) {
            if (commandState.clearCommandHighlight)
                state->selections.clearCommand(this);
            if (commandState.markExitedByDoubleSpace)
                state->modes.markCommandModeExitedByDoubleSpace();
            if (state->completion.popupVisible()) {
                state->completion.hidePopup();
            }
            return true;
        }

        if (commandState.highlightCommand)
            state->selections.highlightCommand(
                this,
                commandState.completion.prefixPosition);

        if (commandState.hidePopup) {
            if (state->completion.popupVisible())
                state->completion.hidePopup();
            return true;
        }

        if (commandState.showCompletions) {
            state->completion.updateCommandModeCompletions(commandState);
            state->showAutoComplete(this);
        }
        return true;
    }

    if (commandState.resetExitedByDoubleSpace)
        state->modes.resetCommandModeExit();

    state->selections.clearCommand(this);
    state->modes.clearCommandMode();

    return false;
}

void MyCodeEditor::refreshSymbolCompletion(
    EditorSemanticContext context,
    const QTextBlock& currentBlock)
{
    context.wordPrefix = state->completion.wordUnderCursor(this);
    const EditorCompletionState completionState =
        state->semanticService()->editorCompletionState(context);
    if (completionState.available) {
        state->completion.updateSymbolCompletions(completionState);
        state->completion.setReplacementStart(
            currentBlock.position(),
            completionState.replacementStartColumn);
        state->showAutoComplete(this);
    }
}

void MyCodeEditor::executeAlternateModeCommand(const QString &command)
{
    if (!command.trimmed().isEmpty())
        emit alternateCommandRequested(command);
    state->modes.clearAlternateBuffer();
    hideAutoComplete();
}

void MyCodeEditor::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control
        && state->sourceHover.setCtrlPressed(false)) {
        clearSourceNavigationHover();
    }

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
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (requestSourceNavigationAtPosition(event->pos())) {
            event->accept();
            return;
        }
    }

    QPlainTextEdit::mousePressEvent(event);
}

void MyCodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    bool isCtrlPressed = (event->modifiers() & Qt::ControlModifier);

    if (state->sourceHover.setCtrlPressed(isCtrlPressed)) {
        if (state->sourceHover.isCtrlPressed()) {
            refreshSourceNavigationHoverAt(event->pos());
        } else {
            clearSourceNavigationHover();
        }
    } else if (state->sourceHover.isCtrlPressed()) {
        refreshSourceNavigationHoverAt(event->pos());
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MyCodeEditor::leaveEvent(QEvent *event)
{
    state->sourceHover.setCtrlPressed(false);
    clearSourceNavigationHover();

    QPlainTextEdit::leaveEvent(event);
}

EditorSourceNavigationTarget
MyCodeEditor::sourceNavigationTargetAtPosition(const QPoint& position)
{
    return state->sourceNavigationTargetAtPosition(this, position);
}

bool MyCodeEditor::requestSourceNavigationAtPosition(const QPoint& position)
{
    return state->requestSourceNavigationAtPosition(this, position);
}

void MyCodeEditor::refreshSourceNavigationHoverAt(const QPoint& position)
{
    state->refreshSourceNavigationHoverAt(this, position);
}

void MyCodeEditor::applySourceNavigationHover(
    const EditorSourceNavigationTarget& target)
{
    state->applySourceNavigationHover(this, target);
}

void MyCodeEditor::clearSourceNavigationHover()
{
    state->clearSourceNavigationHover(this);
}

void MyCodeEditor::moveMouseToCursor()
{
    state->cursorNavigation.moveMouseToCursor(this);
}

void MyCodeEditor::applyLineNavigationTarget(
    const SourceLineNavigationTarget& target)
{
    state->cursorNavigation.applyLineTarget(this, target);
}
