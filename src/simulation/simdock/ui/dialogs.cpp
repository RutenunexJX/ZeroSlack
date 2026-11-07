#include "dialogs.h"
#include "../core/questasession.h"
#include "uistyle.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
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

QString askProjectName(QWidget* parent, const QString& currentName)
{
    QPointer<ElaDialog> dialog = new ElaDialog(parent);
    dialog->setWindowTitle(currentName.isEmpty() ? QStringLiteral("New simulation project") : QStringLiteral("Rename simulation project"));
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
    name->setText(currentName);
    layout->addWidget(name);
    auto* create = Ui::button(currentName.isEmpty() ? QStringLiteral("Create project") : QStringLiteral("Rename project"), dialog.data(), true);
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
