#include "relationshipanalysisqueue.h"

#include <QTimer>

RelationshipAnalysisQueue::~RelationshipAnalysisQueue()
{
    for (QTimer* timer : timers) {
        if (timer)
            timer->stop();
    }
}

void RelationshipAnalysisQueue::schedule(
    const QString& fileName,
    const QString& content,
    int delayMs)
{
    if (fileName.isEmpty() || content.isEmpty())
        return;

    const QString pending = pendingContent.value(fileName);
    const QString lastContent = pending.isNull()
        ? lastContentByFile.value(fileName)
        : pending;
    if (!lastContent.isNull()
        && !contentDiffersBeyondWhitespace(lastContent, content)) {
        return;
    }

    pendingContent.insert(fileName, content);

    cancel(fileName);

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, timer]() {
        if (timers.value(fileName) == timer)
            timers.remove(fileName);
        timer->deleteLater();

        const QString content = contentProvider
            ? contentProvider(fileName)
            : QString();
        pendingContent.remove(fileName);
        if (!content.isNull())
            emit relationshipAnalysisRequested(fileName, content);
    });
    timers[fileName] = timer;
    timer->start();
}

void RelationshipAnalysisQueue::cancel(const QString& fileName)
{
    if (!timers.contains(fileName))
        return;

    QTimer* timer = timers.take(fileName);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }
}

void RelationshipAnalysisQueue::cancelAll()
{
    const QStringList fileNames = timers.keys();
    for (const QString& fileName : fileNames)
        cancel(fileName);
    pendingContent.clear();
}

void RelationshipAnalysisQueue::clearFile(const QString& fileName)
{
    cancel(fileName);
    pendingContent.remove(fileName);
    lastContentByFile.remove(fileName);
}

bool RelationshipAnalysisQueue::hasScheduled(const QString& fileName) const
{
    QTimer* timer = timers.value(fileName, nullptr);
    return timer && timer->isActive();
}
