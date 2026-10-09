#pragma once
#include "model.h"
#include <QJsonObject>
#include <QCache>
#include <atomic>

namespace simdock {
struct ScanMetrics {
    qint64 readFiles = 0, parsedFiles = 0, reusedFiles = 0;
    qint64 readNs = 0, analysisNs = 0, totalNs = 0;
};
// Owned by one scan worker. Cache syntax snapshots only; semantic elaboration
// still uses the complete, fresh selected source set through ZeroSlack's Slang.
struct SourceCache {
    QString root;
    QCache<QString, SourceFile> files{64 * 1024}; // KiB; bounded per workbench.
};
QString resolvedPath(const QString& path);
bool insideWorkspace(const QString& root, const QString& path);
QString projectDirectory(const QString& root);
Scan scanWorkspace(const QString& root, const std::atomic_bool* cancelled = nullptr,
                   ScanMetrics* metrics = nullptr, SourceCache* cache = nullptr);
QList<Project> loadProjects(const QString& root, QStringList* errors = nullptr);
bool saveProject(const QString& root, const Project& project, QString* error);
Project newProject(const QString& name);
QString projectTbPath(const Project& project, const QString& top);
QString validateInputs(const QString& root, const Project& project,
                       QMap<QString, QByteArray>* fingerprints = nullptr,
                       const std::atomic_bool* cancelled = nullptr,
                       const QMap<QString, QByteArray>& overrides = {});
bool writeNewTb(const QString& root, const QString& relativePath, const QString& content, QString* error);
QJsonObject projectJson(const Project& project);
}
