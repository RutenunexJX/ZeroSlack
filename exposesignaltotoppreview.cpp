#include "exposesignaltotoppreview.h"

#include "actionregistry.h"
#include "editorhoverpopup.h"

#include <QEventLoop>
#include <QLineEdit>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace {
QString summaryText(const ExposeSignalToTopReport& report)
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

QString previewText(const ExposeSignalToTopReport& report)
{
    return QStringLiteral("%1\n\nDiff:\n%2")
        .arg(summaryText(report),
             report.renderedDiff.isEmpty()
                 ? QStringLiteral("No diff is available.")
                 : report.renderedDiff);
}

PeekContentModel contentForReport(
    const ExposeSignalToTopReport& report,
    bool canReplan,
    ExposeSignalToTopPreview::Mode mode)
{
    PeekContentModel content;
    content.kind = PeekContentKind::ActionPlanPreview;
    const ActionDescriptor* descriptor =
        findActionById(
            QStringLiteral("refactor.exposeSignalToTop"));
    content.title = descriptor
        ? descriptor->canonicalName
        : QStringLiteral("Expose Signal to Top");
    content.rows.append(
        {report.ready()
             ? mode
                       == ExposeSignalToTopPreview::Mode::PreviewOnly
                   ? QStringLiteral(
                         "Plan-only preview. Closing performs no mutation.")
                   : QStringLiteral(
                         "Ready. Apply performs one atomic multi-file transaction.")
             : QStringLiteral("Blocked: %1").arg(report.message),
         report.ready()
             ? PeekContentRowRole::Body
             : PeekContentRowRole::Warning,
         true});

    content.editor.enabled = true;
    content.editor.readOnly = !canReplan;
    content.editor.text = report.exportedPortName;
    content.editor.placeholderText =
        QStringLiteral("Final output port");
    content.editor.objectName =
        QStringLiteral("exposeSignalPortName");
    content.editor.minimumWidth = 360;

    content.readOnlyText.enabled = true;
    content.readOnlyText.text = previewText(report);
    content.readOnlyText.objectName =
        QStringLiteral("exposeSignalPlanPreview");
    content.readOnlyText.minimumSize = QSize(520, 300);
    content.maximumSize = QSize(860, 620);

    if (mode
        == ExposeSignalToTopPreview::Mode::PreviewOnly) {
        PeekContentAction close;
        close.id = QStringLiteral("close");
        close.label = QStringLiteral("Close");
        close.role = PeekContentActionRole::Primary;
        close.defaultAction = true;
        content.actions.append(close);
    } else {
        PeekContentAction apply;
        apply.id = QStringLiteral("apply");
        apply.label = QStringLiteral("Apply");
        apply.role = PeekContentActionRole::Primary;
        apply.enabled = report.ready();
        content.actions.append(apply);

        PeekContentAction cancel;
        cancel.id = QStringLiteral("cancel");
        cancel.label = QStringLiteral("Cancel");
        cancel.defaultAction = true;
        content.actions.append(cancel);
    }
    return content;
}
}

ExposeSignalToTopPreview::ExposeSignalToTopPreview(
    const ExposeSignalToTopReport& initialReport,
    ReplanFunction replan,
    QWidget* host,
    Mode mode)
    : currentReport(initialReport)
    , replanFunction(std::move(replan))
    , hostWidget(host)
    , previewMode(mode)
{
}

ExposeSignalToTopPreview::Result ExposeSignalToTopPreview::exec()
{
    currentResult = Result::Rejected;
    if (!hostWidget)
        return currentResult;

    QEventLoop eventLoop;
    QPointer<EditorHoverPopup> peek =
        new EditorHoverPopup(hostWidget);
    peek->setProperty("exposeSignalToTopPreview", true);
    const QRect anchor(
        hostWidget->mapToGlobal(hostWidget->rect().center()),
        QSize(1, 1));
    std::uint64_t replanRevision = 0;

    std::function<void(bool, int)> render;
    render = [&](bool restoreEditorFocus,
                 int cursorPosition) {
        if (!peek)
            return;
        peek->showContent(
            contentForReport(
                currentReport,
                static_cast<bool>(replanFunction),
                previewMode),
            anchor,
            hostWidget ? hostWidget->font() : QFont());
        QLineEdit* editor = peek->editableLineEdit();
        if (!editor || !replanFunction)
            return;
        if (restoreEditorFocus) {
            editor->setFocus(Qt::OtherFocusReason);
            editor->setCursorPosition(
                std::clamp(cursorPosition,
                           0,
                           static_cast<int>(
                               editor->text().size())));
        }
        QObject::connect(
            editor,
            &QLineEdit::textChanged,
            peek.data(),
            [&, editor](const QString& text) {
                const int cursor = editor->cursorPosition();
                const std::uint64_t revision =
                    ++replanRevision;
                currentReport = replanFunction(text);
                QTimer::singleShot(
                    0,
                    peek.data(),
                    [&, revision, cursor]() {
                        if (!peek
                            || revision != replanRevision) {
                            return;
                        }
                        render(true, cursor);
                    });
            });
    };

    QObject::connect(
        peek.data(),
        &EditorHoverPopup::actionTriggered,
        &eventLoop,
        [&](const QString& actionId) {
            if (actionId == QStringLiteral("apply")) {
                if (previewMode != Mode::ReviewAndApply)
                    return;
                if (!currentReport.ready())
                    return;
                currentResult = Result::Accepted;
            } else if (actionId
                           == QStringLiteral("close")
                       && previewMode
                              == Mode::PreviewOnly) {
                currentResult = Result::Accepted;
            } else if (actionId
                       != QStringLiteral("cancel")) {
                return;
            }
            if (peek)
                peek->closePopup();
        });
    QObject::connect(
        peek.data(),
        &EditorHoverPopup::closed,
        &eventLoop,
        &QEventLoop::quit);
    QObject::connect(
        hostWidget.data(),
        &QObject::destroyed,
        &eventLoop,
        &QEventLoop::quit);

    render(static_cast<bool>(replanFunction),
           currentReport.exportedPortName.size());
    if (peek && peek->isVisible())
        eventLoop.exec();

    if (peek)
        delete peek.data();
    return currentResult;
}

ExposeSignalToTopPreview::Result
ExposeSignalToTopPreview::result() const
{
    return currentResult;
}

const ExposeSignalToTopReport&
ExposeSignalToTopPreview::reportForApply() const
{
    return currentReport;
}

QString ExposeSignalToTopPreview::exportedPortName() const
{
    return currentReport.exportedPortName;
}
