#ifndef RELATIONSHIPANALYSISQUEUE_H
#define RELATIONSHIPANALYSISQUEUE_H

#include "zeroslackexport.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

class QTimer;

class ZEROSLACK_API RelationshipAnalysisQueue : public QObject
{
    Q_OBJECT

public:
    explicit RelationshipAnalysisQueue(QObject* parent = nullptr);
    ~RelationshipAnalysisQueue() override;

    void setContentProvider(std::function<QString(const QString&)> provider);

    void schedule(const QString& fileName, const QString& content, int delayMs);
    void cancel(const QString& fileName);
    void cancelAll();
    void clearFile(const QString& fileName);
    bool hasScheduled(const QString& fileName) const;
    QString lastContent(const QString& fileName) const;
    void rememberRequestedContent(const QString& fileName, const QString& content);

signals:
    void relationshipAnalysisRequested(const QString& fileName, const QString& content);

private:
    QMap<QString, QTimer*> timers;
    QMap<QString, QString> pendingContent;
    QMap<QString, QString> lastContentByFile;
    std::function<QString(const QString&)> contentProvider;

    static bool contentDiffersBeyondWhitespace(
        const QString& oldContent,
        const QString& newContent);
};

#endif // RELATIONSHIPANALYSISQUEUE_H
