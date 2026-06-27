#include "commodecoordinator.h"

#include "commodecommandregistry.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "tabmanager.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QScreen>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {
QString stripText(bool active,
                  const QString& buffer,
                  const QString& message)
{
    if (!active)
        return QString();
    if (!message.isEmpty())
        return QStringLiteral("COM  %1").arg(message);
    if (!buffer.isEmpty()) {
        const QString hint = comModeCommandHint(buffer);
        if (!hint.isEmpty())
            return QStringLiteral("COM  %1  %2").arg(buffer, hint);
        return QStringLiteral("COM  %1").arg(buffer);
    }
    return QStringLiteral("COM");
}

QString pickerItemLabel(const ComModePickerItem& item)
{
    const QString location = item.line > 0
        ? QStringLiteral("%1:%2").arg(item.displayPath).arg(item.line)
        : item.displayPath;
    if (item.kind == ComModePickerItemKind::Signal) {
        const QString detail = item.detailLabel.isEmpty()
            ? item.kindLabel
            : QStringLiteral("%1  %2").arg(item.kindLabel, item.detailLabel);
        return QStringLiteral("%1\n%2  %3")
            .arg(item.name, detail, location);
    }
    if (!item.scopeName.isEmpty()
        && (item.kind == ComModePickerItemKind::Parameter
            || item.kind == ComModePickerItemKind::Localparam)) {
        return QStringLiteral("%1\n%2  %3")
            .arg(item.name, item.scopeName, location);
    }
    return QStringLiteral("%1\n%2").arg(item.name, location);
}

bool objectBelongsToEditor(QObject* object, MyCodeEditor* editor)
{
    if (!object || !editor)
        return false;
    if (object == editor)
        return true;

    auto* widget = qobject_cast<QWidget*>(object);
    return widget && (widget == editor || editor->isAncestorOf(widget));
}

bool focusBelongsToWindow(QWidget* anchor)
{
    QWidget* focus = QApplication::focusWidget();
    if (!focus || !anchor)
        return true;
    QWidget* anchorWindow = anchor->window();
    return !anchorWindow || focus->window() == anchorWindow;
}
} // namespace

ComModeCoordinator::ComModeCoordinator(
    QStatusBar* statusBar,
    QWidget* anchor,
    TabManager* tabManager,
    ProjectModel* projectModel,
    SemanticIndex* semanticIndex,
    NavigationCommandCoordinator* navigation,
    QObject* parent)
    : QObject(parent)
    , statusBar(statusBar)
    , anchor(anchor)
    , tabManager(tabManager)
    , projectModel(projectModel)
    , semanticIndex(semanticIndex)
    , navigation(navigation)
{
    ensureCommandStrip();
    moduleSelector = std::make_unique<ComModuleSelectorPanel>(anchor);
    moduleSelector->setFilterChangedHandler(
        [this](const QString& filter) { (void)refreshPicker(filter); });
    moduleSelector->setItemActivatedHandler(
        [this](const ComModePickerItem& item) { activatePickerItem(item); });
    moduleSelector->setCancelledHandler([this]() {
        if (activeComEditor)
            activeComEditor->setFocus(Qt::ShortcutFocusReason);
    });
    moduleSelector->setExitRequestedHandler(
        [this]() { exitComModeFromPicker(); });
}

ComModeCoordinator::~ComModeCoordinator()
{
    if (globalEscapeInstalled && qApp)
        qApp->removeEventFilter(this);
}

void ComModeCoordinator::connectSignals()
{
    if (connected || !tabManager)
        return;

    for (int i = 0; i < tabManager->editorCount(); ++i)
        attachEditor(tabManager->getEditorAt(i));

    connect(tabManager,
            &TabManager::tabCreated,
            this,
            &ComModeCoordinator::attachEditor);
    connect(tabManager,
            &TabManager::activeTabChanged,
            this,
            [this](MyCodeEditor* editor) {
                if (!editor || !editor->comModeActive()) {
                    hideCommandStrip();
                    return;
                }
                activeComEditor = editor;
                updateCommandStrip(editor,
                                   true,
                                   editor->comModeBuffer(),
                                   QString());
            });
    installGlobalEscapeFilter();
    connected = true;
}

bool ComModeCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (handleGlobalEscape(watched, event))
        return true;
    return QObject::eventFilter(watched, event);
}

void ComModeCoordinator::attachEditor(MyCodeEditor* editor)
{
    if (!editor || attachedEditors.contains(editor))
        return;
    attachedEditors.insert(editor);

    connect(editor,
            &QObject::destroyed,
            this,
            [this, editor]() {
                attachedEditors.remove(editor);
                if (activeComEditor == editor) {
                    activeComEditor = nullptr;
                    hideCommandStrip();
                }
            });
    connect(editor,
            &MyCodeEditor::comModeStateChanged,
            this,
            [this, editor](bool active,
                           const QString& buffer,
                           const QString& message) {
                updateCommandStrip(editor, active, buffer, message);
            });
    connect(editor,
            &MyCodeEditor::comCommandRequested,
            this,
            [this, editor](const QString& command) {
                handleCommand(editor, command);
            });
    connect(editor,
            &MyCodeEditor::comRelativeLineRequested,
            this,
            [this, editor](int moduleLine) {
                handleRelativeLine(editor, moduleLine);
            });
}

QLabel* ComModeCoordinator::commandStripWidget() const
{
    return commandStrip;
}

ComModuleSelectorPanel* ComModeCoordinator::moduleSelectorPanel() const
{
    return moduleSelector.get();
}

void ComModeCoordinator::ensureCommandStrip()
{
    if (commandStrip || !statusBar)
        return;

    commandStrip = new QLabel(statusBar);
    commandStrip->setObjectName(QStringLiteral("comModeCommandStrip"));
    commandStrip->setFixedHeight(32);
    commandStrip->setMinimumWidth(260);
    commandStrip->setTextInteractionFlags(Qt::NoTextInteraction);
    commandStrip->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    commandStrip->setContentsMargins(10, 0, 10, 0);
    QFont mono = commandStrip->font();
    mono.setFamilies({QStringLiteral("Cascadia Code"),
                      QStringLiteral("Consolas"),
                      QStringLiteral("monospace")});
    mono.setStyleHint(QFont::Monospace);
    commandStrip->setFont(mono);
    commandStrip->setStyleSheet(QStringLiteral(
        "QLabel#comModeCommandStrip {"
        "  background: #111827;"
        "  color: #F9FAFB;"
        "  border: 1px solid #38BDF8;"
        "  padding: 0 10px;"
        "}"));
    commandStrip->hide();
    statusBar->addWidget(commandStrip, 1);
}

void ComModeCoordinator::installGlobalEscapeFilter()
{
    if (globalEscapeInstalled || !qApp)
        return;
    qApp->installEventFilter(this);
    globalEscapeInstalled = true;
}

bool ComModeCoordinator::handleGlobalEscape(QObject* watched, QEvent* event)
{
    if (forwardingEscapeToEditor || !event
        || event->type() != QEvent::KeyPress) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() != Qt::Key_Escape || keyEvent->isAutoRepeat())
        return false;

    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return false;

    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (!editor || !editor->isEnabled())
        return false;

    if (objectBelongsToEditor(watched, editor)
        || objectBelongsToEditor(QApplication::focusWidget(), editor)) {
        return false;
    }

    if (!focusBelongsToWindow(anchor))
        return false;

    QKeyEvent forwarded(QEvent::KeyPress,
                        keyEvent->key(),
                        keyEvent->modifiers(),
                        keyEvent->text(),
                        keyEvent->isAutoRepeat(),
                        keyEvent->count());

    forwardingEscapeToEditor = true;
    editor->setFocus(Qt::ShortcutFocusReason);
    QApplication::sendEvent(editor, &forwarded);
    forwardingEscapeToEditor = false;

    if (!forwarded.isAccepted())
        return false;

    keyEvent->accept();
    return true;
}

void ComModeCoordinator::updateCommandStrip(MyCodeEditor* editor,
                                            bool active,
                                            const QString& buffer,
                                            const QString& message)
{
    ensureCommandStrip();
    if (!commandStrip)
        return;

    const bool isCurrentEditor =
        tabManager && editor && tabManager->getCurrentEditor() == editor;
    if (!active || !isCurrentEditor) {
        if (activeComEditor == editor)
            activeComEditor = nullptr;
        if (!active || !activeComEditor)
            hideCommandStrip();
        return;
    }

    activeComEditor = editor;
    commandStrip->setText(stripText(active, buffer, message));
    commandStrip->show();
}

void ComModeCoordinator::hideCommandStrip()
{
    if (!commandStrip)
        return;
    commandStrip->clear();
    commandStrip->hide();
}

void ComModeCoordinator::handleCommand(MyCodeEditor* editor,
                                       const QString& command)
{
    if (!editor)
        return;
    activeComEditor = editor;
    if (command == QStringLiteral("gm")) {
        showPicker(editor, PickerMode::Module, command);
    } else if (command == QStringLiteral("gpk")) {
        showPicker(editor, PickerMode::Package, command);
    } else if (command == QStringLiteral("gpa")) {
        showPicker(editor, PickerMode::Parameter, command);
    } else if (command == QStringLiteral("gsd")) {
        showPicker(editor, PickerMode::Signal, command);
    } else if (command == QStringLiteral("gpo")) {
        handlePortAppend(editor);
    } else if (command == QStringLiteral("gsi")) {
        handleSignalInsert(editor);
    } else if (command == QStringLiteral("gii")) {
        handleInstanceInsert(editor);
    } else if (command == QStringLiteral("gac")) {
        handleAssignInsert(editor);
    } else if (command == QStringLiteral("gpi")) {
        handleParameterInsert(editor);
    } else if (command == QStringLiteral("gef")) {
        handleModuleEndInsert(editor);
    }
}

void ComModeCoordinator::handleRelativeLine(MyCodeEditor* editor,
                                            int moduleLine)
{
    if (!editor)
        return;

    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    ComModeRelativeLineQuery query;
    query.snapshot = semanticSnapshot();
    query.fileName = editor->documentFileName();
    query.currentModuleName = editor->currentModuleName();
    query.currentLine = block.isValid() ? block.blockNumber() + 1 : -1;
    query.requestedModuleLine = moduleLine;

    const ComModeRelativeLineResult result =
        service.relativeLineTarget(query);
    if (!result.ok) {
        editor->showComModeMessage(result.message);
        emit editor->editorStatusMessageRequested(result.message);
        return;
    }

    if (navigation)
        navigation->navigateToFileAndLine(result.filePath,
                                          result.line,
                                          result.column);
    editor->showComModeMessage(QStringLiteral("g%1").arg(moduleLine));
}

void ComModeCoordinator::handlePortAppend(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComPortAppend(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear port append point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleSignalInsert(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComSignalInsert(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear signal insert point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleInstanceInsert(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComInstanceInsert(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear instance insert point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleAssignInsert(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComAssignInsert(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear assign insert point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleParameterInsert(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComParameterInsert(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear parameter insert point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleModuleEndInsert(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->executeComModuleEndInsert(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear module end point");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::showPicker(MyCodeEditor* editor,
                                    PickerMode mode,
                                    const QString& command)
{
    if (!moduleSelector || !editor)
        return;
    activeComEditor = editor;
    activePickerMode = mode;
    activePickerCommand = command;
    const QString prompt = command == QStringLiteral("gpk")
        ? QStringLiteral("gpk package")
        : command == QStringLiteral("gpa")
            ? QStringLiteral("gpa parameter")
            : command == QStringLiteral("gsd")
                ? QStringLiteral("gsd signal")
                : QStringLiteral("gm module");
    moduleSelector->setPrompt(prompt);
    moduleSelector->setEmptyText(
        command == QStringLiteral("gsd")
            ? QStringLiteral("No signals")
            : QString());
    if (!refreshPicker(QString())) {
        if (activeComEditor)
            activeComEditor->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    moduleSelector->showFor(anchor ? anchor : editor);
}

bool ComModeCoordinator::refreshPicker(const QString& filter)
{
    if (!moduleSelector)
        return false;
    ComModePickerQuery query;
    query.snapshot = semanticSnapshot();
    query.project = projectSnapshot();
    query.filter = filter;
    if (activePickerMode == PickerMode::Module) {
        moduleSelector->setItems(service.moduleItems(query));
        return true;
    } else if (activePickerMode == PickerMode::Package) {
        moduleSelector->setItems(service.packageItems(query));
        return true;
    } else if (activePickerMode == PickerMode::Parameter
               || activePickerMode == PickerMode::Signal) {
        ComModeScopedPickerQuery scopedQuery;
        scopedQuery.snapshot = query.snapshot;
        scopedQuery.project = query.project;
        scopedQuery.filter = query.filter;
        scopedQuery.maxResults = query.maxResults;
        if (activeComEditor) {
            const QTextCursor cursor = activeComEditor->textCursor();
            const QTextBlock block = cursor.block();
            scopedQuery.fileName = activeComEditor->documentFileName();
            scopedQuery.currentModuleName = activeComEditor->currentModuleName();
            scopedQuery.currentLine = block.isValid()
                ? block.blockNumber() + 1
                : -1;
        }
        const ComModeScopedPickerResult result =
            activePickerMode == PickerMode::Signal
                ? service.signalItems(scopedQuery)
                : service.parameterItems(scopedQuery);
        if (!result.hasScope) {
            if (activeComEditor) {
                activeComEditor->showComModeMessage(result.message);
                emit activeComEditor->editorStatusMessageRequested(
                    result.message);
            }
            moduleSelector->hide();
            return false;
        }
        if (activePickerMode == PickerMode::Signal) {
            moduleSelector->setEmptyText(
                filter.trimmed().isEmpty()
                    ? QStringLiteral("No signals")
                    : QStringLiteral("No matching signals"));
        }
        moduleSelector->setItems(result.items);
        return true;
    }
    return false;
}

void ComModeCoordinator::activatePickerItem(const ComModePickerItem& item)
{
    MyCodeEditor* sourceEditor = activeComEditor;
    if (navigation && !item.filePath.isEmpty())
        navigation->navigateToFileAndLine(item.filePath, item.line, item.column);

    MyCodeEditor* targetEditor =
        tabManager ? tabManager->getCurrentEditor() : sourceEditor;
    const QString message =
        QStringLiteral("%1 %2").arg(activePickerCommand, item.name);
    if (targetEditor && targetEditor != sourceEditor) {
        if (sourceEditor && sourceEditor->comModeActive())
            sourceEditor->exitComMode();
        activeComEditor = targetEditor;
        targetEditor->enterComMode(message);
        targetEditor->setFocus(Qt::ShortcutFocusReason);
        return;
    }

    if (sourceEditor) {
        sourceEditor->showComModeMessage(message);
        sourceEditor->setFocus(Qt::ShortcutFocusReason);
    }
}

void ComModeCoordinator::exitComModeFromPicker()
{
    QPointer<MyCodeEditor> editor(activeComEditor);
    if (!editor)
        return;
    editor->exitComMode();
    if (editor)
        editor->setFocus(Qt::ShortcutFocusReason);
}

ProjectSnapshot ComModeCoordinator::projectSnapshot() const
{
    return projectModel ? projectModel->snapshot() : ProjectSnapshot();
}

std::shared_ptr<const SemanticIndexSnapshot>
ComModeCoordinator::semanticSnapshot() const
{
    return semanticIndex ? semanticIndex->snapshot() : nullptr;
}

ComModuleSelectorPanel::ComModuleSelectorPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("comModeModuleSelector"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(560);
    setMaximumWidth(820);
    setStyleSheet(QStringLiteral(
        "QFrame#comModeModuleSelector { background:#111827; color:#F9FAFB; "
        "border:1px solid #38BDF8; border-radius:6px; }"
        "QLineEdit { margin:8px 10px 4px 10px; padding:7px; "
        "border:1px solid #475569; border-radius:4px; "
        "background:#0B1120; color:#F9FAFB; font-family:monospace; }"
        "QListWidget { margin:4px 10px 10px 10px; border:0; "
        "background:#111827; color:#E5E7EB; outline:0; }"
        "QListWidget::item { padding:5px 7px; border-radius:3px; }"
        "QListWidget::item:selected { background:#0EA5E9; color:white; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("comModeModuleSearchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("gm module"));
    layout->addWidget(searchEdit);

    resultList = new QListWidget(this);
    resultList->setObjectName(QStringLiteral("comModeModuleResultList"));
    resultList->setMinimumHeight(280);
    layout->addWidget(resultList);

    searchEdit->installEventFilter(this);
    resultList->installEventFilter(this);

    connect(searchEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString& text) {
                if (filterChangedHandler)
                    filterChangedHandler(text);
            });
    connect(resultList,
            &QListWidget::itemDoubleClicked,
            this,
            [this](QListWidgetItem*) { activateCurrentItem(); });
}

void ComModuleSelectorPanel::setItems(
    const QList<ComModePickerItem>& items)
{
    currentItems = items;
    resultList->clear();
    if (currentItems.isEmpty() && !emptyText.isEmpty()) {
        auto* item = new QListWidgetItem(emptyText, resultList);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable
                       & ~Qt::ItemIsEnabled);
        resultList->addItem(item);
        return;
    }
    for (int i = 0; i < currentItems.size(); ++i) {
        auto* item = new QListWidgetItem(
            pickerItemLabel(currentItems.at(i)),
            resultList);
        item->setData(Qt::UserRole, i);
        resultList->addItem(item);
    }
    if (resultList->count() > 0)
        resultList->setCurrentRow(0);
}

void ComModuleSelectorPanel::setPrompt(const QString& prompt)
{
    if (searchEdit)
        searchEdit->setPlaceholderText(prompt);
}

void ComModuleSelectorPanel::setEmptyText(const QString& text)
{
    emptyText = text;
}

void ComModuleSelectorPanel::showFor(QWidget* anchor)
{
    if (searchEdit)
        searchEdit->clear();

    adjustSize();
    QWidget* target = anchor ? anchor->window() : nullptr;
    QRect rect;
    if (target)
        rect = target->geometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        rect = screen->availableGeometry();

    const QPoint pos(rect.center().x() - width() / 2,
                     rect.top() + qMax(90, rect.height() / 5));
    move(pos);
    show();
    raise();
    focusSearch();
}

void ComModuleSelectorPanel::focusSearch()
{
    if (searchEdit) {
        searchEdit->setFocus(Qt::ShortcutFocusReason);
        searchEdit->selectAll();
    }
}

QString ComModuleSelectorPanel::filterText() const
{
    return searchEdit ? searchEdit->text() : QString();
}

void ComModuleSelectorPanel::setFilterChangedHandler(
    std::function<void(const QString&)> handler)
{
    filterChangedHandler = std::move(handler);
}

void ComModuleSelectorPanel::setItemActivatedHandler(
    std::function<void(const ComModePickerItem&)> handler)
{
    itemActivatedHandler = std::move(handler);
}

void ComModuleSelectorPanel::setCancelledHandler(
    std::function<void()> handler)
{
    cancelledHandler = std::move(handler);
}

void ComModuleSelectorPanel::setExitRequestedHandler(
    std::function<void()> handler)
{
    exitRequestedHandler = std::move(handler);
}

bool ComModuleSelectorPanel::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
        return handleKey(static_cast<QKeyEvent*>(event));
    return false;
}

void ComModuleSelectorPanel::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event))
        return;
    QFrame::keyPressEvent(event);
}

void ComModuleSelectorPanel::activateCurrentItem()
{
    const int row = resultList ? resultList->currentRow() : -1;
    if (row < 0 || row >= currentItems.size())
        return;
    const ComModePickerItem item = currentItems.at(row);
    hide();
    if (itemActivatedHandler)
        itemActivatedHandler(item);
}

void ComModuleSelectorPanel::cancel()
{
    hide();
    if (cancelledHandler)
        cancelledHandler();
}

void ComModuleSelectorPanel::moveSelection(int delta)
{
    if (!resultList || resultList->count() == 0)
        return;
    const int next = qBound(0,
                            resultList->currentRow() + delta,
                            resultList->count() - 1);
    resultList->setCurrentRow(next);
}

bool ComModuleSelectorPanel::handleKey(QKeyEvent* event)
{
    if (!event)
        return false;

    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_QuoteLeft
        || event->text() == QStringLiteral("`")) {
        event->accept();
        QPointer<ComModuleSelectorPanel> self(this);
        QTimer::singleShot(0, this, [self]() {
            if (!self)
                return;
            self->hide();
            if (self->exitRequestedHandler)
                self->exitRequestedHandler();
        });
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateCurrentItem();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Down) {
        moveSelection(1);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Up) {
        moveSelection(-1);
        event->accept();
        return true;
    }
    return false;
}
