#include "uitypography.h"
#include "uicontrols.h"
#include "commandlayercoordinator.h"

#include "columnnumbertool.h"
#include "insightvisualstyle.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "tabmanager.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFormLayout>
#include <QFont>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRadioButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
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

bool isEditorCompletionPopup(MyCodeEditor* editor, QWidget* popup)
{
    if (!editor || !popup)
        return false;

    const QList<QCompleter*> completers =
        editor->findChildren<QCompleter*>();
    for (QCompleter* completer : completers) {
        QAbstractItemView* candidate =
            completer ? completer->popup() : nullptr;
        if (candidate
            && (popup == candidate || popup == candidate->window())) {
            return true;
        }
    }
    return false;
}

bool matchesActionShortcut(
    const QKeyEvent* event,
    const char* actionId)
{
    if (!event || !actionId)
        return false;
    const QString shortcutText =
        effectiveActionShortcut(
            QString::fromLatin1(actionId));
    if (shortcutText.isEmpty())
        return false;
    const QKeySequence shortcut =
        QKeySequence::fromString(
            shortcutText,
            QKeySequence::PortableText);
    return shortcut.matches(
               QKeySequence(
                   event->keyCombination()))
        == QKeySequence::ExactMatch;
}

bool matchesCommandModeShortcut(
    const QKeyEvent* event)
{
    return matchesActionShortcut(
        event, ActionIds::ViewCommandMode);
}

bool matchesColumnNumberShortcut(
    const QKeyEvent* event)
{
    return matchesActionShortcut(
        event, ActionIds::InsertColumnNumbers);
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

ActionExecutionResult successfulCommandExecution(
    bool phaseManaged = false)
{
    ActionExecutionResult result;
    result.handled = true;
    result.succeeded = true;
    if (phaseManaged) {
        result.output.insert(
            QStringLiteral("commandLayer.phaseManaged"),
            true);
    }
    return result;
}

ActionExecutionResult failedCommandExecution(
    const QString& reason)
{
    ActionExecutionResult result;
    result.handled = true;
    result.failureReason = reason;
    return result;
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
    QButtonGroup* baseButtons = nullptr;
    QRadioButton* decimalRadio = nullptr;
    QRadioButton* hexadecimalRadio = nullptr;
    QRadioButton* octalRadio = nullptr;
    QRadioButton* binaryRadio = nullptr;
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
    int lineCount = 0;
    std::optional<ColumnNumberConfig> rememberedConfig;
    std::function<void(const ColumnNumberConfig&, int)> applyHandler;
    std::function<void()> cancelledHandler;

    ColumnNumberConfig currentConfig() const;
    void applyConfig(const ColumnNumberConfig& config);
    ColumnNumberConfig loadConfig(
        const ColumnNumberConfig& fallback) const;
    void saveConfig(const ColumnNumberConfig& config);
    void refreshFieldVisibility();
    void cancel();
    void apply();
    bool handleKey(QKeyEvent* event);
};

ColumnNumberToolPanel::ColumnNumberToolPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("columnNumberToolPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(330);
    InsightVisualStyle::applyPanel(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);

    auto* title = UiControls::label(QStringLiteral("Insert Numbers"), this);
    QFont titleFont = UiTypography::font(UiTypography::Role::PanelTitle);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);
    outer->addLayout(form);

    startSpin = UiControls::spinBox(this);
    startSpin->setObjectName(QStringLiteral("columnNumberStart"));
    startSpin->setRange(-1000000000, 1000000000);
    form->addRow(QStringLiteral("Start"), startSpin);

    auto* baseGroup = new QGroupBox(QStringLiteral("Format"), this);
    auto* baseLayout = new QGridLayout(baseGroup);
    baseLayout->setContentsMargins(8, 8, 8, 8);
    baseLayout->setHorizontalSpacing(12);
    baseLayout->setVerticalSpacing(4);
    baseButtons = new QButtonGroup(this);
    decimalRadio = UiControls::radioButton(QStringLiteral("Decimal"), baseGroup);
    hexadecimalRadio = UiControls::radioButton(QStringLiteral("Hexadecimal"), baseGroup);
    octalRadio = UiControls::radioButton(QStringLiteral("Octal"), baseGroup);
    binaryRadio = UiControls::radioButton(QStringLiteral("Binary"), baseGroup);
    decimalRadio->setObjectName(QStringLiteral("columnNumberBaseDecimal"));
    hexadecimalRadio->setObjectName(QStringLiteral("columnNumberBaseHexadecimal"));
    octalRadio->setObjectName(QStringLiteral("columnNumberBaseOctal"));
    binaryRadio->setObjectName(QStringLiteral("columnNumberBaseBinary"));
    baseButtons->addButton(decimalRadio,
                           static_cast<int>(ColumnNumberBase::Dec));
    baseButtons->addButton(hexadecimalRadio,
                           static_cast<int>(ColumnNumberBase::Hex));
    baseButtons->addButton(binaryRadio,
                           static_cast<int>(ColumnNumberBase::Bin));
    baseButtons->addButton(octalRadio,
                           static_cast<int>(ColumnNumberBase::Oct));
    baseLayout->addWidget(decimalRadio, 0, 0);
    baseLayout->addWidget(hexadecimalRadio, 0, 1);
    baseLayout->addWidget(octalRadio, 1, 0);
    baseLayout->addWidget(binaryRadio, 1, 1);
    form->addRow(baseGroup);

    styleCombo = UiControls::comboBox(this);
    styleCombo->setObjectName(QStringLiteral("columnNumberStyle"));
    styleCombo->addItem(QStringLiteral("Plain"),
                        static_cast<int>(ColumnNumberStyle::Plain));
    styleCombo->addItem(QStringLiteral("C-like"),
                        static_cast<int>(ColumnNumberStyle::CLike));
    styleCombo->addItem(QStringLiteral("SV unsized"),
                        static_cast<int>(ColumnNumberStyle::SvUnsized));
    styleCombo->addItem(QStringLiteral("SV sized"),
                        static_cast<int>(ColumnNumberStyle::SvSized));
    form->addRow(QStringLiteral("Style"), styleCombo);

    bitWidthLabel = UiControls::label(QStringLiteral("Bit width"), this);
    bitWidthSpin = UiControls::spinBox(this);
    bitWidthSpin->setRange(1, 4096);
    form->addRow(bitWidthLabel, bitWidthSpin);

    directionCombo = UiControls::comboBox(this);
    directionCombo->setObjectName(QStringLiteral("columnNumberDirection"));
    directionCombo->addItem(QStringLiteral("Up"),
                            static_cast<int>(ColumnNumberDirection::Up));
    directionCombo->addItem(QStringLiteral("Down"),
                            static_cast<int>(ColumnNumberDirection::Down));
    form->addRow(QStringLiteral("Direction"), directionCombo);

    stepSpin = UiControls::spinBox(this);
    stepSpin->setObjectName(QStringLiteral("columnNumberStep"));
    stepSpin->setRange(0, 1000000000);
    stepSpin->setValue(1);
    form->addRow(QStringLiteral("Step"), stepSpin);

    repeatSpin = UiControls::spinBox(this);
    repeatSpin->setObjectName(QStringLiteral("columnNumberRepeat"));
    repeatSpin->setRange(1, 1000000);
    repeatSpin->setValue(1);
    form->addRow(QStringLiteral("Repeat"), repeatSpin);

    digitWidthModeCombo = UiControls::comboBox(this);
    digitWidthModeCombo->setObjectName(QStringLiteral("columnNumberDigitWidthMode"));
    digitWidthModeCombo->addItem(QStringLiteral("auto"), 0);
    digitWidthModeCombo->addItem(QStringLiteral("fixed"), 1);
    form->addRow(QStringLiteral("Digit width"), digitWidthModeCombo);

    digitWidthLabel = UiControls::label(QStringLiteral("Fixed digits"), this);
    digitWidthSpin = UiControls::spinBox(this);
    digitWidthSpin->setRange(1, 1024);
    form->addRow(digitWidthLabel, digitWidthSpin);

    padCombo = UiControls::comboBox(this);
    padCombo->setObjectName(QStringLiteral("columnNumberPad"));
    padCombo->addItem(QStringLiteral("none"),
                      static_cast<int>(ColumnNumberPad::None));
    padCombo->addItem(QStringLiteral("space"),
                      static_cast<int>(ColumnNumberPad::Space));
    padCombo->addItem(QStringLiteral("zero"),
                      static_cast<int>(ColumnNumberPad::Zero));
    form->addRow(QStringLiteral("Pad"), padCombo);

    hexCaseLabel = UiControls::label(QStringLiteral("Hex case"), this);
    hexCaseCombo = UiControls::comboBox(this);
    hexCaseCombo->addItem(QStringLiteral("Upper"), 1);
    hexCaseCombo->addItem(QStringLiteral("Lower"), 0);
    form->addRow(hexCaseLabel, hexCaseCombo);

    replaceModeCombo = UiControls::comboBox(this);
    replaceModeCombo->addItem(
        QStringLiteral("replace selection"),
        static_cast<int>(ColumnNumberReplaceMode::ReplaceSelection));
    replaceModeCombo->addItem(
        QStringLiteral("insert at column"),
        static_cast<int>(ColumnNumberReplaceMode::InsertAtColumn));
    form->addRow(QStringLiteral("Replace mode"), replaceModeCombo);

    const QList<QWidget*> watched = {
        startSpin,
        decimalRadio,
        hexadecimalRadio,
        octalRadio,
        binaryRadio,
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

    auto refresh = [this]() { refreshFieldVisibility(); };
    connect(baseButtons,
            &QButtonGroup::idToggled,
            this,
            [refresh](int, bool checked) {
                if (checked)
                    refresh();
            });
    connect(styleCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            refresh);
    connect(digitWidthModeCombo,
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
    applyConfig(loadConfig(config));
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
        baseButtons && baseButtons->checkedId() >= 0
            ? baseButtons->checkedId()
            : static_cast<int>(ColumnNumberBase::Dec));
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
    if (baseButtons) {
        if (QAbstractButton* button =
                baseButtons->button(static_cast<int>(config.base))) {
            button->setChecked(true);
        }
    }
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
    refreshFieldVisibility();
}

ColumnNumberConfig ColumnNumberToolPanel::loadConfig(
    const ColumnNumberConfig& fallback) const
{
    if (rememberedConfig.has_value())
        return *rememberedConfig;

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("ZeroSlack"),
                       QStringLiteral("ZeroSlack"));
    settings.beginGroup(QStringLiteral("columnNumberTool/v1"));
    if (!settings.contains(QStringLiteral("start")))
        return fallback;
    ColumnNumberConfig config = fallback;
    config.start = settings.value(QStringLiteral("start"), config.start).toLongLong();
    config.base = static_cast<ColumnNumberBase>(
        settings.value(QStringLiteral("base"), static_cast<int>(config.base)).toInt());
    config.style = static_cast<ColumnNumberStyle>(
        settings.value(QStringLiteral("style"), static_cast<int>(config.style)).toInt());
    config.bitWidth = settings.value(QStringLiteral("bitWidth"), config.bitWidth).toInt();
    config.direction = static_cast<ColumnNumberDirection>(
        settings.value(QStringLiteral("direction"), static_cast<int>(config.direction)).toInt());
    config.step = settings.value(QStringLiteral("step"), config.step).toLongLong();
    config.repeat = settings.value(QStringLiteral("repeat"), config.repeat).toInt();
    config.fixedDigitWidth = settings.value(
        QStringLiteral("fixedDigitWidth"), config.fixedDigitWidth).toBool();
    config.digitWidth = settings.value(
        QStringLiteral("digitWidth"), config.digitWidth).toInt();
    config.pad = static_cast<ColumnNumberPad>(
        settings.value(QStringLiteral("pad"), static_cast<int>(config.pad)).toInt());
    config.uppercaseHex = settings.value(
        QStringLiteral("uppercaseHex"), config.uppercaseHex).toBool();
    config.replaceMode = static_cast<ColumnNumberReplaceMode>(
        settings.value(QStringLiteral("replaceMode"),
                       static_cast<int>(config.replaceMode)).toInt());
    return config;
}

void ColumnNumberToolPanel::saveConfig(
    const ColumnNumberConfig& config)
{
    rememberedConfig = config;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("ZeroSlack"),
                       QStringLiteral("ZeroSlack"));
    settings.beginGroup(QStringLiteral("columnNumberTool/v1"));
    settings.setValue(QStringLiteral("start"), config.start);
    settings.setValue(QStringLiteral("base"), static_cast<int>(config.base));
    settings.setValue(QStringLiteral("style"), static_cast<int>(config.style));
    settings.setValue(QStringLiteral("bitWidth"), config.bitWidth);
    settings.setValue(QStringLiteral("direction"), static_cast<int>(config.direction));
    settings.setValue(QStringLiteral("step"), config.step);
    settings.setValue(QStringLiteral("repeat"), config.repeat);
    settings.setValue(QStringLiteral("fixedDigitWidth"), config.fixedDigitWidth);
    settings.setValue(QStringLiteral("digitWidth"), config.digitWidth);
    settings.setValue(QStringLiteral("pad"), static_cast<int>(config.pad));
    settings.setValue(QStringLiteral("uppercaseHex"), config.uppercaseHex);
    settings.setValue(QStringLiteral("replaceMode"), static_cast<int>(config.replaceMode));
    settings.sync();
}

void ColumnNumberToolPanel::refreshFieldVisibility()
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
    saveConfig(config);
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
    InsightVisualStyle::applyPanel(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 9);
    layout->setSpacing(4);

    titleLabel = UiControls::label(this);
    titleLabel->setObjectName(QStringLiteral("commandLayerTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    layout->addWidget(titleLabel);

    queryLabel = UiControls::label(this);
    queryLabel->setObjectName(QStringLiteral("commandLayerQuery"));
    layout->addWidget(queryLabel);

    candidateList = UiControls::listWidget(this);
    candidateList->setObjectName(QStringLiteral("commandLayerCandidateList"));
    candidateList->setFocusPolicy(Qt::NoFocus);
    candidateList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(candidateList);

    failureLabel = UiControls::label(this);
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
    const QString commandShortcut =
        effectiveActionShortcut(
            QString::fromLatin1(
                ActionIds::ViewCommandMode));
    titleLabel->setText(
        QStringLiteral("COMMAND MODE  ·  %1 held")
            .arg(commandShortcut));
    queryLabel->setText(
        QStringLiteral("Query: %1")
            .arg(query.isEmpty() ? QStringLiteral("<empty>") : query));

    candidateList->clear();
    for (const CommandLayerCommandMatch& match : matches) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2")
                .arg(match.command.name, match.command.description),
            candidateList);
        item->setData(Qt::UserRole, match.command.actionId);
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
    const QList<ActionCatalogEntry>& entries,
    QWidget* anchor)
{
    const QString commandShortcut =
        effectiveActionShortcut(
            QString::fromLatin1(
                ActionIds::ViewCommandMode));
    titleLabel->setText(QStringLiteral("ACTION CATALOG"));
    queryLabel->setText(
        QStringLiteral(
            "All registered actions and aliases. Enter, Esc, or a command key "
            "returns to %1 search; release %1 to return to the editor.")
            .arg(commandShortcut));
    candidateList->clear();
    for (const ActionCatalogEntry& entry : entries) {
        candidateList->addItem(
            QStringLiteral("%1 | %2")
                .arg(entry.canonicalName, entry.displayText));
        candidateList->item(candidateList->count() - 1)
            ->setData(Qt::UserRole, entry.actionId);
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
    QObject* parent,
    ActionExecutionHost* applicationActionHost)
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
    actionExecutionHost.setFallbackHost(
        applicationActionHost);
    registerActionExecutionRoutes();
}

CommandLayerCoordinator::~CommandLayerCoordinator()
{
    if (applicationFilterInstalled && qApp)
        qApp->removeEventFilter(this);
}

void CommandLayerCoordinator::registerActionExecutionRoutes()
{
    using EditorAction = bool (MyCodeEditor::*)(QString*);

    const auto bind =
        [this](const QString& route,
               CommandLayerActionExecutionHost::RouteHandler handler) {
        QString failure;
        const bool registered =
            actionExecutionHost.bindRoute(
                route, std::move(handler), &failure);
        Q_ASSERT_X(registered,
                   "CommandLayerCoordinator",
                   "duplicate or invalid action execution route");
        Q_UNUSED(failure);
    };
    const auto bindEditor =
        [this, &bind](const QString& route,
                      EditorAction action,
                      const QString& fallbackFailure) {
        bind(route,
             [this, action, fallbackFailure](
                 const ActionDescriptor&,
                 const ActionInvocation&) {
            MyCodeEditor* editor = executingActionEditor;
            if (!editor) {
                return failedCommandExecution(
                    QStringLiteral("No editor tab is available"));
            }
            QString message;
            if (!(editor->*action)(&message)) {
                return failedCommandExecution(
                    message.isEmpty() ? fallbackFailure : message);
            }
            return successfulCommandExecution();
        });
    };

    bind(QStringLiteral("ui.commandMode.show"),
         [this](const ActionDescriptor& descriptor,
                const ActionInvocation&) {
        if (!beginCommandShortcutHold()) {
            return failedCommandExecution(
                QStringLiteral(
                    "Command Mode cannot take editor input ownership."));
        }
        if (panel) {
            panel->setProperty(
                "entryActionId",
                descriptor.id);
        }
        return successfulCommandExecution(true);
    });
    bind(QStringLiteral("editor.columnNumbers.show"),
         [this](const ActionDescriptor& descriptor,
                const ActionInvocation&) {
        MyCodeEditor* editor = executingActionEditor;
        if (!editor) {
            return failedCommandExecution(
                QStringLiteral("No editor tab is available"));
        }
        const QStringList selectedRows =
            editor->columnSelectionTexts();
        if (!editor->columnSelectionActive()
            || selectedRows.isEmpty()) {
            return failedCommandExecution(
                QStringLiteral("No column selection"));
        }
        if (!columnNumberTool) {
            return failedCommandExecution(
                QStringLiteral(
                    "Column Number Tool is unavailable"));
        }

        if (phase == Phase::Search
            || phase == Phase::Help) {
            leaveCommandLayer(false);
        }
        columnNumberEditor = editor;
        columnNumberTool->setProperty(
            "entryActionId", descriptor.id);
        columnNumberTool->configure(
            inferColumnNumberConfig(
                selectedRows.constFirst()),
            selectedRows.size(),
            selectedRows);
        columnNumberTool->showFor(
            anchor ? anchor : editor);
        return successfulCommandExecution(true);
    });

    bind(QStringLiteral("editor.navigation.goLine"),
         [this](const ActionDescriptor&,
                const ActionInvocation& invocation) {
        return executeRelativeLineAction(invocation);
    });
    bind(QStringLiteral("editor.navigation.goModule"),
         [this](const ActionDescriptor& descriptor,
                const ActionInvocation&) {
        MyCodeEditor* editor = executingActionEditor;
        if (!editor || !picker) {
            return failedCommandExecution(
                QStringLiteral("No editor tab is available"));
        }
        showPicker(
            editor,
            PickerMode::Module,
            descriptor.aliasForSurface(
                          ActionSurface::CommandLayer)
                .token);
        return successfulCommandExecution(true);
    });
    bind(QStringLiteral("editor.navigation.goPackage"),
         [this](const ActionDescriptor& descriptor,
                const ActionInvocation&) {
        MyCodeEditor* editor = executingActionEditor;
        if (!editor || !picker) {
            return failedCommandExecution(
                QStringLiteral("No editor tab is available"));
        }
        showPicker(
            editor,
            PickerMode::Package,
            descriptor.aliasForSurface(
                          ActionSurface::CommandLayer)
                .token);
        return successfulCommandExecution(true);
    });
    bindEditor(QStringLiteral("editor.navigation.goEndmodule"),
               &MyCodeEditor::goToFinalEndmodule,
               QStringLiteral("No endmodule found"));
    bind(
        QStringLiteral("action.repeatLast"),
        [this](const ActionDescriptor&,
               const ActionInvocation&) {
            return applicationActionExecutionHistory()
                .repeatLast(actionExecutionHost);
        });
    bindEditor(
        QStringLiteral("editor.structure.clearAssignmentRhs"),
        &MyCodeEditor::clearSelectedAssignmentRhs,
        QStringLiteral("No assignment RHS found"));
    bindEditor(QStringLiteral("editor.selection.beginEnd"),
               &MyCodeEditor::selectInsideBeginEnd,
               QStringLiteral("No begin-end block"));
    bindEditor(QStringLiteral("editor.selection.delete"),
               &MyCodeEditor::deleteSelectedContent,
               QStringLiteral("No selected content"));
    bindEditor(
        QStringLiteral("editor.mode.signalSelection"),
        &MyCodeEditor::startSignalSelectionMode,
        QStringLiteral("Signal selection is unavailable"));
    bindEditor(QStringLiteral("editor.lines.delete"),
               &MyCodeEditor::deleteLines,
               QStringLiteral("No logical line is available"));
    bindEditor(
        QStringLiteral("editor.lines.join"),
        &MyCodeEditor::joinLines,
        QStringLiteral(
            "At least two logical lines are required"));
    bindEditor(
        QStringLiteral("editor.lines.moveUp"),
        &MyCodeEditor::moveLinesUp,
        QStringLiteral(
            "The selected logical lines cannot move upward"));
    bindEditor(
        QStringLiteral("editor.lines.moveDown"),
        &MyCodeEditor::moveLinesDown,
        QStringLiteral(
            "The selected logical lines cannot move downward"));
    bindEditor(
        QStringLiteral(
            "editor.multicursor.addNextOccurrence"),
        &MyCodeEditor::addNextSymbolOccurrence,
        QStringLiteral(
            "No structural symbol occurrence is available"));
    bindEditor(
        QStringLiteral(
            "editor.multicursor.selectScopeOccurrences"),
        &MyCodeEditor::selectAllSymbolOccurrences,
        QStringLiteral(
            "No structural symbol occurrence is available"));
    bindEditor(
        QStringLiteral("editor.selection.expandSmart"),
        &MyCodeEditor::expandSmartSelection,
        QStringLiteral(
            "No larger structural selection is available"));
    bindEditor(
        QStringLiteral(
            "editor.navigation.nextSelectedSymbolOccurrence"),
        &MyCodeEditor::goToNextSelectedSymbolOccurrence,
        QStringLiteral(
            "No next selected-symbol occurrence is available"));
    bindEditor(
        QStringLiteral(
            "editor.navigation.previousSelectedSymbolOccurrence"),
        &MyCodeEditor::goToPreviousSelectedSymbolOccurrence,
        QStringLiteral(
            "No previous selected-symbol occurrence is available"));
    bind(QStringLiteral("ui.actionCatalog"),
         [this](const ActionDescriptor&,
                const ActionInvocation&) {
        showHelp();
        return successfulCommandExecution(true);
    });
}

ActionExecutionResult
CommandLayerCoordinator::executeRelativeLineAction(
    const ActionInvocation& invocation)
{
    MyCodeEditor* editor = executingActionEditor;
    if (!editor) {
        return failedCommandExecution(
            QStringLiteral("No editor tab is available"));
    }

    bool lineValid = false;
    const int moduleLine =
        invocation.parameters.value(QStringLiteral("line"))
            .toInt(&lineValid);
    if (!lineValid || moduleLine < 1) {
        return failedCommandExecution(
            QStringLiteral(
                "go <number> requires a line number >= 1"));
    }

    const QTextBlock block = editor->textCursor().block();
    CommandLayerRelativeLineQuery query;
    query.snapshot = semanticSnapshot();
    query.fileName = editor->documentFileName();
    query.currentModuleName = editor->currentModuleName();
    query.currentLine =
        block.isValid() ? block.blockNumber() + 1 : -1;
    query.requestedModuleLine = moduleLine;
    const CommandLayerRelativeLineResult result =
        service.relativeLineTarget(query);
    if (!result.ok)
        return failedCommandExecution(result.message);

    if (navigation) {
        navigation->navigateToFileAndLine(
            result.filePath, result.line, result.column);
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral("go %1").arg(moduleLine));
    return successfulCommandExecution();
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

bool CommandLayerCoordinator::executePaletteCommand(
    const QString& actionId,
    const QVariantMap& parameters)
{
    const auto found = std::find_if(
        commandLayerCommandRegistry().cbegin(),
        commandLayerCommandRegistry().cend(),
        [&actionId](const CommandLayerCommandMetadata& command) {
            return command.actionId == actionId;
        });
    if (found == commandLayerCommandRegistry().cend())
        return false;

    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        editor = lastEditor;
    if (!editor)
        return false;

    ActionInvocation invocation;
    invocation.workspaceId = projectSnapshot().workspaceRoot;
    invocation.parameters = parameters;
    executeCommand(editor, *found, invocation);
    return true;
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
        f24TapCandidate = false;
        cancelDirectGesture();
        if (phase == Phase::Search || phase == Phase::Help)
            leaveCommandLayer(false);
        return false;
    }

    if (f24Held
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::Wheel)) {
        f24TapCandidate = false;
        cancelDirectGesture();
    }

    if ((event->type() == QEvent::Resize
         || event->type() == QEvent::Move)
        && panel && panel->isVisible()
        && watched == (anchor ? anchor->window() : nullptr)) {
        if (phase == Phase::Help) {
            panel->showHelp(unifiedActionCatalog(), anchor);
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
        if (matchesCommandModeShortcut(keyEvent)
            || phase == Phase::Search
            || phase == Phase::Help
            || (phase == Phase::Inactive
                && matchesColumnNumberShortcut(keyEvent))) {
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
    if (matchesCommandModeShortcut(keyEvent))
        return handleCommandShortcutEvent(keyEvent);
    if (event->type() == QEvent::KeyPress)
        return handleKeyPress(keyEvent);
    if (event->type() == QEvent::KeyRelease)
        return handleDirectGestureKeyRelease(keyEvent);
    return false;
}

bool CommandLayerCoordinator::handleCommandShortcutEvent(
    QKeyEvent* event)
{
    if (!event)
        return false;
    if (event->isAutoRepeat()) {
        event->accept();
        return true;
    }

    if (event->type() == QEvent::KeyRelease) {
        const bool owned = f24Held || phase != Phase::Inactive;
        const bool repeatTap = f24Held
            && f24TapCandidate
            && pendingDirectKey == 0
            && queryText.isEmpty()
            && phase == Phase::Search;
        f24Held = false;
        f24TapCandidate = false;
        cancelDirectGesture();
        if (repeatTap)
            repeatLastActionFromTap();
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

    const ActionDescriptor* descriptor =
        findActionById(
            QString::fromLatin1(
                ActionIds::ViewCommandMode));
    if (!descriptor)
        return false;
    const ActionExecutionResult result =
        executeAction(
            *descriptor,
            actionExecutionHost,
            ActionInvocation());
    if (!result.succeeded)
        return false;
    event->accept();
    return true;
}

bool CommandLayerCoordinator::beginCommandShortcutHold()
{
    if (QApplication::activeModalWidget())
        return false;

    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        return false;

    QWidget* popup = QApplication::activePopupWidget();
    const bool completionPopupActive =
        popup && popup->isVisible()
        && isEditorCompletionPopup(editor, popup);
    if (!focusBelongsToWindow(anchor) && !completionPopupActive)
        return false;
    if (popup && popup->isVisible() && !completionPopupActive)
        return false;

    // Completion candidates are owned by the editor interaction-mode
    // controller.  Exit that mode before applying the general popup guard so
    // the command shortcut can replace an application-owned completion popup,
    // while menus and other independent popups continue to block Command Mode.
    editor->exitInteractionModes(
        EditorModeExitReason::ExternalControl);
    if (QWidget* remainingPopup = QApplication::activePopupWidget();
        remainingPopup && remainingPopup->isVisible()) {
        return false;
    }
    lastEditor = editor;
    f24Held = true;
    f24TapCandidate = true;
    cancelDirectGesture();
    enterSearch();
    return true;
}

bool CommandLayerCoordinator::handleDirectGestureKeyRelease(
    QKeyEvent* event)
{
    if (!event || event->isAutoRepeat()
        || pendingDirectKey == 0
        || event->key() != pendingDirectKey) {
        return false;
    }

    const bool execute = f24Held
        && phase == Phase::Search
        && !QApplication::activeModalWidget();
    event->accept();
    if (execute)
        executeDirectGesture();
    else
        cancelDirectGesture();
    return true;
}

bool CommandLayerCoordinator::beginDirectGesture(QKeyEvent* event)
{
    if (!event || !f24Held || phase != Phase::Search
        || !queryText.isEmpty() || pendingDirectKey != 0
        || event->isAutoRepeat()) {
        return false;
    }

    if (event->key() != Qt::Key_D)
        return false;

    pendingDirectKey = event->key();
    pendingDirectActionId = QString::fromLatin1(
        ActionIds::EditDeleteSelection);
    f24TapCandidate = false;
    failureReason = QStringLiteral("Release D: delete selection");
    updateSearchPanel();
    event->accept();
    return true;
}

void CommandLayerCoordinator::cancelDirectGesture()
{
    pendingDirectKey = 0;
    pendingDirectActionId.clear();
}

void CommandLayerCoordinator::executeDirectGesture()
{
    const QString actionId = pendingDirectActionId;
    cancelDirectGesture();
    failureReason.clear();

    const ActionDescriptor* descriptor = findActionById(actionId);
    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        editor = lastEditor;
    if (!descriptor || !editor) {
        completeCommand(QStringLiteral("No editor tab is available"));
        return;
    }

    executingActionEditor = editor;
    ActionInvocation invocation;
    invocation.workspaceId = projectSnapshot().workspaceRoot;
    const ActionExecutionResult result = executeAction(
        *descriptor, actionExecutionHost, invocation);
    executingActionEditor.clear();
    if (!result.succeeded) {
        const QString message = !result.failureReason.isEmpty()
            ? result.failureReason
            : result.message;
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }
    completeCommand();
}

void CommandLayerCoordinator::repeatLastActionFromTap()
{
    MyCodeEditor* editor = currentEditorForLocalCommand();
    if (!editor)
        editor = lastEditor;
    if (!editor)
        return;

    executingActionEditor = editor;
    const ActionExecutionResult result =
        applicationActionExecutionHistory().repeatLast(
            actionExecutionHost);
    executingActionEditor.clear();
    if (!result.succeeded) {
        const QString message = !result.failureReason.isEmpty()
            ? result.failureReason
            : result.message;
        if (!message.isEmpty())
            reportFailure(editor, message);
    }
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

    if (!matchesColumnNumberShortcut(event))
        return false;
    if (QApplication::activeModalWidget())
        return false;
    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible()) {
        return false;
    }
    if (!focusBelongsToWindow(anchor))
        return false;
    MyCodeEditor* editor = currentEditorForLocalCommand();
    const ActionDescriptor* descriptor =
        findActionById(
            QString::fromLatin1(
                ActionIds::InsertColumnNumbers));
    if (!editor || !descriptor)
        return false;
    executingActionEditor = editor;
    const ActionExecutionResult result =
        executeAction(
            *descriptor,
            actionExecutionHost,
            ActionInvocation());
    executingActionEditor.clear();
    if (!result.succeeded) {
        reportFailure(
            editor,
            result.failureReason.isEmpty()
                ? result.message
                : result.failureReason);
    }
    event->accept();
    return true;
}

bool CommandLayerCoordinator::handleSearchKey(QKeyEvent* event)
{
    if (!event)
        return false;
    if (pendingDirectKey != 0) {
        if (event->key() == Qt::Key_Escape) {
            f24TapCandidate = false;
            cancelDirectGesture();
            failureReason.clear();
            updateSearchPanel();
        }
        if (!event->isAutoRepeat())
            f24TapCandidate = false;
        event->accept();
        return true;
    }
    if (beginDirectGesture(event))
        return true;
    if (!event->isAutoRepeat())
        f24TapCandidate = false;
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
        panel->showHelp(unifiedActionCatalog(), anchor);
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
    if (!event)
        return;
    QString text = event->text();
    if (text.isEmpty())
        text = commandCharacterForKey(event->key());
    QString printable;
    printable.reserve(text.size());
    for (const QChar character : text) {
        if (!character.isNull() && character.isPrint())
            printable.append(character);
    }
    if (printable.isEmpty())
        return;
    queryText.append(printable);
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
        const CommandLayerCommandMetadata* command =
            findCommandLayerCommand(
                QStringLiteral("go <number>"));
        if (!command) {
            completeCommand(
                QStringLiteral(
                    "Command Layer action is not registered: "
                    "navigation.goLine"));
            return;
        }
        ActionInvocation invocation;
        invocation.workspaceId =
            projectSnapshot().workspaceRoot;
        invocation.parameters.insert(
            QStringLiteral("line"), lineQuery.line);
        executeCommand(editor, *command, invocation);
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
        executeCommand(editor, command);
        return;
    }
    executeCommand(editor, command);
}

void CommandLayerCoordinator::executeCommand(
    MyCodeEditor* editor,
    const CommandLayerCommandMetadata& command,
    const ActionInvocation& requestedInvocation)
{
    if (!editor) {
        completeCommand(
            QStringLiteral("No editor tab is available"));
        return;
    }

    ActionInvocation invocation = requestedInvocation;
    if (invocation.workspaceId.isEmpty())
        invocation.workspaceId = projectSnapshot().workspaceRoot;

    executingActionEditor = editor;
    const ActionExecutionResult result =
        executeCommandLayerCommand(
            command, actionExecutionHost, invocation);
    executingActionEditor.clear();

    if (!result.succeeded) {
        const QString message =
            !result.failureReason.trimmed().isEmpty()
            ? result.failureReason
            : (!result.message.trimmed().isEmpty()
                   ? result.message
                   : QStringLiteral(
                         "Command Layer action failed: %1")
                         .arg(command.actionId));
        reportFailure(editor, message);
        completeCommand(message);
        return;
    }

    if (!result.output
             .value(QStringLiteral(
                 "commandLayer.phaseManaged"))
             .toBool()) {
        completeCommand();
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
    InsightVisualStyle::applyPanel(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    searchEdit = UiControls::lineEdit(this);
    searchEdit->setObjectName(QStringLiteral("commandLayerPickerSearch"));
    InsightVisualStyle::applySearchField(searchEdit);
    layout->addWidget(searchEdit);
    resultList = UiControls::listWidget(this);
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
    QWidget* target = anchorWidget ? anchorWidget->window() : nullptr;
    QScreen* targetScreen = target ? target->screen() : screen();
    const QRect available = targetScreen->availableGeometry();
    const QSize preferred(fontMetrics().horizontalAdvance(QLatin1Char('M')) * 56,
                          sizeHint().height());
    resize(preferred.expandedTo(minimumSizeHint()).boundedTo(available.size() * .95));
    QRect rect;
    if (target)
        rect = target->geometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        rect = screen->availableGeometry();
    move(qBound(available.left(), rect.center().x() - width() / 2,
                available.right() - width() + 1),
         qBound(available.top(), rect.top() + rect.height() / 5,
                available.bottom() - height() + 1));
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
