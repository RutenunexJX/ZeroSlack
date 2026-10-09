#include "stimulusdialog.h"
#include <QApplication>
#include "../core/stimulus.h"
#include "scoreboarddialog.h"
#include "../core/testbench.h"
#include "uistyle.h"
#include "componentpath.h"
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaToolButton.h>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QScreen>
#include <QPointer>
#include <QDynamicPropertyChangeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <array>

namespace simdock
{
namespace
{
// Only the versioned public C ABI is used. Qt widgets stay owned by their DLL.
struct WaveEditorApi
{
    using Create = int (*)(const char *, size_t, const char *, QWidget *, QWidget **, char *,
                           size_t) noexcept;
    using Snapshot = int (*)(QWidget *, char *, size_t, size_t *, char *, size_t) noexcept;
    using Update = int (*)(QWidget *, const char *, size_t, char *, size_t) noexcept;
    QLibrary ela, library;
    Create create = nullptr;
    Snapshot snapshot = nullptr;
    Update update = nullptr;
    QString error;
    WaveEditorApi()
        : ela(QDir(componentDirectory()).filePath(QStringLiteral("WaveWorkbenchEla"))),
          library(QDir(componentDirectory()).filePath(QStringLiteral("wavewidgets")))
    {
        ela.setLoadHints(QLibrary::PreventUnloadHint);
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if (!ela.load() || !library.load())
        {
            error = QStringLiteral("The waveform editor component is missing or incompatible. Install a "
                                   "ZeroSlack package that includes Tickx's editor.\n%1")
                        .arg(ela.isLoaded() ? library.errorString() : ela.errorString());
            return;
        }
        const auto abi = reinterpret_cast<int (*)() noexcept>(library.resolve("wavewidgets_abi_version"));
        create = reinterpret_cast<Create>(library.resolve("wavewidgets_create_stimulus_editor_v1"));
        snapshot = reinterpret_cast<Snapshot>(library.resolve("wavewidgets_stimulus_project_v1"));
        update = reinterpret_cast<Update>(library.resolve("wavewidgets_update_stimulus_editor_v1"));
        if (!abi || abi() != 1 || !create || !snapshot)
        {
            create = nullptr;
            error = QStringLiteral("The installed waveform component does not support the stimulus editor. "
                                   "Update the ZeroSlack package.");
        }
    }
};
WaveEditorApi &api()
{
    static WaveEditorApi instance;
    return instance;
}
} // namespace

StimulusDialog::StimulusDialog(const Module &module, const Scan &scan, const QJsonObject &saved,
                               qint64 durationNs, SaveHandler saveHandler, QWidget *parent)
    : StimulusDialog(module, resolveStimulus(module, scan), saved, durationNs,
        [saveHandler = std::move(saveHandler)](const QJsonObject &drawing, const QString &, QString *error) {
            return !saveHandler || saveHandler(drawing, error);
        }, parent) {}

StimulusDialog::StimulusDialog(const Module &module, const StimulusSemantics &semantics,
                               const QJsonObject &saved, qint64 durationNs,
                               GeneratedSaveHandler saveHandler, QWidget *parent)
    : ElaDialog(parent), m_module(module), m_semantics(semantics), m_save(std::move(saveHandler))
{
    setObjectName(QStringLiteral("stimulusDialog"));
    setWindowTitle(QStringLiteral("Draw stimulus — %1").arg(module.name));
    setIsDefaultClosed(true);
    setIsStayTop(false);
    setAppBarHeight(36);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    const auto available = screen()->availableGeometry().size();
    resize(qMin(1240, available.width() - 40), qMin(820, available.height() - 60));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    auto *heading = new QHBoxLayout;
    heading->addWidget(
        Ui::label(QStringLiteral("Draw inputs · %1").arg(module.name), this, Ui::Role::PanelTitle));
    heading->addStretch();
    m_checks = Ui::button(QStringLiteral("Checks: %1").arg(scoreboardLabel(saved.value(QStringLiteral("scoreboard")).toObject())), this);
    m_checks->setObjectName(QStringLiteral("stimulusChecks"));
    heading->addWidget(m_checks);
    connect(m_checks, &QPushButton::clicked, this, &StimulusDialog::editChecks);
    auto *timing = Ui::disclosure(QStringLiteral("Timing"), this);
    timing->setObjectName(QStringLiteral("stimulusTiming"));
    timing->setChecked(false);
    heading->addWidget(timing);
    layout->addLayout(heading);
    auto *settings = new QWidget(this);
    auto *rows = new QVBoxLayout(settings);
    rows->setContentsMargins(0, 0, 0, 0);
    auto *clockRow = new QHBoxLayout;
    auto *resetRow = new QHBoxLayout;
    const auto options = saved.isEmpty() ? suggestedTbOptions(module) : stimulusOptions(saved);
    m_clock = new ElaComboBox(this);
    m_reset = new ElaComboBox(this);
    m_clock->setObjectName(QStringLiteral("stimulusClock"));
    m_reset->setObjectName(QStringLiteral("stimulusReset"));
    for (auto *box : {m_clock, m_reset})
    {
        box->addItem(QStringLiteral("None"), QString());
        for (const auto &p : module.ports)
            if (isTimingInput(p))
                box->addItem(p.name, p.name);
    }
    m_clock->setCurrentIndex(qMax(0, m_clock->findData(options.clock)));
    m_reset->setCurrentIndex(qMax(0, m_reset->findData(options.reset)));
    m_period = Ui::spinBox(this);
    m_period->setObjectName(QStringLiteral("stimulusPeriod"));
    m_period->setRange(2, 1000000);
    m_period->setValue(options.clockPeriodNs);
    m_period->setSuffix(QStringLiteral(" ns"));
    m_cycles = Ui::spinBox(this);
    m_cycles->setRange(1, 1000);
    m_cycles->setValue(options.resetCycles);
    m_cycles->setSuffix(QStringLiteral(" cycles"));
    m_polarity = new ElaComboBox(this);
    m_polarity->addItems({QStringLiteral("Active low"), QStringLiteral("Active high")});
    m_polarity->setCurrentIndex(options.resetActiveLow ? 0 : 1);
    m_duration = Ui::spinBox(this);
    m_duration->setObjectName(QStringLiteral("stimulusDuration"));
    m_duration->setRange(1, 3600000);
    m_units = new ElaComboBox(this);
    m_units->addItem(QStringLiteral("ns"), 1);
    m_units->addItem(QStringLiteral("us"), 1000);
    m_units->addItem(QStringLiteral("ms"), 1000000);
    m_units->setCurrentIndex(durationNs % 1000000 == 0 ? 2 : durationNs % 1000 == 0 ? 1 : 0);
    m_duration->setValue(int(durationNs / m_units->currentData().toLongLong()));
    clockRow->addWidget(Ui::label(QStringLiteral("Clock"), this));
    clockRow->addWidget(m_clock, 1);
    clockRow->addWidget(m_period);
    clockRow->addSpacing(16);
    clockRow->addWidget(Ui::label(QStringLiteral("Duration"), this));
    clockRow->addWidget(m_duration);
    clockRow->addWidget(m_units);
    resetRow->addWidget(Ui::label(QStringLiteral("Reset"), this));
    resetRow->addWidget(m_reset, 1);
    resetRow->addWidget(m_polarity);
    resetRow->addWidget(m_cycles);
    auto *apply = Ui::button(QStringLiteral("Apply timing"), this);
    apply->setObjectName(QStringLiteral("applyStimulusTiming"));
    resetRow->addWidget(apply);
    rows->addLayout(clockRow);
    rows->addLayout(resetRow);
    auto *hint = Ui::label(
        QStringLiteral(
            "Shortening duration trims the end. Changing reset settings rebuilds only the reset waveform."),
        this, Ui::Role::Metadata);
    hint->setWordWrap(true);
    rows->addWidget(hint);
    settings->hide();
    layout->addWidget(settings);
    connect(timing, &QToolButton::toggled, settings, &QWidget::setVisible);
    auto *canvasHost = new QWidget(this);
    m_canvasLayout = new QVBoxLayout(canvasHost);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(canvasHost, 1);
    m_error = Ui::label(QString(), this, Ui::Role::Metadata);
    m_error->setObjectName(QStringLiteral("stimulusError"));
    m_error->setProperty("error", true);
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    auto *actions = new QHBoxLayout;
    auto *resetDrawing = Ui::button(QStringLiteral("Reset drawing"), this);
    resetDrawing->setObjectName(QStringLiteral("resetStimulus"));
    actions->addWidget(resetDrawing);
    actions->addStretch();
    auto *cancel = Ui::button(QStringLiteral("Cancel"), this);
    auto *saveButton = Ui::button(QStringLiteral("Save"), this);
    auto *run = Ui::button(QStringLiteral("Save and run"), this, true);
    saveButton->setObjectName(QStringLiteral("saveStimulus"));
    run->setObjectName(QStringLiteral("runStimulus"));
    actions->addWidget(cancel);
    actions->addWidget(saveButton);
    actions->addWidget(run);
    m_saveLockedControls = {canvasHost, settings, resetDrawing, saveButton, run, m_checks, timing};
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, [this] { save(false); });
    connect(run, &QPushButton::clicked, this, [this] { save(true); });
    connect(apply, &QPushButton::clicked, this, [this] { applyTiming(false); });
    connect(resetDrawing, &QPushButton::clicked, this, [this] { applyTiming(true); });
    for (auto *spin : {m_duration, m_period, m_cycles})
        connect(spin, &QSpinBox::valueChanged, this, [this] { m_pendingTiming = true; });
    for (auto *box : {m_clock, m_reset, m_polarity, m_units})
        connect(box, &QComboBox::currentIndexChanged, this, [this] { m_pendingTiming = true; });
    Ui::normalizeControls(this);
    if (saved.isEmpty())
        applyTiming(true);
    else
    {
        if (replaceEditor(saved))
        {
            QString error;
            stimulusTestbench(module, m_semantics, saved, &error);
            showError(error);
        }
    }
    Ui::constrainDialog(this);
}

StimulusDialog::~StimulusDialog()
{
    delete m_editor;
}
void StimulusDialog::showError(const QString &error)
{
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
}
QJsonObject StimulusDialog::drawing(QString *error) const
{
    error->clear();
    if (!m_editor || !api().snapshot)
    {
        *error = api().error.isEmpty() ? QStringLiteral("No input drawing is available.") : api().error;
        return {};
    }
    std::array<char, 2048> message{};
    size_t size = 0;
    if (api().snapshot(m_editor, nullptr, 0, &size, message.data(), message.size()) || !size ||
        size > 8 * 1024 * 1024)
    {
        *error = QString::fromUtf8(message.data());
        return {};
    }
    QByteArray data(qsizetype(size), '\0');
    if (api().snapshot(m_editor, data.data(), size, &size, message.data(), message.size()))
    {
        *error = QString::fromUtf8(message.data());
        return {};
    }
    auto result = m_drawing;
    const auto wave = QJsonDocument::fromJson(data.chopped(1)).object();
    result.insert(QStringLiteral("wave"), wave);
    if (wave.value(QStringLiteral("simdockTiming")).isObject())
        result.insert(QStringLiteral("timing"), wave.value(QStringLiteral("simdockTiming")));
    return result;
}
bool StimulusDialog::replaceEditor(const QJsonObject &drawing)
{
    if (!api().create)
    {
        showError(api().error);
        return false;
    }
    auto wave = drawing.value(QStringLiteral("wave")).toObject();
    wave.insert(QStringLiteral("simdockTiming"), drawing.value(QStringLiteral("timing")));
    const auto payload = QJsonDocument(wave).toJson(QJsonDocument::Compact);
    std::array<char, 2048> message{};
    QWidget *editor = nullptr;
    if (api().create(payload.constData(), size_t(payload.size()), QT_VERSION_STR, this, &editor,
                     message.data(), message.size()))
    {
        showError(QString::fromUtf8(message.data()));
        return false;
    }
    delete m_editor;
    // The v1 editor initializes its private theme once from the application
    // palette. Give its transparent toolbar/status area the matching backdrop;
    // the surrounding SimDock dialog can have an independent theme.
    static const QColor editorBackground = qApp->palette().color(QPalette::Window);
    auto editorPalette = editor->palette();
    editorPalette.setColor(QPalette::Window, editorBackground);
    editor->setPalette(editorPalette);
    editor->setAutoFillBackground(true);
    m_editor = editor;
    m_editor->installEventFilter(this);
    m_drawing = drawing;
    m_canvasLayout->addWidget(editor);
    m_pendingTiming = false;
    showError({});
    return true;
}
bool StimulusDialog::applyTiming(bool reset)
{
    QString error;
    auto previous = reset || !m_editor ? QJsonObject() : drawing(&error);
    if (!error.isEmpty())
    {
        showError(error);
        return false;
    }
    auto options = suggestedTbOptions(m_module);
    options.clock = m_clock->currentData().toString();
    options.reset = m_reset->currentData().toString();
    options.clockPeriodNs = m_period->value();
    options.resetCycles = m_cycles->value();
    options.resetActiveLow = m_polarity->currentIndex() == 0;
    const auto duration = qint64(m_duration->value()) * m_units->currentData().toLongLong();
    auto next = retimeStimulus(m_module, m_semantics, previous, options, duration, &error);
    if (reset && m_drawing.contains(QStringLiteral("scoreboard")))
        next[QStringLiteral("scoreboard")] = m_drawing.value(QStringLiteral("scoreboard"));
    if (!error.isEmpty())
    {
        showError(error);
        return false;
    }
    if (reset || !m_editor) return replaceEditor(next);
    if (!api().update) {
        showError(QStringLiteral("Update the waveform component to change timing without losing edit history."));
        return false;
    }
    const auto payload = QJsonDocument(next.value(QStringLiteral("wave")).toObject()).toJson(QJsonDocument::Compact);
    std::array<char, 2048> message{};
    if (api().update(m_editor, payload.constData(), size_t(payload.size()), message.data(), message.size())) {
        showError(QString::fromUtf8(message.data())); return false;
    }
    m_drawing = next;
    m_pendingTiming = false;
    showError({});
    return true;
}
bool StimulusDialog::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_editor && event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "wavewidgets.stateRevision") {
        QString error;
        const auto current = drawing(&error);
        const auto duration = [](const QJsonObject &drawing) {
            return drawing.value(QStringLiteral("wave")).toObject().value(QStringLiteral("scenarios"))
                .toArray().first().toObject().value(QStringLiteral("durationTick"));
        };
        if (error.isEmpty() && (current.value(QStringLiteral("timing")) != m_drawing.value(QStringLiteral("timing"))
            || duration(current) != duration(m_drawing))) {
            syncTiming(current);
            m_drawing = current;
        }
    }
    return ElaDialog::eventFilter(object, event);
}
void StimulusDialog::syncTiming(const QJsonObject &current)
{
    const auto options = stimulusOptions(current);
    const QSignalBlocker a(m_clock), b(m_reset), c(m_period), d(m_polarity), e(m_cycles), f(m_duration), g(m_units);
    m_clock->setCurrentIndex(qMax(0, m_clock->findData(options.clock)));
    m_reset->setCurrentIndex(qMax(0, m_reset->findData(options.reset)));
    m_period->setValue(options.clockPeriodNs);
    m_polarity->setCurrentIndex(options.resetActiveLow ? 0 : 1);
    m_cycles->setValue(options.resetCycles);
    const auto scenarios = current.value(QStringLiteral("wave")).toObject().value(QStringLiteral("scenarios")).toArray();
    if (scenarios.size() == 1) {
        const auto duration = (scenarios.first().toObject().value(QStringLiteral("durationTick")).toString().toLongLong() + 999) / 1000;
        m_units->setCurrentIndex(duration % 1000000 == 0 ? 2 : duration % 1000 == 0 ? 1 : 0);
        m_duration->setValue(int(duration / m_units->currentData().toLongLong()));
    }
    m_pendingTiming = false;
}
void StimulusDialog::editChecks()
{
    if (m_pendingTiming && !applyTiming(false)) return;
    QString error;
    const auto &ports = m_semantics.ports;
    error = m_semantics.error.isEmpty() ? m_semantics.checksError : m_semantics.error;
    if (!error.isEmpty()) { showError(error); return; }
    QPointer<StimulusDialog> owner(this);
    QPointer<ScoreboardDialog> dialog = new ScoreboardDialog(ports, stimulusOptions(m_drawing), m_drawing.value(QStringLiteral("scoreboard")).toObject(), this);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    const auto plan = dialog->plan();
    delete dialog;
    if (result != QDialog::Accepted) return;
    m_drawing[QStringLiteral("scoreboard")] = plan;
    m_checks->setText(QStringLiteral("Checks: %1").arg(scoreboardLabel(plan)));
    showError({});
}
void StimulusDialog::save(bool run)
{
    if (m_saving) return;
    if (m_pendingTiming && !applyTiming(false))
        return;
    QString error;
    const auto result = drawing(&error);
    if (!error.isEmpty()) { showError(error); return; }
    if (m_prepareSave) {
        m_saving = true;
        for (auto *control : m_saveLockedControls) control->setEnabled(false);
        QPointer<StimulusDialog> owner(this);
        m_prepareSave(result, [owner, result, run](const QString &content, const QString &issue) {
            if (!owner) return;
            for (auto *control : owner->m_saveLockedControls) control->setEnabled(true);
            owner->m_saving = false;
            owner->finishSave(result, content, issue, run);
        });
        return;
    }
    const auto content = error.isEmpty() ? stimulusTestbench(m_module, m_semantics, result, &error) : QString();
    finishSave(result, content, error, run);
}
void StimulusDialog::finishSave(const QJsonObject &result, const QString &content, QString error, bool run)
{
    if (!error.isEmpty() || (m_save && !m_save(result, content, &error)))
    {
        showError(error);
        return;
    }
    m_runRequested = run;
    accept();
}
} // namespace simdock
