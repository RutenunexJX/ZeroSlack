#include "relationshipanalysisqueue.h"

RelationshipAnalysisQueue::RelationshipAnalysisQueue(QObject* parent)
    : QObject(parent)
{
}

void RelationshipAnalysisQueue::setContentProvider(
    std::function<QString(const QString&)> provider)
{
    contentProvider = std::move(provider);
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
