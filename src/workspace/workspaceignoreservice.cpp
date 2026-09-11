#include "workspaceignoreservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

std::unique_ptr<WorkspaceIgnoreService> WorkspaceIgnoreService::instance = nullptr;

namespace {
QString normalizePath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString normalizeWorkspaceChild(const QString& workspaceRoot,
                                const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();

    const QFileInfo info(trimmed);
    const QString absolutePath = info.isAbsolute()
        ? trimmed
        : QDir(workspaceRoot).absoluteFilePath(trimmed);
    return normalizePath(absolutePath);
}

bool isInsideWorkspace(const QString& workspaceRoot, const QString& path)
{
    return path.startsWith(workspaceRoot + QLatin1Char('/'),
                           Qt::CaseInsensitive);
}

void sortUnique(QStringList* values)
{
    if (!values)
        return;
    values->sort(Qt::CaseInsensitive);
    values->removeDuplicates();
}
}

WorkspaceIgnoreService* WorkspaceIgnoreService::getInstance()
{
    if (!instance)
        instance = std::make_unique<WorkspaceIgnoreService>();
    return instance.get();
}

WorkspaceIgnoreReport WorkspaceIgnoreService::normalizeIgnoredDirectories(
    const WorkspaceIgnoreQuery& query) const
{
    WorkspaceIgnoreReport report;

    const QString workspaceRoot = normalizePath(query.workspaceRoot);
    if (workspaceRoot.isEmpty()) {
        report.failureReason = QStringLiteral("No workspace is open.");
        report.issues.append(
            WorkspaceIgnoreIssue{query.workspaceRoot, report.failureReason});
        return report;
    }

    const QFileInfo workspaceInfo(workspaceRoot);
    if (!workspaceInfo.exists() || !workspaceInfo.isDir()) {
        report.failureReason =
            QStringLiteral("Workspace root is not a directory.");
        report.issues.append(
            WorkspaceIgnoreIssue{query.workspaceRoot, report.failureReason});
        return report;
    }

    QStringList normalizedDirectories;
    for (const QString& inputPath : query.directoryPaths) {
        const QString normalized =
            normalizeWorkspaceChild(workspaceRoot, inputPath);
        if (normalized.isEmpty())
            continue;

        if (normalized == workspaceRoot) {
            const QString reason =
                QStringLiteral("Cannot ignore the workspace root.");
            report.issues.append(WorkspaceIgnoreIssue{inputPath, reason});
            continue;
        }
        if (!isInsideWorkspace(workspaceRoot, normalized)) {
            const QString reason =
                QStringLiteral("Ignored directory must be inside workspace.");
            report.issues.append(WorkspaceIgnoreIssue{inputPath, reason});
            continue;
        }

        const QFileInfo directoryInfo(normalized);
        if (directoryInfo.exists() && !directoryInfo.isDir()) {
            const QString reason =
                QStringLiteral("Ignored path is not a directory.");
            report.issues.append(WorkspaceIgnoreIssue{inputPath, reason});
            continue;
        }

        normalizedDirectories.append(normalized);
    }

    if (!report.issues.isEmpty()) {
        report.failureReason = report.issues.first().reason;
        return report;
    }

    sortUnique(&normalizedDirectories);
    report.valid = true;
    report.ignoredDirectories = normalizedDirectories;
    return report;
}
