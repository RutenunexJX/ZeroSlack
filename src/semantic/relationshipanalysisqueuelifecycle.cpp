#include "relationshipanalysisqueue.h"
#include "semanticanalysisinput.h"

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
    if (fileName.isEmpty() || content.isNull())
        return;
    const QString key = SemanticInputCapture::pathKey(fileName);
    // Coalesce only an identical pending request. A requested-but-cancelled
    // publication is not evidence that these bytes have been analyzed.
    if (timers.contains(key) && pendingContent.value(key) == content)
        return;
    cancel(fileName);
    pendingContent.insert(key, content);

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, key, timer]() {
        if (timers.value(key) != timer)
            return;
        timers.remove(key);
        timer->deleteLater();

        const QString content = contentProvider
            ? contentProvider(fileName)
            : pendingContent.value(key);
        pendingContent.remove(key);
        if (!content.isNull())
            emit relationshipAnalysisRequested(fileName, content);
    });
    timers[key] = timer;
    timer->start();
}

void RelationshipAnalysisQueue::cancel(const QString& fileName)
{
    const QString key = SemanticInputCapture::pathKey(fileName);
    pendingContent.remove(key);
    QTimer* timer = timers.take(key);
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
    pendingContent.remove(SemanticInputCapture::pathKey(fileName));
    lastContentByFile.remove(SemanticInputCapture::pathKey(fileName));
}

bool RelationshipAnalysisQueue::hasScheduled(const QString& fileName) const
{
    QTimer* timer = timers.value(SemanticInputCapture::pathKey(fileName), nullptr);
    return timer && timer->isActive();
}
