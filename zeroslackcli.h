#ifndef ZEROSLACKCLI_H
#define ZEROSLACKCLI_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct ZeroSlackCliRequest {
    QString command;
    QString workspaceRoot;
    QString format = QStringLiteral("json");
    QString cacheDirectory;
    QString filePath;
    QString symbol;
    QString query;
    QString baseRef;
    int line = 0;
    int depth = 1;
    int maxTokens = 4000;
    QStringList includedProviders;
    bool lineSpecified = false;
    bool forceRefresh = false;
    bool allowRefresh = true;
    bool requireCurrent = false;
};

struct ZeroSlackCliResult {
    int exitCode = 0;
    QJsonObject envelope;
    QByteArray rendered;

    bool succeeded() const { return exitCode == 0; }
};

class ZeroSlackCliService
{
public:
    static constexpr int kSchemaVersion = 1;

    ZeroSlackCliResult execute(const ZeroSlackCliRequest& request) const;
    QString cachePathForWorkspace(const QString& workspaceRoot,
                                  const QString& cacheDirectory = {}) const;

    static QByteArray render(const QJsonObject& envelope,
                             const QString& format);
    static QString usageText();
};

#endif // ZEROSLACKCLI_H
