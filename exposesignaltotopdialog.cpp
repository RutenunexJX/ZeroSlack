#include "exposesignaltotopdialog.h"

#include "actionregistry.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

ExposeSignalToTopDialog::ExposeSignalToTopDialog(
    const ExposeSignalToTopReport& initialReport,
    ReplanFunction replan,
    QWidget* parent)
    : QDialog(parent)
    , currentReport(initialReport)
    , replanFunction(std::move(replan))
{
    setObjectName(QStringLiteral("exposeSignalToTopDialog"));
    const ActionDescriptor* descriptor =
        findActionById(
            QStringLiteral("refactor.exposeSignalToTop"));
    setWindowTitle(
        descriptor ? descriptor->canonicalName
                   : QStringLiteral("Expose Signal to Top"));
    resize(880, 680);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    portNameEdit = new QLineEdit(this);
    portNameEdit->setObjectName(
        QStringLiteral("exposeSignalPortName"));
    portNameEdit->setText(initialReport.exportedPortName);
    form->addRow(QStringLiteral("Final output port"), portNameEdit);
    layout->addLayout(form);

    stateLabel = new QLabel(this);
    stateLabel->setObjectName(
        QStringLiteral("exposeSignalState"));
    stateLabel->setWordWrap(true);
    layout->addWidget(stateLabel);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    summaryView = new QPlainTextEdit(splitter);
    summaryView->setObjectName(
        QStringLiteral("exposeSignalSummary"));
    summaryView->setReadOnly(true);
    summaryView->setLineWrapMode(QPlainTextEdit::NoWrap);
    diffView = new QPlainTextEdit(splitter);
    diffView->setObjectName(
        QStringLiteral("exposeSignalDiff"));
    diffView->setReadOnly(true);
    diffView->setLineWrapMode(QPlainTextEdit::NoWrap);
    splitter->addWidget(summaryView);
    splitter->addWidget(diffView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        this);
    buttons->setObjectName(
        QStringLiteral("exposeSignalButtons"));
    applyButton = buttons->button(QDialogButtonBox::Apply);
    applyButton->setObjectName(
        QStringLiteral("exposeSignalApplyButton"));
    QPushButton* cancelButton =
        buttons->button(QDialogButtonBox::Cancel);
    cancelButton->setObjectName(
        QStringLiteral("exposeSignalCancelButton"));
    layout->addWidget(buttons);

    connect(applyButton, &QPushButton::clicked,
            this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);
    connect(portNameEdit, &QLineEdit::textChanged,
            this, [this](const QString& name) {
                if (!replanFunction)
                    return;
                updateReport(replanFunction(name));
            });

    updateReport(initialReport);
}

const ExposeSignalToTopReport&
ExposeSignalToTopDialog::reportForApply() const
{
    return currentReport;
}

QString ExposeSignalToTopDialog::exportedPortName() const
{
    return portNameEdit ? portNameEdit->text() : QString();
}

void ExposeSignalToTopDialog::updateReport(
    const ExposeSignalToTopReport& report)
{
    currentReport = report;
    if (stateLabel) {
        stateLabel->setText(
            report.ready()
                ? QStringLiteral(
                      "Ready. Apply performs one atomic multi-file transaction.")
                : QStringLiteral("Blocked: %1").arg(report.message));
    }
    if (summaryView)
        summaryView->setPlainText(summaryText(report));
    if (diffView) {
        diffView->setPlainText(
            report.renderedDiff.isEmpty()
                ? QStringLiteral("No diff is available.")
                : report.renderedDiff);
    }
    if (applyButton) {
        applyButton->setEnabled(report.ready());
        applyButton->setToolTip(
            report.ready() ? QString() : report.message);
    }
}

QString ExposeSignalToTopDialog::summaryText(
    const ExposeSignalToTopReport& report)
{
    QStringList lines;
    lines.append(QStringLiteral("Source instance: %1")
                     .arg(report.sourceInstancePath));
    lines.append(QStringLiteral("Active top: %1")
                     .arg(report.targetInstancePath));
    lines.append(QStringLiteral("Final port: %1")
                     .arg(report.exportedPortName));

    lines.append(QStringLiteral("Propagation path:"));
    if (report.hierarchySteps.isEmpty()) {
        lines.append(QStringLiteral(
            "  source module is the active top"));
    } else {
        for (const ExposeSignalHierarchyStepView& step
             : report.hierarchySteps) {
            lines.append(
                QStringLiteral("  %1. %2 (%3) -> %4 (%5)")
                    .arg(step.index + 1)
                    .arg(step.childInstancePath,
                         step.childModule,
                         step.parentInstancePath,
                         step.parentModule));
        }
    }

    lines.append(QStringLiteral("Affected modules: %1")
                     .arg(report.affectedModules.join(
                         QStringLiteral(", "))));
    lines.append(QStringLiteral("Affected files:"));
    for (const QString& file : report.affectedFiles)
        lines.append(QStringLiteral("  %1").arg(file));
    lines.append(QStringLiteral("Affected instances:"));
    for (const QString& instance : report.affectedInstancePaths)
        lines.append(QStringLiteral("  %1").arg(instance));

    if (!report.blockers.isEmpty()) {
        lines.append(QStringLiteral("Blockers:"));
        for (const QString& blocker : report.blockers)
            lines.append(QStringLiteral("  %1").arg(blocker));
    }
    return lines.join(QLatin1Char('\n'));
}
