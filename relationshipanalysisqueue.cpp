#include "relationshipanalysisqueue.h"

#include <QTimer>

RelationshipAnalysisQueue::RelationshipAnalysisQueue(QObject* parent)
    : QObject(parent)
{
}

RelationshipAnalysisQueue::~RelationshipAnalysisQueue()
{
    for (QTimer* timer : timers) {
        if (timer)
            timer->stop();
    }
}

void RelationshipAnalysisQueue::setContentProvider(
    std::function<QString(const QString&)> provider)
{
    contentProvider = std::move(provider);
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

QString RelationshipAnalysisQueue::lastContent(const QString& fileName) const
{
    return lastContentByFile.value(fileName);
}

void RelationshipAnalysisQueue::rememberRequestedContent(
    const QString& fileName,
    const QString& content)
{
    if (!fileName.isEmpty())
        lastContentByFile.insert(fileName, content);
}

bool RelationshipAnalysisQueue::contentDiffersBeyondWhitespace(
    const QString& oldContent,
    const QString& newContent)
{
    auto withoutWhitespace = [](const QString& content) {
        QString compact;
        compact.reserve(content.size());
        for (QChar ch : content) {
            if (!ch.isSpace())
                compact.append(ch);
        }
        return compact;
    };

    return withoutWhitespace(oldContent) != withoutWhitespace(newContent);
}
