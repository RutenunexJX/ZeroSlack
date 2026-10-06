#ifndef ZEROSLACKCLI_H
#define ZEROSLACKCLI_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

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
    enum class ObservationStage { WorkspaceCaptured, BeforeOutput };
    // Deterministic interleaving for contract tests; no command-line switch or
    // process-global callback can enable it in the shipped CLI.
    using ObservationHook = std::function<void(ObservationStage)>;
    // An empty endpoint uses the SDK default; explicit endpoints isolate callers.
    explicit ZeroSlackCliService(ObservationHook hook = {}, QString runtimeEndpoint = {})
        : observationHook(std::move(hook)), suiteRuntimeEndpoint(std::move(runtimeEndpoint)) {}
    static constexpr int kSchemaVersion = 1;

    ZeroSlackCliResult execute(const ZeroSlackCliRequest& request) const;
    QString cachePathForWorkspace(const QString& workspaceRoot,
                                  const QString& cacheDirectory = {}) const;

    static QByteArray render(const QJsonObject& envelope,
                             const QString& format);
    static QString usageText();
private:
    ObservationHook observationHook;
    QString suiteRuntimeEndpoint;
};

#endif // ZEROSLACKCLI_H
