#pragma once
#include "documentblockmodel.h"
#include <QByteArray>
#include <functional>

// Portable, hash-bound association storage. A source writer calls the supplied
// commit guard only after its own atomic output has been prepared.
class ZEROSLACK_API DocumentBlockStore final : public QObject {
public:
    using Guard = std::function<bool(QString*)>;
    using SourceWriter = std::function<bool(const Guard&, QString*)>;
    static DocumentBlockStore* get(DocumentBlockModel* model);
    bool bind(const QString& source, const QString& workspace, QString* error = nullptr);
    bool reload(QString* error = nullptr);
    bool save(const QString& source, const QString& workspace, const QByteArray& expectedSource,
              const QByteArray& nextSource, bool copyIdentity, const SourceWriter& writer,
              const Guard& revalidate, QString* error);
    bool relocate(const QString& source, const QString& workspace, QString* error);
    bool attach(const QString& sidecar, QString* error);
    bool acceptMatchingGeneration(QString* error);
    QString importImage(const QString& path, QString* error);
    QString importFile(const QString& path, QString* error);
    QString importBytes(const QByteArray& bytes, const QString& suffix, QString* error);
    QByteArray assetBytes(const QString& asset, QString* error) const;
    bool validateStorage(QString* error) const;
    bool stageAsset(const QString& name, const QByteArray& bytes, QString* error);
    QByteArray pendingImage(const QString& asset) const { return pendingAssets.value(asset); }
    QString assetPath(const QString& asset) const;
    QString sourcePath() const { return source; }
    QString storagePath() const { return sidecar; }
    QString rootPath() const { return root; }
    bool needed() const;
    bool blocked() const { return !blockReason.isEmpty(); }
    QJsonObject recoveryState(QString* error, const QString& recoveryDirectory = {});
    bool restoreRecovery(const QJsonObject& state, QString* error);
    static QByteArray fileRevision(const QString& path, QString* error = nullptr);
    static QString pathFor(const QString& source, const QString& workspace);
    static bool renameWithAssociations(const QString& workspace,
        const QMap<QString,QPair<QString,QByteArray>>& files, const Guard& renameSource, QString* error);
private:
    explicit DocumentBlockStore(DocumentBlockModel* model);
    DocumentBlockModel* model;
    QString source, root, sidecar, blockReason;
    QByteArray observed, sourceRevision;
    QJsonObject savedMetadata;
    QHash<QString,QByteArray> pendingAssets;
    bool bound = false, canAcceptMatching = false;
    bool read(const QString& path, bool explicitAttach, QString* error);
    bool fail(const QString& message, QString* error);
    static QString rootFor(const QString& source, const QString& workspace);
    bool writeAssets(const QString& targetRoot, const QJsonObject& metadata, QString* error) const;
};
