#pragma once
#include <zeroslack/semantic/semanticapi.h>
#include <zeroslack/semantic/semanticindex.h>
#include <zeroslack/semantic/rtlinsightlink.h>
#include <QJsonObject>
#include <QMutex>
#include <QCache>

enum class SignalTraceDirection { Upstream, Downstream };
enum class SignalTraceStatus { Ready, DefinitionOnly, Unavailable, Cancelled, Partial, Stale };

struct ZEROSLACK_SEMANTIC_API SignalTraceObject {
    QString workspaceId, projectId, instancePath, objectPath;
    SymbolStableKey symbol;
    QString accessPath;
    SignalBitRange bits;
    QByteArray sourceHash;
    bool isValid() const { return !workspaceId.isEmpty() && !projectId.isEmpty() && symbol.isValid(); }
    bool isConcrete() const { return isValid() && !instancePath.isEmpty() && !objectPath.isEmpty(); }
    QString id() const;
    QString displayName() const;
    QJsonObject toJson() const;
    static SignalTraceObject fromJson(const QJsonObject& value);
};

struct SignalTraceContext {
    QString workspaceId, projectId, instancePath;
    SemanticSnapshotToken token;
    QHash<QString,QByteArray> currentSourceHashes;
};
struct SignalTraceEvidence {
    QString id;
    SignalTraceRole role = SignalTraceRole::Unknown;
    SemanticSourceRange range;
    RtlInsightCodeLink link;
    QString text;
    QString moduleName;
    QString objectId;
    QByteArray sourceHash;
    int fileLineCount = 0;
    bool definitionOnly = false;
    SemanticSignalDependency dependency;
};
struct SignalTraceGroup {
    SignalTraceObject object;
    QList<SignalTraceEvidence> evidence;
    bool dataPath = false;
    QStringList limitations;
};
struct SignalTraceResolution {
    SignalTraceObject object;
    QList<SignalTraceObject> choices;
    SignalTraceStatus status = SignalTraceStatus::Unavailable;
    QString message;
};
struct SignalTracePage {
    SignalTraceObject subject;
    SignalTraceDirection direction = SignalTraceDirection::Upstream;
    quint64 semanticRevision = 0;
    SignalTraceStatus status = SignalTraceStatus::Unavailable;
    QList<SignalTraceGroup> groups;
    QList<SignalTraceEvidence> references;
    QStringList limitations;
    int nextCursor = -1;
    int scanned = 0;
};

class ZEROSLACK_SEMANTIC_API SignalTraceService {
public:
    explicit SignalTraceService(SignalTraceContext context);
    const SignalTraceContext& context() const { return m_context; }
    SignalTraceResolution resolve(const SymbolStableKey& key, const QString& name = {},
        const QString& file = {}, const QString& module = {}, const QString& accessPath = {}) const;
    bool validate(const SignalTraceObject& object, QString* reason = nullptr) const;
    bool isSourceCurrent(const QString& file) const;
    SignalTracePage oneHop(const SignalTraceObject& object, SignalTraceDirection direction,
        int cursor = 0, int pageSize = 100, const std::function<bool()>& cancelled = {}) const;
    SignalTracePage references(const SignalTraceObject& object, int cursor = 0, int limit = 500,
        const std::function<bool()>& cancelled = {}) const;
    static QString roleName(SignalTraceRole role);
    static QString boundaryName(SignalStorageBoundary boundary);
    SignalTraceObject endpoint(const SemanticRelationship& edge, bool from) const;
    SignalTraceEvidence evidence(const SemanticRelationship& edge, const SignalTraceObject& subject,
        SignalTraceRole role, bool definitionOnly = false) const;
private:
    struct SourceInfo {
        QString text;
        QByteArray hash;
        QVector<qsizetype> lines;
        bool current=false;
    };
    std::shared_ptr<const SourceInfo> source(const QString& file) const;
    SignalTraceContext m_context;
    mutable QMutex m_sourceMutex;
    mutable QHash<QString,std::shared_ptr<const SourceInfo>> m_sources;
    mutable QMutex m_pageMutex;
    mutable QCache<QString,SignalTracePage> m_pages{4096};
};
