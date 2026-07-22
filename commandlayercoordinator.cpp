#include "commandlayercoordinator.h"

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
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {
bool focusBelongsToWindow(QWidget* anchor)
{
    QWidget* focus = QApplication::focusWidget();
    if (!focus || !anchor)
        return true;
    QWidget* anchorWindow = anchor->window();
    return !anchorWindow || focus->window() == anchorWindow;
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

QString commandCharacterForKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(QLatin1Char('a').unicode() + key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(QLatin1Char('0').unicode() + key - Qt::Key_0));
    if (key == Qt::Key_Space)
        return QStringLiteral(" ");
    if (key == Qt::Key_Minus)
        return QStringLiteral("-");
    if (key == Qt::Key_Plus)
        return QStringLiteral("+");
    return QString();
}

QString pickerItemLabel(const CommandLayerPickerItem& item)
{
    const QString location = item.line > 0
        ? QStringLiteral("%1:%2").arg(item.displayPath).arg(item.line)
        : item.displayPath;
    return QStringLiteral("%1\n%2").arg(item.name, location);
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

CommandLayerPanel::CommandLayerPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("commandLayerPanel"));
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral(
        "QFrame#commandLayerPanel { background:#0B1120; color:#F9FAFB; "
        "border:1px solid #38BDF8; border-radius:7px; }"
        "QLabel#commandLayerTitle { color:#7DD3FC; font-weight:700; "
        "letter-spacing:1px; }"
        "QLabel#commandLayerQuery { color:#F9FAFB; font-family:monospace; }"
        "QLabel#commandLayerFailure { color:#FCA5A5; "
        "background:#1F1115; padding:4px 7px; border-radius:3px; }"
        "QListWidget { border:0; background:#111827; color:#E5E7EB; "
        "outline:0; font-family:monospace; }"
        "QListWidget::item { padding:4px 7px; border-radius:3px; }"
        "QListWidget::item:selected { background:#0369A1; color:white; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 9);
    layout->setSpacing(4);

    titleLabel = new QLabel(this);
    titleLabel->setObjectName(QStringLiteral("commandLayerTitle"));
    layout->addWidget(titleLabel);

    queryLabel = new QLabel(this);
    queryLabel->setObjectName(QStringLiteral("commandLayerQuery"));
    layout->addWidget(queryLabel);

    candidateList = new QListWidget(this);
    candidateList->setObjectName(QStringLiteral("commandLayerCandidateList"));
    candidateList->setFocusPolicy(Qt::NoFocus);
    candidateList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(candidateList);

    failureLabel = new QLabel(this);
    failureLabel->setObjectName(QStringLiteral("commandLayerFailure"));
    failureLabel->setWordWrap(true);
    layout->addWidget(failureLabel);
    hide();
}

void CommandLayerPanel::showSearch(
    const QString& query,
    const QList<CommandLayerCommandMatch>& matches,
    int selectedIndex,
    const QString& failureReason,
    QWidget* anchor)
{
    titleLabel->setText(QStringLiteral("COMMAND LAYER  ·  F24 held"));
    queryLabel->setText(
        QStringLiteral("Query: %1")
            .arg(query.isEmpty() ? QStringLiteral("<empty>") : query));

    candidateList->clear();
    for (const CommandLayerCommandMatch& match : matches) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2")
                .arg(match.command.name, match.command.description),
            candidateList);
        item->setData(Qt::UserRole,
                      static_cast<int>(match.command.id));
        item->setToolTip(
            QStringLiteral("%1 match").arg(
                commandLayerMatchRankName(match.rank)));
    }
    if (!matches.isEmpty())
        candidateList->setCurrentRow(
            qBound(0, selectedIndex, matches.size() - 1));

    const int rows = qBound(1, candidateList->count(), 6);
    candidateList->setFixedHeight(rows * 27 + 4);
    failureLabel->setText(failureReason);
    failureLabel->setVisible(!failureReason.isEmpty());
    positionFor(anchor);
    show();
    raise();
}

void CommandLayerPanel::showHelp(
    const QList<CommandLayerCommandMetadata>& commands,
    QWidget* anchor)
{
    titleLabel->setText(QStringLiteral("COMMAND LAYER HELP"));
    queryLabel->setText(
        QStringLiteral("Enter, Esc, or a command key returns to search; "
                       "release F24 to return to the editor."));
    candidateList->clear();
    for (const CommandLayerCommandMetadata& command : commands) {
        candidateList->addItem(
            QStringLiteral("%1  —  %2")
                .arg(command.name, command.description));
    }
    candidateList->clearSelection();
    candidateList->setCurrentRow(-1);
    candidateList->setFixedHeight(
        qBound(1, candidateList->count(), 8) * 27 + 4);
    failureLabel->clear();
    failureLabel->hide();
    positionFor(anchor);
    show();
    raise();
}

QListWidget* CommandLayerPanel::candidateListWidget() const
{
    return candidateList;
}

QLabel* CommandLayerPanel::queryLabelWidget() const
{
    return queryLabel;
}

QLabel* CommandLayerPanel::failureLabelWidget() const
{
    return failureLabel;
}

void CommandLayerPanel::positionFor(QWidget* anchorWidget)
{
    QWidget* target = anchorWidget ? anchorWidget->window() : parentWidget();
    QWidget* host = parentWidget();
    if (!target || !host)
        return;

    const int availableWidth = qMax(360, target->width() - 32);
    const int panelWidth = qMin(760, availableWidth);
    resize(panelWidth, sizeHint().height());
    const QPoint targetOrigin = target == host
        ? QPoint(0, 0)
        : target->mapTo(host, QPoint(0, 0));
    const int x = targetOrigin.x()
        + qMax(8, (target->width() - panelWidth) / 2);
    const int y = targetOrigin.y()
        + qMax(8, target->height() - height() - 30);
    move(x, y);
}

CommandLayerCoordinator::CommandLayerCoordinator(
    QWidget* anchor,
    TabManager* tabManager,
    ProjectModel* projectModel,
    SemanticIndex* semanticIndex,
    NavigationCommandCoordinator* navigation,
    QObject* parent)
    : QObject(parent)
    , anchor(anchor)
    , tabManager(tabManager)
    , projectModel(projectModel)
    , semanticIndex(semanticIndex)
    , navigation(navigation)
{
    panel = std::make_unique<CommandLayerPanel>(anchor);
    picker = std::make_unique<CommandLayerPickerPanel>(anchor);
    picker->setFilterChangedHandler(
        [this](const QString& filter) { refreshPicker(filter); });
    picker->setItemActivatedHandler(
        [this](const CommandLayerPickerItem& item) {
            activatePickerItem(item);
        });
    picker->setCancelledHandler([this]() { finishPicker(); });

    columnNumberTool = std::make_unique<ColumnNumberToolPanel>(anchor);
    columnNumberTool->setCancelledHandler([this]() {
        if (columnNumberEditor)
            columnNumberEditor->setFocus(Qt::ShortcutFocusReason);
        columnNumberEditor.clear();
    });
    columnNumberTool->setApplyHandler(
        [this](const ColumnNumberConfig& config, int rows) {
            QPointer<MyCodeEditor> editor = columnNumberEditor;
            columnNumberEditor.clear();
            if (!editor)
                return;

            QString message;
            const bool replaceSelection =
                config.replaceMode
                == ColumnNumberReplaceMode::ReplaceSelection;
            if (!editor->applyColumnSelectionTexts(
                    previewColumnNumbers(config, rows),
                    replaceSelection,
                    &message)) {
                if (message.isEmpty())
                    message = QStringLiteral("No column selection");
                emit editor->editorStatusMessageRequested(message);
            }
            editor->setFocus(Qt::ShortcutFocusReason);
        });
}

CommandLayerCoordinator::~CommandLayerCoordinator()
{
    if (applicationFilterInstalled && qApp)
        qApp->removeEventFilter(this);
}

void CommandLayerCoordinator::connectSignals()
{
    if (connected)
        return;
    if (tabManager) {
        connect(tabManager,
                &TabManager::activeTabChanged,
                this,
                [this](MyCodeEditor* editor) {
            if (editor)
                lastEditor = editor;
            if (phase == Phase::Search)
                updateSearchPanel();
        });
    }
    installApplicationEventFilter();
    connected = true;
}

bool CommandLayerCoordinator::isActive() const
{
    return phase != Phase::Inactive;
}

bool CommandLayerCoordinator::isF24Held() const
{
    return f24Held;
}

QString CommandLayerCoordinator::query() const
{
    return queryText;
}

CommandLayerPanel* CommandLayerCoordinator::panelWidget() const
{
    return panel.get();
}

CommandLayerPickerPanel* CommandLayerCoordinator::pickerPanel() const
{
    return picker.get();
}

void CommandLayerCoordinator::installApplicationEventFilter()
{
    if (applicationFilterInstalled || !qApp)
        return;
    qApp->installEventFilter(this);
    applicationFilterInstalled = true;
}

bool CommandLayerCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (handleApplicationEvent(watched, event))
        return true;
    return QObject::eventFilter(watched, event);
}

bool CommandLayerCoordinator::handleApplicationEvent(QObject* watched,
                                                     QEvent* event)
{
    if (!event)
        return false;

    if (event->type() == QEvent::ApplicationDeactivate) {
        f24Held = false;
        if (phase == Phase::Search || phase == Phase::Help)
            leaveCommandLayer(false);
        return false;
    }

    if ((event->type() == QEvent::Resize
         || event->type() == QEvent::Move)
        && panel && panel->isVisible()
        && watched == (anchor ? anchor->window() : nullptr)) {
        if (phase == Phase::Help) {
            panel->showHelp(commandLayerCommandRegistry(), anchor);
        } else if (phase == Phase::Search) {
            panel->showSearch(queryText,
                              matches,
                              selectedMatch,
                              failureReason,
                              anchor);
        }
    }

    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F24
            || phase == Phase::Search
            || phase == Phase::Help
            || (phase == Phase::Inactive
                && isColumnNumberShortcutKey(keyEvent))) {
            keyEvent->accept();
            return true;
        }
        return false;
    }

    if (event->type() != QEvent::KeyPress
        && event->type() != QEvent::KeyRelease) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_F24)
        return handleF24Event(keyEvent);
    if (event->type() == QEvent::KeyPress)
        return handleKeyPress(keyEvent);
    return false;
}

bool CommandLayerCoordinator::handleF24Event(QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->isAutoRepeat()) {
        event->accept();
        return true;
    }

    if (event->type() == QEvent::KeyRelease) {
        const bool owned = f24Held || phase != Phase::Inactive;
        f24Held = false;
        if (phase == Phase::Search || phase == Phase::Help)
            leaveCommandLayer();
        event->accept();
        return owned;
    }

    if (phase == Phase::Picker) {
        f24Held = true;
        event->accept();
        return true;
    }
    if (phase == Phase::Search || phase == Phase::Help) {
        f24Held = true;
        event->accept();
        return true;
    }

    if (QApplication::activeModalWidget())
        return false;
    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible()) {
        return false;
    }
    if (!focusBelongsToWindow(anchor))
        return false;

    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        return false;
    lastEditor = editor;
    f24Held = true;
    enterSearch();
    event->accept();
    return true;
}

bool CommandLayerCoordinator::handleKeyPress(QKeyEvent* event)
{
    if (!event)
        return false;
    if (phase == Phase::Search)
        return handleSearchKey(event);
    if (phase == Phase::Help)
        return handleHelpKey(event);
    if (phase == Phase::Picker)
        return false;

    if (!isColumnNumberShortcutKey(event))
        return false;
    if (QApplication::activeModalWidget())
        return false;
    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible()) {
        return false;
    }
    if (!focusBelongsToWindow(anchor))
        return false;
    openColumnNumberToolForCurrentEditor();
    event->accept();
    return true;
}

bool CommandLayerCoordinator::handleSearchKey(QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter) {
        executeSelectedCommand();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Up) {
        moveSelection(-1);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Down) {
        moveSelection(1);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Backspace) {
        if (!queryText.isEmpty())
            queryText.chop(1);
        failureReason.clear();
        selectedMatch = 0;
        updateSearchPanel();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        queryText.clear();
        failureReason.clear();
        selectedMatch = 0;
        updateSearchPanel();
        event->accept();
        return true;
    }

    appendQueryCharacter(event);
    event->accept();
    return true;
}

bool CommandLayerCoordinator::handleHelpKey(QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Escape
        || event->key() == Qt::Key_Backspace) {
        enterSearch();
        event->accept();
        return true;
    }

    const QString character = commandCharacterForKey(event->key());
    if (!character.isEmpty()) {
        enterSearch();
        queryText.append(character);
        updateSearchPanel();
    }
    event->accept();
    return true;
}

void CommandLayerCoordinator::enterSearch(bool clearFailure)
{
    phase = Phase::Search;
    queryText.clear();
    matches.clear();
    selectedMatch = 0;
    if (clearFailure)
        failureReason.clear();
    updateSearchPanel();
}

void CommandLayerCoordinator::leaveCommandLayer(bool restoreFocus)
{
    phase = Phase::Inactive;
    queryText.clear();
    failureReason.clear();
    matches.clear();
    selectedMatch = 0;
    if (panel)
        panel->hide();
    if (restoreFocus) {
        MyCodeEditor* editor = currentEditorForLocalCommand();
        if (!editor)
            editor = lastEditor;
        if (editor)
            editor->setFocus(Qt::ShortcutFocusReason);
    }
}

void CommandLayerCoordinator::updateSearchPanel()
{
    if (phase != Phase::Search || !panel)
        return;

    const CommandLayerLineParseResult lineQuery =
        parseCommandLayerLineQuery(queryText);
    if (lineQuery.state == CommandLayerLineParseState::Valid) {
        matches.clear();
        if (const CommandLayerCommandMetadata* command =
                findCommandLayerCommand(QStringLiteral("go <number>"))) {
            CommandLayerCommandMatch match;
            match.command = *command;
            match.rank = CommandLayerMatchRank::Exact;
            match.registryIndex = 0;
            matches.append(match);
        }
        if (failureReason.startsWith(QStringLiteral("No command matches"))
            || failureReason.startsWith(QStringLiteral("Line number"))) {
            failureReason.clear();
        }
    } else if (lineQuery.state == CommandLayerLineParseState::Invalid) {
        matches.clear();
        failureReason = lineQuery.failureReason;
    } else {
        matches = commandLayerCommandMatches(queryText);
        if (!queryText.trimmed().isEmpty() && matches.isEmpty()) {
            failureReason =
                QStringLiteral("No command matches: %1").arg(queryText);
        } else if (failureReason.startsWith(
                       QStringLiteral("No command matches"))) {
            failureReason.clear();
        }
    }
    if (matches.isEmpty())
        selectedMatch = 0;
    else
        selectedMatch = qBound(0, selectedMatch, matches.size() - 1);
    panel->showSearch(queryText,
                      matches,
                      selectedMatch,
                      failureReason,
                      anchor);
}

void CommandLayerCoordinator::showHelp()
{
    phase = Phase::Help;
    queryText.clear();
    failureReason.clear();
    matches.clear();
    selectedMatch = 0;
    if (panel)
        panel->showHelp(commandLayerCommandRegistry(), anchor);
}

void CommandLayerCoordinator::moveSelection(int delta)
{
    if (matches.isEmpty())
        return;
    selectedMatch = qBound(0,
                           selectedMatch + delta,
                           matches.size() - 1);
    updateSearchPanel();
}

void CommandLayerCoordinator::appendQueryCharacter(QKeyEvent* event)
{
    const QString character = commandCharacterForKey(event ? event->key() : 0);
    if (character.isEmpty())
        return;
    queryText.append(character);
    failureReason.clear();
    selectedMatch = 0;
    updateSearchPanel();
}

void CommandLayerCoordinator::executeSelectedCommand()
{
    const CommandLayerLineParseResult lineQuery =
        parseCommandLayerLineQuery(queryText);
    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        editor = lastEditor;
    if (!editor) {
        completeCommand(QStringLiteral("No editor tab is available"));
        return;
    }
    lastEditor = editor;

    if (lineQuery.state == CommandLayerLineParseState::Invalid) {
        failureReason = lineQuery.failureReason;
        updateSearchPanel();
        emit editor->editorStatusMessageRequested(failureReason);
        return;
    }
    if (lineQuery.state == CommandLayerLineParseState::Valid) {
        handleRelativeLine(editor, lineQuery.line);
        return;
    }
    if (matches.isEmpty()) {
        failureReason = queryText.trimmed().isEmpty()
            ? QStringLiteral("Enter a command query")
            : QStringLiteral("No command matches: %1").arg(queryText);
        updateSearchPanel();
        emit editor->editorStatusMessageRequested(failureReason);
        return;
    }

    const CommandLayerCommandMetadata command =
        matches.at(qBound(0, selectedMatch, matches.size() - 1)).command;
    if (command.inputKind == CommandLayerCommandInputKind::PositiveInteger) {
        failureReason =
            QStringLiteral("go <number> requires a line number >= 1");
        updateSearchPanel();
        emit editor->editorStatusMessageRequested(failureReason);
        return;
    }
    executeCommand(editor, command);
}

void CommandLayerCoordinator::executeCommand(
    MyCodeEditor* editor,
    const CommandLayerCommandMetadata& command)
{
    switch (command.id) {
    case CommandLayerCommandId::GoLine:
        completeCommand(
            QStringLiteral("go <number> requires a line number >= 1"));
        return;
    case CommandLayerCommandId::GoModule:
        showPicker(editor, PickerMode::Module, command.name);
        return;
    case CommandLayerCommandId::GoPackage:
        showPicker(editor, PickerMode::Package, command.name);
        return;
    case CommandLayerCommandId::GoEndmodule:
        handleGoEndmodule(editor);
        return;
    case CommandLayerCommandId::AddSignal:
        handleAddSignal(editor);
        return;
    case CommandLayerCommandId::AddParameter:
        handleAddParameter(editor);
        return;
    case CommandLayerCommandId::AddPort:
        handleAddPort(editor);
        return;
    case CommandLayerCommandId::ClearRight:
        handleClearRight(editor);
        return;
    case CommandLayerCommandId::SelectBeginEnd:
        handleSelectBeginEnd(editor);
        return;
    case CommandLayerCommandId::Help:
        showHelp();
        return;
    }
}

void CommandLayerCoordinator::completeCommand(const QString& failure)
{
    queryText.clear();
    matches.clear();
    selectedMatch = 0;
    failureReason = failure;
    if (f24Held) {
        phase = Phase::Search;
        updateSearchPanel();
    } else {
        leaveCommandLayer();
    }
}

void CommandLayerCoordinator::reportFailure(MyCodeEditor* editor,
                                            const QString& message)
{
    if (editor && !message.isEmpty())
        emit editor->editorStatusMessageRequested(message);
}

MyCodeEditor* CommandLayerCoordinator::currentEditorForLocalCommand() const
{
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    return editor && editor->isEnabled() ? editor : nullptr;
}

void CommandLayerCoordinator::handleRelativeLine(MyCodeEditor* editor,
                                                 int moduleLine)
{
    if (!editor) {
        completeCommand(QStringLiteral("No editor tab is available"));
        return;
    }
    const QTextBlock block = editor->textCursor().block();
    CommandLayerRelativeLineQuery query;
    query.snapshot = semanticSnapshot();
    query.fileName = editor->documentFileName();
    query.currentModuleName = editor->currentModuleName();
    query.currentLine = block.isValid() ? block.blockNumber() + 1 : -1;
    query.requestedModuleLine = moduleLine;
    const CommandLayerRelativeLineResult result =
        service.relativeLineTarget(query);
    if (!result.ok) {
        reportFailure(editor, result.message);
        completeCommand(result.message);
        return;
    }
    if (navigation)
        navigation->navigateToFileAndLine(result.filePath,
                                          result.line,
                                          result.column);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("go %1").arg(moduleLine));
    completeCommand();
}

void CommandLayerCoordinator::handleAddPort(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->addPortRow(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear port append point");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::handleAddSignal(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->addSignalRow(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear signal insert point");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::handleAddParameter(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->addParameterRow(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No clear parameter insert point");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::handleGoEndmodule(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->goToFinalEndmodule(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No endmodule found");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::handleClearRight(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->clearSelectedAssignmentRhs(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No assignment RHS found");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::handleSelectBeginEnd(MyCodeEditor* editor)
{
    QString message;
    if (!editor || !editor->selectInsideBeginEnd(&message)) {
        if (message.isEmpty())
            message = QStringLiteral("No begin-end block");
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::openColumnNumberToolForCurrentEditor()
{
    handleColumnNumberTool(currentEditorForLocalCommand());
}

void CommandLayerCoordinator::handleColumnNumberTool(MyCodeEditor* editor)
{
    if (!editor)
        return;
    const QStringList selectedRows = editor->columnSelectionTexts();
    if (!editor->columnSelectionActive() || selectedRows.isEmpty()) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("No column selection"));
        return;
    }
    columnNumberEditor = editor;
    if (columnNumberTool) {
        columnNumberTool->configure(
            inferColumnNumberConfig(selectedRows.constFirst()),
            selectedRows.size(),
            selectedRows);
        columnNumberTool->showFor(anchor ? anchor : editor);
    }
}

void CommandLayerCoordinator::showPicker(MyCodeEditor* editor,
                                         PickerMode mode,
                                         const QString& command)
{
    if (!picker || !editor) {
        completeCommand(QStringLiteral("No editor tab is available"));
        return;
    }
    phase = Phase::Picker;
    activePickerMode = mode;
    activePickerCommand = command;
    activePickerEditor = editor;
    if (panel)
        panel->hide();
    picker->setPrompt(mode == PickerMode::Package
                          ? QStringLiteral("go package")
                          : QStringLiteral("go module"));
    picker->setEmptyText(mode == PickerMode::Package
                             ? QStringLiteral("No packages")
                             : QStringLiteral("No modules"));
    refreshPicker(QString());
    picker->showFor(anchor ? anchor : editor);
}

void CommandLayerCoordinator::refreshPicker(const QString& filter)
{
    if (!picker || phase != Phase::Picker)
        return;
    CommandLayerPickerQuery query;
    query.snapshot = semanticSnapshot();
    query.project = projectSnapshot();
    query.filter = filter;
    picker->setItems(activePickerMode == PickerMode::Package
                         ? service.packageItems(query)
                         : service.moduleItems(query));
}

void CommandLayerCoordinator::activatePickerItem(
    const CommandLayerPickerItem& item)
{
    QPointer<MyCodeEditor> sourceEditor = activePickerEditor;
    if (navigation && !item.filePath.isEmpty())
        navigation->navigateToFileAndLine(item.filePath, item.line, item.column);
    MyCodeEditor* targetEditor = currentEditorForLocalCommand();
    if (!targetEditor)
        targetEditor = sourceEditor;
    if (targetEditor) {
        lastEditor = targetEditor;
        emit targetEditor->editorStatusMessageRequested(
            QStringLiteral("%1 %2").arg(activePickerCommand, item.name));
    }
    finishPicker();
}

void CommandLayerCoordinator::finishPicker()
{
    if (picker)
        picker->hide();
    activePickerEditor.clear();
    activePickerCommand.clear();
    if (f24Held)
        enterSearch();
    else
        leaveCommandLayer();
}

ProjectSnapshot CommandLayerCoordinator::projectSnapshot() const
{
    return projectModel ? projectModel->snapshot() : ProjectSnapshot();
}

std::shared_ptr<const SemanticIndexSnapshot>
CommandLayerCoordinator::semanticSnapshot() const
{
    return semanticIndex ? semanticIndex->snapshot() : nullptr;
}

CommandLayerPickerPanel::CommandLayerPickerPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("commandLayerPicker"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(560);
    setMaximumWidth(820);
    setStyleSheet(QStringLiteral(
        "QFrame#commandLayerPicker { background:#111827; color:#F9FAFB; "
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
    searchEdit->setObjectName(QStringLiteral("commandLayerPickerSearch"));
    layout->addWidget(searchEdit);
    resultList = new QListWidget(this);
    resultList->setObjectName(QStringLiteral("commandLayerPickerResults"));
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

void CommandLayerPickerPanel::setItems(
    const QList<CommandLayerPickerItem>& items)
{
    currentItems = items;
    resultList->clear();
    if (currentItems.isEmpty()) {
        auto* item = new QListWidgetItem(emptyText, resultList);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable
                       & ~Qt::ItemIsEnabled);
        return;
    }
    for (int i = 0; i < currentItems.size(); ++i) {
        auto* item = new QListWidgetItem(
            pickerItemLabel(currentItems.at(i)), resultList);
        item->setData(Qt::UserRole, i);
    }
    resultList->setCurrentRow(0);
}

void CommandLayerPickerPanel::setPrompt(const QString& prompt)
{
    if (searchEdit)
        searchEdit->setPlaceholderText(prompt);
}

void CommandLayerPickerPanel::setEmptyText(const QString& text)
{
    emptyText = text;
}

void CommandLayerPickerPanel::showFor(QWidget* anchorWidget)
{
    if (searchEdit)
        searchEdit->clear();
    adjustSize();
    QWidget* target = anchorWidget ? anchorWidget->window() : nullptr;
    QRect rect;
    if (target)
        rect = target->geometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        rect = screen->availableGeometry();
    move(rect.center().x() - width() / 2,
         rect.top() + qMax(90, rect.height() / 5));
    show();
    raise();
    focusSearch();
}

void CommandLayerPickerPanel::focusSearch()
{
    if (searchEdit) {
        searchEdit->setFocus(Qt::ShortcutFocusReason);
        searchEdit->selectAll();
    }
}

void CommandLayerPickerPanel::setFilterChangedHandler(
    std::function<void(const QString&)> handler)
{
    filterChangedHandler = std::move(handler);
}

void CommandLayerPickerPanel::setItemActivatedHandler(
    std::function<void(const CommandLayerPickerItem&)> handler)
{
    itemActivatedHandler = std::move(handler);
}

void CommandLayerPickerPanel::setCancelledHandler(
    std::function<void()> handler)
{
    cancelledHandler = std::move(handler);
}

bool CommandLayerPickerPanel::eventFilter(QObject*, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
        return handleKey(static_cast<QKeyEvent*>(event));
    return false;
}

void CommandLayerPickerPanel::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event))
        return;
    QFrame::keyPressEvent(event);
}

void CommandLayerPickerPanel::activateCurrentItem()
{
    const int row = resultList ? resultList->currentRow() : -1;
    if (row < 0 || row >= currentItems.size())
        return;
    const CommandLayerPickerItem item = currentItems.at(row);
    hide();
    if (itemActivatedHandler)
        itemActivatedHandler(item);
}

void CommandLayerPickerPanel::cancel()
{
    hide();
    if (cancelledHandler)
        cancelledHandler();
}

void CommandLayerPickerPanel::moveSelection(int delta)
{
    if (!resultList || currentItems.isEmpty())
        return;
    resultList->setCurrentRow(
        qBound(0,
               resultList->currentRow() + delta,
               currentItems.size() - 1));
}

bool CommandLayerPickerPanel::handleKey(QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter) {
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
