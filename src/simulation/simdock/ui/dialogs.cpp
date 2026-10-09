#include "dialogs.h"
#include "../core/questasession.h"
#include "../core/testbench.h"
#include "uistyle.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace simdock {
static QPixmap palettePreview(const WavePalette& palette, qreal scale)
{
    QPixmap image(QSize(480, 110) * scale);
    image.setDevicePixelRatio(scale);
    image.fill(QColor(palette.background));
    QPainter painter(&image);
    painter.setFont(Ui::codeFont());
    const QStringList labels{QStringLiteral("Clock"), QStringLiteral("Reset"), QStringLiteral("Data"), QStringLiteral("Ready"), QStringLiteral("State"), QStringLiteral("X"), QStringLiteral("Z")};
    const QStringList colors{palette.clock, palette.reset, palette.data, palette.handshake, palette.state, palette.unknown, palette.highZ};
    for (int i = 0; i < labels.size(); ++i) {
        const int x = 12 + i * 67;
        painter.setPen(QColor(palette.text));
        painter.drawText(QRect(x, 12, 60, 22), Qt::AlignCenter, labels[i]);
        painter.setPen(QPen(QColor(colors[i]), 2));
        if (i < 2 || i == 3) {
            painter.drawPolyline(QPolygonF{QPointF(x, 64), QPointF(x + 15, 64), QPointF(x + 15, 46), QPointF(x + 43, 46), QPointF(x + 43, 64), QPointF(x + 60, 64)});
        } else if (i < 5) {
            painter.drawPolygon(QPolygonF{QPointF(x, 55), QPointF(x + 6, 46), QPointF(x + 54, 46), QPointF(x + 60, 55), QPointF(x + 54, 64), QPointF(x + 6, 64)});
        } else {
            painter.drawLine(x, 55, x + 60, 55);
        }
        painter.drawText(QRect(x, 77, 60, 20), Qt::AlignCenter, colors[i]);
    }
    return image;
}

SettingsDialog::SettingsDialog(const QString& executable, QWidget* parent, bool simulatorLocked, const QString& waveThemeId, bool hostTheme) : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));
    setObjectName(QStringLiteral("settingsDialog"));
    setIsDefaultClosed(true);
    setIsStayTop(false);
    setAppBarHeight(36);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    resize(680, 610);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    layout->addWidget(Ui::label(QStringLiteral("Appearance"), this, Ui::Role::PanelTitle));
    auto* appearance = new QFormLayout;
    appearance->setHorizontalSpacing(16);
    appearance->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_theme = new ElaComboBox(this);
    m_theme->setObjectName(QStringLiteral("themeSelector"));
    m_theme->addItems({QStringLiteral("Light"), QStringLiteral("Dark")});
    m_theme->setCurrentIndex(eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0);
    if (hostTheme) {
        m_theme->setEnabled(false);
        m_theme->setToolTip(QStringLiteral("Theme follows the host application."));
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] {
            m_theme->setCurrentIndex(eTheme->getThemeMode() == ElaThemeType::Dark ? 1 : 0);
        });
    }
    Ui::formRow(appearance, QStringLiteral("Theme"), m_theme);
    m_waveTheme = new ElaComboBox(this);
    m_waveTheme->setObjectName(QStringLiteral("waveThemeSelector"));
    for (const auto& palette : wavePalettes()) m_waveTheme->addItem(palette.name, palette.id);
    const auto savedWaveTheme = waveThemeFromId(waveThemeId.isEmpty()
        ? QSettings().value(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString() : waveThemeId);
    m_waveTheme->setCurrentIndex(m_waveTheme->findData(wavePalette(savedWaveTheme).id));
    Ui::formRow(appearance, QStringLiteral("Questa waveform colors"), m_waveTheme);
    layout->addLayout(appearance);
    auto* preview = new QLabel(this);
    preview->setObjectName(QStringLiteral("waveThemePreview"));
    preview->setAlignment(Qt::AlignCenter);
    preview->setFixedHeight(110);
    layout->addWidget(preview);
    const auto updatePalette = [this, preview] {
        preview->setPixmap(palettePreview(wavePalette(waveTheme()), devicePixelRatioF()));
        preview->setAccessibleName(QStringLiteral("%1 waveform color preview").arg(wavePalette(waveTheme()).name));
    };
    connect(m_waveTheme, &QComboBox::currentIndexChanged, this, updatePalette);
    updatePalette();
    auto* waveHint = Ui::label(QStringLiteral("Applied on the next simulation run. Common clock, reset, handshake and state names are colored automatically."), this, Ui::Role::Metadata);
    waveHint->setWordWrap(true);
    layout->addWidget(waveHint);
    layout->addSpacing(8);
    layout->addWidget(Ui::label(QStringLiteral("Simulator"), this, Ui::Role::PanelTitle));
    layout->addWidget(Ui::label(QStringLiteral("Select vsim.exe from your Questa installation. You can change it later."), this, Ui::Role::Metadata));
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->setHorizontalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* type = new ElaComboBox(this);
    type->addItem(QStringLiteral("QuestaSim"));
    type->setObjectName(QStringLiteral("simulatorType"));
    Ui::formRow(form, QStringLiteral("Type"), type);
    auto* row = new QHBoxLayout;
    m_path = new ElaLineEdit(this);
    m_path->setObjectName(QStringLiteral("simulatorPath"));
    m_path->setPlaceholderText(QStringLiteral("Select vsim.exe from your Questa installation"));
    m_path->setText(executable.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("vsim")) : executable);
    auto* browse = Ui::button(QStringLiteral("Browse"), this);
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    Ui::formRow(form, QStringLiteral("Executable"), row);
    layout->addLayout(form);
    type->setEnabled(!simulatorLocked);
    m_path->setEnabled(!simulatorLocked);
    browse->setEnabled(!simulatorLocked);
    if (simulatorLocked) {
        auto* locked = Ui::label(QStringLiteral("Close the active Questa session to change the simulator."), this, Ui::Role::Metadata);
        locked->setWordWrap(true);
        layout->addWidget(locked);
    }
    auto* error = Ui::label(QString(), this, Ui::Role::Metadata);
    error->setProperty("error", true);
    error->hide();
    error->setWordWrap(true);
    layout->addWidget(error);
    layout->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = Ui::button(QStringLiteral("Cancel"), this);
    auto* save = Ui::button(QStringLiteral("Save"), this, true);
    save->setObjectName(QStringLiteral("saveSettings"));
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    Ui::normalizeControls(this);
    Ui::constrainDialog(this);
    connect(browse, &QPushButton::clicked, this, [this] {
        QPointer<SettingsDialog> owner(this);
        QPointer<QFileDialog> dialog = new QFileDialog(this, QStringLiteral("Select Questa executable"), m_path->text(),
            QStringLiteral("Questa (vsim.exe);;All files (*)"));
        dialog->setFileMode(QFileDialog::ExistingFile);
        dialog->setOption(QFileDialog::DontUseNativeDialog);
        const auto result = dialog->exec();
        if (!owner || !dialog) return;
        const auto paths = dialog->selectedFiles();
        delete dialog;
        if (result == QDialog::Accepted && !paths.isEmpty()) m_path->setText(paths.first());
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this, error, simulatorLocked] {
        const auto issue = simulatorLocked ? QString() : simulatorError(this->executable());
        error->setText(issue);
        error->setVisible(!issue.isEmpty());
        if (issue.isEmpty()) accept();
    });
}
QString SettingsDialog::executable() const { return m_path->text().trimmed(); }
bool SettingsDialog::darkTheme() const { return m_theme->currentIndex() == 1; }
WaveTheme SettingsDialog::waveTheme() const { return waveThemeFromId(m_waveTheme->currentData().toString()); }

TestbenchDialog::TestbenchDialog(const Module& module, qint64 durationNs, QWidget* parent)
    : ElaDialog(parent), m_module(module), m_duration(durationNs)
{
    setWindowTitle(QStringLiteral("Create Demo TB"));
    setObjectName(QStringLiteral("testbenchDialog"));
    setIsDefaultClosed(true);
    setIsStayTop(false);
    setAppBarHeight(36);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    resize(780, 660);
    setMinimumSize(660, 540);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->addWidget(Ui::label(QStringLiteral("Create a Demo TB for %1").arg(module.name), this, Ui::Role::PageTitle));
    auto* note = Ui::label(QStringLiteral("Select clock and reset signals. Other inputs start at zero; edit the generated TB to add stimulus."), this, Ui::Role::Metadata);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->setSpacing(12);
    const auto suggestion = suggestedTbOptions(module);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->setHorizontalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_name = new ElaLineEdit(this);
    m_name->setObjectName(QStringLiteral("tbNameInput"));
    m_name->setText(suggestion.name);
    Ui::formRow(form, QStringLiteral("TB top-level name"), m_name);
    m_clock = new ElaComboBox(this);
    m_reset = new ElaComboBox(this);
    for (auto* box : {m_clock, m_reset}) {
        box->addItem(QStringLiteral("None"), QString());
        for (const auto& p : module.ports)
            if (isTimingInput(p)) box->addItem(p.name, p.name);
    }
    m_clock->setCurrentIndex(qMax(0, m_clock->findData(suggestion.clock)));
    m_reset->setCurrentIndex(qMax(0, m_reset->findData(suggestion.reset)));
    m_period = Ui::spinBox(this);
    m_period->setRange(2, 1000000);
    m_period->setValue(10);
    m_period->setSuffix(QStringLiteral(" ns"));
    m_polarity = new ElaComboBox(this);
    m_polarity->addItems({QStringLiteral("Active low"), QStringLiteral("Active high")});
    m_polarity->setCurrentIndex(suggestion.resetActiveLow ? 0 : 1);
    m_cycles = Ui::spinBox(this);
    m_cycles->setRange(1, 1000);
    m_cycles->setValue(5);
    auto* clocks = new QHBoxLayout;
    clocks->addWidget(m_clock, 1);
    clocks->addWidget(m_period, 1);
    Ui::formRow(form, QStringLiteral("Clock / period"), clocks);
    auto* resets = new QHBoxLayout;
    resets->addWidget(m_reset, 1);
    resets->addWidget(m_polarity, 1);
    resets->addWidget(m_cycles, 1);
    Ui::formRow(form, QStringLiteral("Reset / polarity / cycles"), resets);
    layout->addLayout(form);
    m_preview = Ui::textView(this);
    m_preview->setObjectName(QStringLiteral("tbPreview"));
    m_preview->setReadOnly(true);
    m_preview->setProperty("codeSurface", true);
    m_preview->setNativeTextBehavior(true);
    m_preview->setFont(Ui::codeFont());
    layout->addWidget(m_preview, 1);
    m_error = Ui::label(QString(), this, Ui::Role::Metadata);
    m_error->setProperty("error", true);
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = Ui::button(QStringLiteral("Cancel"), this);
    auto* create = Ui::button(QStringLiteral("Create file"), this, true);
    create->setObjectName(QStringLiteral("confirmCreateTb"));
    create->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(create);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create, &QPushButton::clicked, this, [this] {
        updatePreview();
        if (m_error->text().isEmpty()) accept();
    });
    connect(m_name, &QLineEdit::textChanged, this, &TestbenchDialog::updatePreview);
    for (auto* box : {m_clock, m_reset, m_polarity})
        connect(box, &QComboBox::currentIndexChanged, this, &TestbenchDialog::updatePreview);
    for (auto* spin : {m_period, m_cycles})
        connect(spin, &QSpinBox::valueChanged, this, &TestbenchDialog::updatePreview);
    Ui::normalizeControls(this);
    updatePreview();
    Ui::constrainDialog(this);
}
TbOptions TestbenchDialog::options() const
{
    return {m_name->text().trimmed(), m_clock->currentData().toString(), m_reset->currentData().toString(),
        m_polarity->currentIndex() == 0, m_period->value(), m_cycles->value()};
}
QString TestbenchDialog::content() const { return m_preview->toPlainText(); }
void TestbenchDialog::updatePreview()
{
    QString error;
    m_preview->setPlainText(generateTestbench(m_module, options(), m_duration, &error));
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
}

QString askProjectName(QWidget* parent)
{
    QPointer<ElaDialog> dialog = new ElaDialog(parent);
    dialog->setWindowTitle(QStringLiteral("New simulation project"));
    dialog->setObjectName(QStringLiteral("newProjectDialog"));
    dialog->setIsDefaultClosed(true);
    dialog->setIsStayTop(false);
    dialog->setAppBarHeight(36);
    dialog->setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    dialog->resize(440, 250);
    auto* layout = new QVBoxLayout(dialog.data());
    layout->setContentsMargins(24, 20, 24, 20);
    layout->addWidget(Ui::label(QStringLiteral("Project name"), dialog.data(), Ui::Role::PageTitle));
    layout->addWidget(Ui::label(QStringLiteral("Source selections are saved with this project."), dialog.data(), Ui::Role::Metadata));
    auto* name = new ElaLineEdit(dialog.data());
    name->setObjectName(QStringLiteral("projectNameInput"));
    name->setPlaceholderText(QStringLiteral("e.g. counter_demo"));
    name->setMaxLength(120);
    layout->addWidget(name);
    auto* create = Ui::button(QStringLiteral("Create project"), dialog.data(), true);
    create->setObjectName(QStringLiteral("confirmCreateProject"));
    layout->addWidget(create);
    auto accept = [&] { if (!name->text().trimmed().isEmpty()) dialog->accept(); };
    QObject::connect(create, &QPushButton::clicked, dialog.data(), accept);
    QObject::connect(name, &QLineEdit::returnPressed, dialog.data(), accept);
    Ui::normalizeControls(dialog.data());
    Ui::constrainDialog(dialog.data());
    name->setFocus();
    const auto result = dialog->exec();
    if (!dialog) return {};
    const auto value = result == QDialog::Accepted ? name->text().trimmed() : QString();
    delete dialog;
    return value;
}
}
