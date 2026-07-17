#include "commodecoordinator.h"

#include "commodecommandregistry.h"
#include "columnnumbertool.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "tabmanager.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {
enum class CommandStripTone {
    Normal,
    Error
};

QString commandStripStyleSheet(CommandStripTone tone)
{
    if (tone == CommandStripTone::Error) {
        return QStringLiteral(
            "QLabel#comModeCommandStrip {"
            "  background: #1F1115;"
            "  color: #FCA5A5;"
            "  border: 1px solid #F43F5E;"
            "  padding: 0 10px;"
            "}");
    }
    return QStringLiteral(
        "QLabel#comModeCommandStrip {"
        "  background: #111827;"
        "  color: #D1FAE5;"
        "  border: 1px solid #38BDF8;"
        "  padding: 0 10px;"
        "}");
}

bool isComModeErrorMessage(const QString& message)
{
    if (message.isEmpty())
        return false;
    return message.startsWith(QStringLiteral("Unknown COM command"))
        || message.startsWith(QStringLiteral("Incomplete COM command"))
        || message.startsWith(QStringLiteral("COM command registry is invalid"))
        || message.startsWith(QStringLiteral("Line number must be"))
        || message.startsWith(QStringLiteral("No assignment"))
        || message.startsWith(QStringLiteral("No begin-end"))
        || message.startsWith(QStringLiteral("No column"))
        || message.startsWith(QStringLiteral("No current"))
        || message.startsWith(QStringLiteral("No clear"))
        || message.startsWith(QStringLiteral("Module has only"));
}

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

bool isComModeToggleKey(QKeyEvent* event)
{
    if (!event)
        return false;
    const bool backtick = event->key() == Qt::Key_QuoteLeft
        || event->text() == QStringLiteral("`");
    if (!backtick)
        return false;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    return modifiers
        == (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
}

bool isColumnNumberShortcutKey(QKeyEvent* event)
{
    if (!event || event->key() != Qt::Key_C)
        return false;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    return modifiers == Qt::AltModifier;
}

void setComboValue(QComboBox* combo, int value)
{
    if (!combo)
        return;
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

int comboValue(const QComboBox* combo, int fallback)
{
    if (!combo)
        return fallback;
    const QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : fallback;
}
} // namespace

class ColumnNumberToolPanel : public QFrame
{
public:
    explicit ColumnNumberToolPanel(QWidget* parent = nullptr);

    void configure(const ColumnNumberConfig& config,
                   int rows,
                   const QStringList& selectedRows);
    void showFor(QWidget* anchor);
    void setApplyHandler(
        std::function<void(const ColumnNumberConfig&, int)> handler);
    void setCancelledHandler(std::function<void()> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QSpinBox* startSpin = nullptr;
    QComboBox* baseCombo = nullptr;
    QComboBox* styleCombo = nullptr;
    QLabel* bitWidthLabel = nullptr;
    QSpinBox* bitWidthSpin = nullptr;
    QComboBox* directionCombo = nullptr;
    QSpinBox* stepSpin = nullptr;
    QSpinBox* repeatSpin = nullptr;
    QComboBox* digitWidthModeCombo = nullptr;
    QLabel* digitWidthLabel = nullptr;
    QSpinBox* digitWidthSpin = nullptr;
    QComboBox* padCombo = nullptr;
    QLabel* hexCaseLabel = nullptr;
    QComboBox* hexCaseCombo = nullptr;
    QComboBox* replaceModeCombo = nullptr;
    QLabel* previewLabel = nullptr;
    int lineCount = 0;
    std::function<void(const ColumnNumberConfig&, int)> applyHandler;
    std::function<void()> cancelledHandler;

    ColumnNumberConfig currentConfig() const;
    void applyConfig(const ColumnNumberConfig& config);
    void refreshPreview();
    void cancel();
    void apply();
    bool handleKey(QKeyEvent* event);
};

ColumnNumberToolPanel::ColumnNumberToolPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("columnNumberToolPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(430);
    setStyleSheet(QStringLiteral(
        "QFrame#columnNumberToolPanel { background:#0B1120; color:#E5E7EB; "
        "border:1px solid #38BDF8; border-radius:6px; }"
        "QLabel { color:#E5E7EB; }"
        "QComboBox, QSpinBox { padding:4px 6px; background:#111827; "
        "color:#F9FAFB; border:1px solid #475569; border-radius:4px; }"
        "QLabel#columnNumberPreview { background:#111827; color:#D1FAE5; "
        "border:1px solid #334155; border-radius:4px; padding:6px; "
        "font-family:monospace; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Column Number Tool"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);
    outer->addLayout(form);

    startSpin = new QSpinBox(this);
    startSpin->setRange(-1000000000, 1000000000);
    form->addRow(QStringLiteral("Start"), startSpin);

    baseCombo = new QComboBox(this);
    baseCombo->addItem(QStringLiteral("Dec"),
                       static_cast<int>(ColumnNumberBase::Dec));
    baseCombo->addItem(QStringLiteral("Hex"),
                       static_cast<int>(ColumnNumberBase::Hex));
    baseCombo->addItem(QStringLiteral("Bin"),
                       static_cast<int>(ColumnNumberBase::Bin));
    form->addRow(QStringLiteral("Base"), baseCombo);

    styleCombo = new QComboBox(this);
    styleCombo->addItem(QStringLiteral("Plain"),
                        static_cast<int>(ColumnNumberStyle::Plain));
    styleCombo->addItem(QStringLiteral("C-like"),
                        static_cast<int>(ColumnNumberStyle::CLike));
    styleCombo->addItem(QStringLiteral("SV unsized"),
                        static_cast<int>(ColumnNumberStyle::SvUnsized));
    styleCombo->addItem(QStringLiteral("SV sized"),
                        static_cast<int>(ColumnNumberStyle::SvSized));
    form->addRow(QStringLiteral("Style"), styleCombo);

    bitWidthLabel = new QLabel(QStringLiteral("Bit width"), this);
    bitWidthSpin = new QSpinBox(this);
    bitWidthSpin->setRange(1, 4096);
    form->addRow(bitWidthLabel, bitWidthSpin);

    directionCombo = new QComboBox(this);
    directionCombo->addItem(QStringLiteral("Up"),
                            static_cast<int>(ColumnNumberDirection::Up));
    directionCombo->addItem(QStringLiteral("Down"),
                            static_cast<int>(ColumnNumberDirection::Down));
    form->addRow(QStringLiteral("Direction"), directionCombo);

    stepSpin = new QSpinBox(this);
    stepSpin->setRange(0, 1000000000);
    stepSpin->setValue(1);
    form->addRow(QStringLiteral("Step"), stepSpin);

    repeatSpin = new QSpinBox(this);
    repeatSpin->setRange(1, 1000000);
    repeatSpin->setValue(1);
    form->addRow(QStringLiteral("Repeat"), repeatSpin);

    digitWidthModeCombo = new QComboBox(this);
    digitWidthModeCombo->addItem(QStringLiteral("auto"), 0);
    digitWidthModeCombo->addItem(QStringLiteral("fixed"), 1);
    form->addRow(QStringLiteral("Digit width"), digitWidthModeCombo);

    digitWidthLabel = new QLabel(QStringLiteral("Fixed digits"), this);
    digitWidthSpin = new QSpinBox(this);
    digitWidthSpin->setRange(1, 1024);
    form->addRow(digitWidthLabel, digitWidthSpin);

    padCombo = new QComboBox(this);
    padCombo->addItem(QStringLiteral("none"),
                      static_cast<int>(ColumnNumberPad::None));
    padCombo->addItem(QStringLiteral("space"),
                      static_cast<int>(ColumnNumberPad::Space));
    padCombo->addItem(QStringLiteral("zero"),
                      static_cast<int>(ColumnNumberPad::Zero));
    form->addRow(QStringLiteral("Pad"), padCombo);

    hexCaseLabel = new QLabel(QStringLiteral("Hex case"), this);
    hexCaseCombo = new QComboBox(this);
    hexCaseCombo->addItem(QStringLiteral("Upper"), 1);
    hexCaseCombo->addItem(QStringLiteral("Lower"), 0);
    form->addRow(hexCaseLabel, hexCaseCombo);

    replaceModeCombo = new QComboBox(this);
    replaceModeCombo->addItem(
        QStringLiteral("replace selection"),
        static_cast<int>(ColumnNumberReplaceMode::ReplaceSelection));
    replaceModeCombo->addItem(
        QStringLiteral("insert at column"),
        static_cast<int>(ColumnNumberReplaceMode::InsertAtColumn));
    form->addRow(QStringLiteral("Replace mode"), replaceModeCombo);

    previewLabel = new QLabel(this);
    previewLabel->setObjectName(QStringLiteral("columnNumberPreview"));
    previewLabel->setMinimumHeight(82);
    previewLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    outer->addWidget(previewLabel);

    const QList<QWidget*> watched = {
        startSpin,
        baseCombo,
        styleCombo,
        bitWidthSpin,
        directionCombo,
        stepSpin,
        repeatSpin,
        digitWidthModeCombo,
        digitWidthSpin,
        padCombo,
        hexCaseCombo,
        replaceModeCombo,
    };
    for (QWidget* widget : watched) {
        widget->installEventFilter(this);
    }

    auto refresh = [this]() { refreshPreview(); };
    connect(startSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            refresh);
    connect(baseCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(styleCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(bitWidthSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            refresh);
    connect(directionCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(stepSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            refresh);
    connect(repeatSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            refresh);
    connect(digitWidthModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(digitWidthSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            refresh);
    connect(padCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(hexCaseCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(replaceModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
}

void ColumnNumberToolPanel::configure(
    const ColumnNumberConfig& config,
    int rows,
    const QStringList& selectedRows)
{
    Q_UNUSED(selectedRows)
    lineCount = qMax(0, rows);
    applyConfig(config);
}

void ColumnNumberToolPanel::showFor(QWidget* anchor)
{
    adjustSize();
    QWidget* target = anchor ? anchor->window() : nullptr;
    QRect rect;
    if (target)
        rect = target->geometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        rect = screen->availableGeometry();

    const QPoint pos(rect.center().x() - width() / 2,
                     rect.top() + qMax(120, rect.height() / 4));
    move(pos);
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void ColumnNumberToolPanel::setApplyHandler(
    std::function<void(const ColumnNumberConfig&, int)> handler)
{
    applyHandler = std::move(handler);
}

void ColumnNumberToolPanel::setCancelledHandler(
    std::function<void()> handler)
{
    cancelledHandler = std::move(handler);
}

ColumnNumberConfig ColumnNumberToolPanel::currentConfig() const
{
    ColumnNumberConfig config;
    config.start = startSpin ? startSpin->value() : 0;
    config.base = static_cast<ColumnNumberBase>(
        comboValue(baseCombo, static_cast<int>(ColumnNumberBase::Dec)));
    config.style = static_cast<ColumnNumberStyle>(
        comboValue(styleCombo, static_cast<int>(ColumnNumberStyle::Plain)));
    if (config.base == ColumnNumberBase::Dec
        && config.style == ColumnNumberStyle::CLike) {
        config.style = ColumnNumberStyle::Plain;
    }
    config.bitWidth = bitWidthSpin ? bitWidthSpin->value() : 8;
    config.direction = static_cast<ColumnNumberDirection>(
        comboValue(directionCombo,
                   static_cast<int>(ColumnNumberDirection::Up)));
    config.step = stepSpin ? stepSpin->value() : 1;
    config.repeat = repeatSpin ? repeatSpin->value() : 1;
    config.fixedDigitWidth = digitWidthModeCombo
        && digitWidthModeCombo->currentData().toInt() == 1;
    config.digitWidth = digitWidthSpin ? digitWidthSpin->value() : 0;
    config.pad = static_cast<ColumnNumberPad>(
        comboValue(padCombo, static_cast<int>(ColumnNumberPad::None)));
    config.uppercaseHex = !hexCaseCombo || hexCaseCombo->currentData().toInt() != 0;
    config.replaceMode = static_cast<ColumnNumberReplaceMode>(
        comboValue(replaceModeCombo,
                   static_cast<int>(
                       ColumnNumberReplaceMode::ReplaceSelection)));
    return config;
}

void ColumnNumberToolPanel::applyConfig(const ColumnNumberConfig& config)
{
    if (startSpin)
        startSpin->setValue(static_cast<int>(config.start));
    setComboValue(baseCombo, static_cast<int>(config.base));
    setComboValue(styleCombo, static_cast<int>(config.style));
    if (bitWidthSpin)
        bitWidthSpin->setValue(qMax(1, config.bitWidth));
    setComboValue(directionCombo, static_cast<int>(config.direction));
    if (stepSpin)
        stepSpin->setValue(static_cast<int>(qMax<qint64>(0, config.step)));
    if (repeatSpin)
        repeatSpin->setValue(qMax(1, config.repeat));
    setComboValue(digitWidthModeCombo, config.fixedDigitWidth ? 1 : 0);
    if (digitWidthSpin)
        digitWidthSpin->setValue(qMax(1, config.digitWidth));
    setComboValue(padCombo, static_cast<int>(config.pad));
    setComboValue(hexCaseCombo, config.uppercaseHex ? 1 : 0);
    setComboValue(replaceModeCombo, static_cast<int>(config.replaceMode));
    refreshPreview();
}

void ColumnNumberToolPanel::refreshPreview()
{
    ColumnNumberConfig config = currentConfig();
    if (config.base == ColumnNumberBase::Dec
        && config.style == ColumnNumberStyle::CLike) {
        setComboValue(styleCombo, static_cast<int>(ColumnNumberStyle::Plain));
        config.style = ColumnNumberStyle::Plain;
    }

    const bool svSized = config.style == ColumnNumberStyle::SvSized;
    if (bitWidthLabel)
        bitWidthLabel->setVisible(svSized);
    if (bitWidthSpin)
        bitWidthSpin->setVisible(svSized);

    const bool fixedDigits = config.fixedDigitWidth;
    if (digitWidthLabel)
        digitWidthLabel->setVisible(fixedDigits);
    if (digitWidthSpin)
        digitWidthSpin->setVisible(fixedDigits);

    const bool isHex = config.base == ColumnNumberBase::Hex;
    if (hexCaseLabel)
        hexCaseLabel->setVisible(isHex);
    if (hexCaseCombo)
        hexCaseCombo->setVisible(isHex);

    QStringList rows = previewColumnNumbers(config, qMin(lineCount, 12));
    if (lineCount > rows.size())
        rows.append(QStringLiteral("..."));
    if (previewLabel) {
        previewLabel->setText(rows.isEmpty()
                                  ? QStringLiteral("Preview")
                                  : rows.join(QLatin1Char('\n')));
    }
}

void ColumnNumberToolPanel::cancel()
{
    hide();
    if (cancelledHandler)
        cancelledHandler();
}

void ColumnNumberToolPanel::apply()
{
    const ColumnNumberConfig config = currentConfig();
    const int rows = lineCount;
    hide();
    if (applyHandler)
        applyHandler(config, rows);
}

bool ColumnNumberToolPanel::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
        return handleKey(static_cast<QKeyEvent*>(event));
    return false;
}

void ColumnNumberToolPanel::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event))
        return;
    QFrame::keyPressEvent(event);
}

bool ColumnNumberToolPanel::handleKey(QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        apply();
        event->accept();
        return true;
    }
    return false;
}

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
    columnNumberTool = std::make_unique<ColumnNumberToolPanel>(anchor);
    columnNumberTool->setCancelledHandler([this]() {
        if (columnNumberEditor)
            columnNumberEditor->setFocus(Qt::ShortcutFocusReason);
        columnNumberEditor.clear();
        columnNumberOpenedFromCom = false;
    });
    columnNumberTool->setApplyHandler(
        [this](const ColumnNumberConfig& config, int rows) {
            QPointer<MyCodeEditor> editor = columnNumberEditor;
            const bool openedFromCom = columnNumberOpenedFromCom;
            columnNumberEditor.clear();
            columnNumberOpenedFromCom = false;
            if (!editor)
                return;
            const QStringList textRows = previewColumnNumbers(config, rows);
            QString message;
            const bool replaceSelection =
                config.replaceMode
                == ColumnNumberReplaceMode::ReplaceSelection;
            if (!editor->applyColumnSelectionTexts(
                    textRows,
                    replaceSelection,
                    &message)) {
                if (message.isEmpty())
                    message = QStringLiteral("No column selection");
                editor->showComModeMessage(message);
                emit editor->editorStatusMessageRequested(message);
                editor->setFocus(Qt::ShortcutFocusReason);
                return;
            }
            if (openedFromCom && editor->comModeActive())
                editor->exitComMode();
            editor->setFocus(Qt::ShortcutFocusReason);
        });
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
    connect(editor,
            &MyCodeEditor::columnNumberToolRequested,
            this,
            [this, editor]() {
                handleColumnNumberTool(editor);
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
    commandStrip->setStyleSheet(commandStripStyleSheet(CommandStripTone::Normal));
    commandStrip->hide();
    statusBar->addWidget(commandStrip, 1);
}

void ComModeCoordinator::installGlobalEscapeFilter()
{
    if (globalEscapeInstalled || !qApp)
        return;
    qApp->installEventFilter(this);
    globalEscapeInstalled = true;

    QWidget* shortcutParent =
        anchor ? anchor->window() : (statusBar ? statusBar->window() : nullptr);
    if (shortcutParent && !comToggleShortcut) {
        comToggleShortcut = new QShortcut(
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::ALT | Qt::Key_QuoteLeft),
            shortcutParent);
        comToggleShortcut->setContext(Qt::WindowShortcut);
        connect(comToggleShortcut,
                &QShortcut::activated,
                this,
                &ComModeCoordinator::toggleCurrentEditorComMode);
    }
    if (shortcutParent && !columnNumberShortcut) {
        columnNumberShortcut = new QShortcut(
            QKeySequence(Qt::ALT | Qt::Key_C),
            shortcutParent);
        columnNumberShortcut->setContext(Qt::WindowShortcut);
        connect(columnNumberShortcut,
                &QShortcut::activated,
                this,
                &ComModeCoordinator::openColumnNumberToolForCurrentEditor);
    }
}

MyCodeEditor* ComModeCoordinator::currentEditorForLocalCommand() const
{
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (!editor || !editor->isEnabled()) {
        editor = nullptr;
        for (MyCodeEditor* candidate : std::as_const(attachedEditors)) {
            if (candidate && candidate->isEnabled()) {
                editor = candidate;
                break;
            }
        }
    }
    return editor;
}

void ComModeCoordinator::toggleCurrentEditorComMode()
{
    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        return;
    if (editor->comModeActive())
        editor->exitComMode();
    else
        editor->enterComMode();
    activeComEditor = editor->comModeActive() ? editor : nullptr;
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::openColumnNumberToolForCurrentEditor()
{
    if (QApplication::activeModalWidget())
        return;
    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible()) {
        return;
    }
    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        return;
    handleColumnNumberTool(editor);
}

bool ComModeCoordinator::handleGlobalEscape(QObject* watched, QEvent* event)
{
    if (forwardingEscapeToEditor || !event
        || event->type() != QEvent::KeyPress) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->isAutoRepeat())
        return false;

    if (QApplication::activeModalWidget())
        return false;
    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible()) {
        return false;
    }

    if (isColumnNumberShortcutKey(keyEvent)) {
        if (!focusBelongsToWindow(anchor))
            return false;
        openColumnNumberToolForCurrentEditor();
        keyEvent->accept();
        return true;
    }

    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    if (!editor || !editor->isEnabled())
        return false;

    if (!isComModeToggleKey(keyEvent))
        return false;

    if (objectBelongsToEditor(watched, editor)
        || objectBelongsToEditor(QApplication::focusWidget(), editor)) {
        return false;
    }

    if (!focusBelongsToWindow(anchor))
        return false;

    forwardingEscapeToEditor = true;
    toggleCurrentEditorComMode();
    forwardingEscapeToEditor = false;

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
    commandStrip->setStyleSheet(
        commandStripStyleSheet(isComModeErrorMessage(message)
                                   ? CommandStripTone::Error
                                   : CommandStripTone::Normal));
    commandStrip->setText(stripText(active, buffer, message));
    commandStrip->show();
}

void ComModeCoordinator::hideCommandStrip()
{
    if (!commandStrip)
        return;
    commandStrip->setStyleSheet(commandStripStyleSheet(CommandStripTone::Normal));
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
    } else if (command == QStringLiteral("cr")) {
        handleClearAssignmentRhs(editor);
    } else if (command == QStringLiteral("cn")) {
        handleColumnNumberTool(editor);
    } else if (command == QStringLiteral("si")) {
        handleSelectInsideBeginEnd(editor);
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

void ComModeCoordinator::handleClearAssignmentRhs(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->clearSelectedAssignmentRhs(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No assignment RHS found");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->exitComMode();
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleSelectInsideBeginEnd(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QString message;
    if (!editor->selectInsideBeginEnd(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No begin-end block");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    editor->showComModeMessage(message.isEmpty()
                                   ? QStringLiteral("Selected inside begin-end")
                                   : message);
    editor->setFocus(Qt::ShortcutFocusReason);
}

void ComModeCoordinator::handleColumnNumberTool(MyCodeEditor* editor)
{
    if (!editor)
        return;
    if (!editor->columnSelectionActive()) {
        const QString message = QStringLiteral("No column selection");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    const QStringList selectedRows = editor->columnSelectionTexts();
    if (selectedRows.isEmpty()) {
        const QString message = QStringLiteral("No column selection");
        editor->showComModeMessage(message);
        emit editor->editorStatusMessageRequested(message);
        return;
    }

    activeComEditor = editor->comModeActive() ? editor : activeComEditor;
    columnNumberEditor = editor;
    columnNumberOpenedFromCom = editor->comModeActive();
    ColumnNumberConfig config =
        inferColumnNumberConfig(selectedRows.constFirst());
    if (columnNumberTool) {
        columnNumberTool->configure(config, selectedRows.size(), selectedRows);
        columnNumberTool->showFor(anchor ? anchor : editor);
    }
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
