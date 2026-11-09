#include "dialogs.h"
#include "../core/questasession.h"
#include "../core/workspacesimulationcontroller.h"
#include "uistyle.h"
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

namespace simdock {
SettingsDialog::SettingsDialog(const QString& executable, QWidget* parent, bool simulatorLocked,
    WorkspaceSimulationController* controller, std::function<void()> configureWorkspace) : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("Simulation settings"));
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
    layout->addLayout(form);
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

}
