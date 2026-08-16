#include "activitylogpanelcoordinator.h"

#include "activitylogservice.h"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

ActivityLogPanelCoordinator::ActivityLogPanelCoordinator(QWidget* parent)
    : service(ActivityLogService::getInstance())
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addStretch(1);
    clearButton = new QPushButton(QObject::tr("Clear"), panel);
    clearButton->setObjectName(QStringLiteral("activityLogClearButton"));
    toolbar->addWidget(clearButton);
    layout->addLayout(toolbar);

    outputText = new QPlainTextEdit(panel);
    outputText->setObjectName(QStringLiteral("activityOutputText"));
    outputText->setReadOnly(true);
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
    if (!outputText->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(text);
    cursor.endEditBlock();
    if (followTail && scrollBar)
        scrollBar->setValue(scrollBar->maximum());
}

bool ActivityLogPanelCoordinator::isVisibleToUser() const
{
    return activityDock
        && activityDock->isVisible()
        && !activityDock->visibleRegion().isEmpty();
}
