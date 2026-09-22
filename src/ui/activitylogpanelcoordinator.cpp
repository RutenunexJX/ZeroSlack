#include "uicontrols.h"
#include "uitypography.h"
#include "activitylogpanelcoordinator.h"

#include "activitylogservice.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QVBoxLayout>

ActivityLogPanelCoordinator::ActivityLogPanelCoordinator(QWidget* parent)
    : QObject(nullptr), service(ActivityLogService::getInstance())
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addStretch(1);
    clearButton = UiControls::pushButton(QObject::tr("Clear"), panel);
    clearButton->setObjectName(QStringLiteral("activityLogClearButton"));
    toolbar->addWidget(clearButton);
    layout->addLayout(toolbar);

    outputText = UiControls::readOnlyText(panel);
    outputText->setObjectName(QStringLiteral("activityOutputText"));
    outputText->setReadOnly(true);
    UiTypography::apply(outputText, UiTypography::Role::Body);
    outputText->document()->setMaximumBlockCount(2000);
    outputText->installEventFilter(this);
    outputText->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(outputText);

    activityDock = new QDockWidget(QObject::tr("Activity"), parent);
    activityDock->setObjectName(QStringLiteral("activityDock"));
    activityDock->setWidget(panel);
    activityDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

    appendExistingEvents();

    QObject::connect(service,
                     &ActivityLogService::eventAppended,
                     outputText,
                     [this](const ActivityLogEvent& event) {
                         pendingSequence = qMax(pendingSequence, event.sequence);
                         if (!isVisibleToUser()) {
                             pendingLines.clear();
                             rebuildFromService = true;
                             return;
                         }
                         pendingLines.append(
                             ActivityLogService::formatEvent(event));
                         schedulePendingFlush();
                     });
    QObject::connect(service,
                     &ActivityLogService::cleared,
                     outputText,
                     [this]() {
                         pendingLines.clear();
                         pendingSequence = 0;
                         rebuildFromService = !isVisibleToUser();
                         if (outputText && !rebuildFromService)
                             outputText->clear();
                     });
    QObject::connect(activityDock,
                     &QDockWidget::visibilityChanged,
                     outputText,
                     [this](bool visible) {
                         if (!visible)
                             return;
                         if (rebuildFromService) {
                             pendingLines.clear();
                             appendExistingEvents();
                             rebuildFromService = false;
                         } else {
                             schedulePendingFlush();
                         }
                     });
    QObject::connect(clearButton,
                     &QPushButton::clicked,
                     service,
                     &ActivityLogService::clear);
}

void ActivityLogPanelCoordinator::appendExistingEvents()
{
    if (!outputText || !service)
        return;

    QStringList lines;
    const QList<ActivityLogEvent> events = service->events();
    lines.reserve(events.size());
    for (const ActivityLogEvent& event : events)
        lines.append(ActivityLogService::formatEvent(event));
    outputText->setPlainText(lines.join(QLatin1Char('\n')));
    QTextCursor spacing(outputText->document());
    spacing.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setBottomMargin(4);
    spacing.mergeBlockFormat(format);
    if (isVisibleToUser() && !events.isEmpty())
        service->markReadThrough(events.last().sequence);
}

bool ActivityLogPanelCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == outputText && event->type() == QEvent::Show) {
        QMetaObject::invokeMethod(this, [this]() {
            if (isVisibleToUser()) {
                pendingLines.clear();
                rebuildFromService = false;
                appendExistingEvents();
            }
        }, Qt::QueuedConnection);
    }
    return QObject::eventFilter(watched, event);
}

void ActivityLogPanelCoordinator::schedulePendingFlush()
{
    if (flushQueued || pendingLines.isEmpty() || !isVisibleToUser())
        return;
    flushQueued = true;
    QMetaObject::invokeMethod(
        outputText,
        [this]() {
            flushQueued = false;
            flushPendingEvents();
        },
        Qt::QueuedConnection);
}

void ActivityLogPanelCoordinator::flushPendingEvents()
{
    if (!outputText || pendingLines.isEmpty() || !isVisibleToUser())
        return;

    const QString text = pendingLines.join(QLatin1Char('\n'));
    pendingLines.clear();
    QScrollBar* scrollBar = outputText->verticalScrollBar();
    const bool followTail = !scrollBar
        || scrollBar->value() >= scrollBar->maximum();
    QTextCursor cursor(outputText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    QTextBlockFormat format;
    format.setBottomMargin(4);
    cursor.mergeBlockFormat(format);
    if (!outputText->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
    if (followTail && scrollBar)
        scrollBar->setValue(scrollBar->maximum());
    service->markReadThrough(pendingSequence);
}

bool ActivityLogPanelCoordinator::isVisibleToUser() const
{
    return outputText
        && outputText->isVisible()
        && !outputText->visibleRegion().isEmpty();
}
