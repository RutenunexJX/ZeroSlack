#include "dialogs.h"
#include "../core/questasession.h"
#include "../core/workspacesimulationcontroller.h"
#include "uistyle.h"
#include "uiwindowchrome.h"
#include "uicontrols.h"
#include <ElaLineEdit.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QComboBox>
#include <QToolButton>
#include <QPainter>
#include <QPainterPath>

namespace simdock {
namespace {
class WavePreview final : public QWidget {
public:
    explicit WavePreview(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("waveThemePreview"));
        setAccessibleName(QStringLiteral("Questa waveform color preview"));
        setMinimumSize(240, 116);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    WaveTheme theme = WaveTheme::QuestaDefault;
    QSize sizeHint() const override { return QSize(360, 116); }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& colors = wavePalette(theme);
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(colors.background));
        painter.drawRoundedRect(rect().adjusted(0,0,-1,-1),6,6);
        painter.setFont(Ui::codeFont());
        const int left=82, right=width()-12;
        const qreal step=qMax(1, right-left)/8.0;
        painter.setPen(QColor(colors.grid));
        for(int i=0;i<=8;++i) painter.drawLine(QPointF(left+i*step,8),QPointF(left+i*step,height()-8));
        const QStringList names{QStringLiteral("clk"),QStringLiteral("rst_n"),QStringLiteral("data")};
        const QStringList signalColors{colors.clock,colors.reset,colors.data};
        for(int row=0;row<3;++row) {
            const qreal y=25+row*33;
            painter.setPen(QColor(colors.text));
            painter.drawText(QRectF(12,y-10,62,22),Qt::AlignVCenter,names.at(row));
            painter.setPen(QPen(QColor(signalColors.at(row)),1.5));
            QPainterPath wave;
            wave.moveTo(left,y+6);
            if(row==0) {
                for(int i=0;i<8;++i) {
                    const qreal level=i%2 ? y+6 : y-6;
                    wave.lineTo(left+i*step,level); wave.lineTo(left+(i+1)*step,level);
                }
            } else if(row==1) {
                wave.lineTo(left+2*step,y+6); wave.lineTo(left+2*step,y-6); wave.lineTo(right,y-6);
            } else {
                wave.lineTo(left+3*step-4,y+6); wave.lineTo(left+3*step+4,y-6); wave.lineTo(right,y-6);
                wave.moveTo(left,y-6); wave.lineTo(left+3*step-4,y-6); wave.lineTo(left+3*step+4,y+6); wave.lineTo(right,y+6);
                painter.drawText(QRectF(left+3*step+10,y-10,80,20),Qt::AlignVCenter,QStringLiteral("0x2A"));
            }
            painter.drawPath(wave);
        }
    }
};
}
SettingsDialog::SettingsDialog(const QString& executable, QWidget* parent, bool simulatorLocked,
    WorkspaceSimulationController* controller, std::function<void()> configureWorkspace, WaveTheme selectedTheme) : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("Simulation settings"));
    UiWindowChrome::ensureWindowBoundary(this);
    setObjectName(QStringLiteral("settingsDialog"));
    setIsDefaultClosed(true);
    setIsStayTop(false);
    setAppBarHeight(36);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    resize(560, 430);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->setHorizontalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* row = new QHBoxLayout;
    m_path = new ElaLineEdit(this);
    m_path->setObjectName(QStringLiteral("simulatorPath"));
    m_path->setPlaceholderText(QStringLiteral("Select vsim.exe from your Questa installation"));
    m_path->setText(executable.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("vsim")) : executable);
    auto* browse = Ui::button(QStringLiteral("Browse"), this);
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    Ui::formRow(form, QStringLiteral("Questa executable"), row);
    auto* colors = new QHBoxLayout;
    m_waveTheme = UiControls::comboBox(this);
    m_waveTheme->setObjectName(QStringLiteral("waveThemeSelector"));
    for (const auto& palette : wavePalettes()) m_waveTheme->addItem(palette.name, palette.id);
    m_waveTheme->setCurrentIndex(m_waveTheme->findData(wavePalette(selectedTheme).id));
    auto* previewToggle = UiControls::toolButton(this);
    previewToggle->setObjectName(QStringLiteral("waveThemePreviewToggle"));
    previewToggle->setText(QStringLiteral("Preview")); previewToggle->setCheckable(true);
    colors->addWidget(m_waveTheme,1); colors->addWidget(previewToggle);
    Ui::formRow(form, QStringLiteral("Questa waveform colors"), colors);
    layout->addLayout(form);
    auto* preview = new WavePreview(this); preview->theme=waveTheme(); preview->hide();
    layout->addWidget(preview);
    connect(m_waveTheme, &QComboBox::currentIndexChanged, this, [this,preview] {
        preview->theme=waveTheme(); preview->update();
    });
    connect(previewToggle, &QToolButton::toggled, this, [this,preview](bool visible) {
        preview->setVisible(visible); adjustSize();
    });
    m_path->setEnabled(!simulatorLocked);
    browse->setEnabled(!simulatorLocked);
    if (simulatorLocked) {
        auto* locked = Ui::label(QStringLiteral("Close the active Questa session to change the simulator."), this, Ui::Role::Metadata);
        locked->setWordWrap(true);
        layout->addWidget(locked);
    }
    ElaPlainTextEdit *includes = nullptr, *mappings = nullptr;
    if (controller && !controller->workspace().isEmpty()) {
        includes = new ElaPlainTextEdit(this); includes->setNativeTextBehavior(true);
        includes->setObjectName(QStringLiteral("simulationIncludes")); includes->setMaximumHeight(72);
        includes->setPlaceholderText(QStringLiteral("Include directories, in search order; one per line"));
        includes->setPlainText(controller->includeDirectories().join('\n'));
        mappings = new ElaPlainTextEdit(this); mappings->setNativeTextBehavior(true);
        mappings->setObjectName(QStringLiteral("simulationDefinitionSources")); mappings->setMaximumHeight(72);
        mappings->setPlaceholderText(QStringLiteral("Definition = source file; one mapping per line"));
        QStringList lines;
        const auto sources = controller->definitionSources();
        for (auto it = sources.cbegin(); it != sources.cend(); ++it) lines << it.key() + QStringLiteral(" = ") + it.value();
        mappings->setPlainText(lines.join('\n'));
        includes->setEnabled(!controller->busy()); mappings->setEnabled(!controller->busy());
        layout->addWidget(Ui::label(QStringLiteral("Workspace compilation"), this, Ui::Role::Section));
        layout->addWidget(includes); layout->addWidget(mappings);
        if (configureWorkspace) {
            auto* configuration = Ui::button(QStringLiteral("Macros and language…"), this);
            configuration->setObjectName(QStringLiteral("simulationWorkspaceConfiguration"));
            configuration->setEnabled(!controller->busy());
            connect(configuration, &QPushButton::clicked, this, std::move(configureWorkspace));
            layout->addWidget(configuration);
        }
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
    connect(save, &QPushButton::clicked, this, [this, error, simulatorLocked, includes, mappings, owner = QPointer<WorkspaceSimulationController>(controller)] {
        auto issue = simulatorLocked ? QString() : simulatorError(this->executable());
        if (issue.isEmpty() && owner && includes && includes->isEnabled()) {
            QStringList directories;
            for (const auto& line : includes->toPlainText().split('\n')) if (!line.trimmed().isEmpty()) directories << line.trimmed();
            QMap<QString, QString> sources;
            for (const auto& line : mappings->toPlainText().split('\n')) {
                if (line.trimmed().isEmpty()) continue;
                const auto equal = line.indexOf('=');
                const auto name = line.left(equal).trimmed(), path = line.mid(equal + 1).trimmed();
                if (equal < 1 || name.isEmpty() || path.isEmpty() || sources.contains(name)) {
                    issue = QStringLiteral("Use one unique Definition = source file mapping per line."); break;
                }
                sources.insert(name, path);
            }
            if (issue.isEmpty()) issue = owner->setSettings(directories, sources);
        }
        error->setText(issue);
        error->setVisible(!issue.isEmpty());
        if (issue.isEmpty()) accept();
    });
}
QString SettingsDialog::executable() const { return m_path->text().trimmed(); }
WaveTheme SettingsDialog::waveTheme() const { return waveThemeFromId(m_waveTheme->currentData().toString()); }

}
