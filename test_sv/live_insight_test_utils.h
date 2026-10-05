#pragma once
#include "liveinsightsession.h"
#include "rtlinsightspanelcoordinator.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

inline bool waitForLiveInsightReports(RtlInsightsPanelCoordinator& panel)
{
    auto* session = panel.graphSession();
    if (!session) return false;
    if (panel.dock() && panel.dock()->window()) panel.dock()->window()->show();
    for (auto kind : {LiveInsightKind::Module, LiveInsightKind::State}) session->flushPending(kind);
    QElapsedTimer timeout;
    timeout.start();
    while ((session->hasPendingUpdate(LiveInsightKind::Module)
            || session->hasPendingUpdate(LiveInsightKind::State)) && timeout.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    for (auto kind : {LiveInsightKind::Module, LiveInsightKind::State}) {
        const auto snapshot = session->snapshot(kind);
        if (session->hasPendingUpdate(kind) || snapshot.phase == LiveInsightPhase::Error) return false;
    }
    return true;
}
