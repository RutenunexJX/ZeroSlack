#include "relationshipanalysisqueue.h"
#include "semanticanalysisinput.h"

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
    return lastContentByFile.value(SemanticInputCapture::pathKey(fileName));
}

void RelationshipAnalysisQueue::rememberRequestedContent(
    const QString& fileName,
    const QString& content)
{
    if (!fileName.isEmpty())
        lastContentByFile.insert(SemanticInputCapture::pathKey(fileName), content);
}
