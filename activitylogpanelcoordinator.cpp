#include "activitylogpanelcoordinator.h"

#include "activitylogservice.h"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
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
                         if (outputText)
                             outputText->appendPlainText(
                                 ActivityLogService::formatEvent(event));
                     });
    QObject::connect(service,
                     &ActivityLogService::cleared,
                     outputText,
                     [this]() {
                         if (outputText)
                             outputText->clear();
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

    for (const ActivityLogEvent& event : service->events())
        outputText->appendPlainText(ActivityLogService::formatEvent(event));
}
