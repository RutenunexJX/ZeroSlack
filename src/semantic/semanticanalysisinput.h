#ifndef SEMANTICANALYSISINPUT_H
#define SEMANTICANALYSISINPUT_H

#include "semanticanalysisrequest.h"
#include <QByteArray>
#include <QSet>
#include <memory>
#include <functional>

// Value-only input envelope. It also records unsuccessful include candidates:
// a newly created, higher-priority header invalidates the old result.
struct SemanticCapturedSource {
    QString text;
    bool readable = false;
    bool overridden = false;
    bool operator==(const SemanticCapturedSource&) const = default;
};

struct SemanticAnalysisInput {
    QString projectIdentity;
    std::uint64_t generation = 0;
    QHash<QString, SemanticCapturedSource> sources;
    QHash<QString, std::uint64_t> documentRevisions;
    QStringList watchFiles;
    QStringList watchDirectories;
    bool equivalentTo(const SemanticAnalysisInput& other) const;
    qsizetype logicalBytes() const;
};

// Worker-owned capture builder, sealed into a shared const envelope on return.
// No Slang objects and no QObject / document pointers cross the worker boundary.
class SemanticInputCapture {
public:
    explicit SemanticInputCapture(const SemanticAnalysisRequest& request);
    static QString pathKey(const QString& path);
    const SemanticCapturedSource& source(const QString& path);
    bool capture(const QStringList& paths, const std::function<bool()>& cancelled);
    QHash<QString, QString> contents() const;
    bool stillMatchesDisk(const std::function<bool()>& cancelled) const;
    void beginCompilationObservation() { compilationObservation = true; observedPaths.clear(); }
    std::shared_ptr<const SemanticAnalysisInput> seal() const;
    const SemanticAnalysisInput& input() const { return captured; }
private:
    SemanticAnalysisInput captured;
    QSet<QString> observedPaths;
    bool compilationObservation = false;
    static SemanticCapturedSource read(const QString& path);
};

#endif
