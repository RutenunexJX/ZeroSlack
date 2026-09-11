#include "diagnosticnavigationservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {
QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool diagnosticComesAfter(const DiagnosticResult& result,
                          const QString& currentFileName,
                          int currentLine,
                          int currentColumn)
{
    const QString resultFile = normalizedFileName(result.diagnostic.fileName);
    const QString currentFile = normalizedFileName(currentFileName);
    if (QString::compare(resultFile, currentFile, Qt::CaseInsensitive) != 0)
        return QString::compare(resultFile, currentFile, Qt::CaseInsensitive) > 0;
    if (result.diagnostic.line != currentLine)
        return result.diagnostic.line > currentLine;
    return result.diagnostic.column > currentColumn;
}

bool diagnosticComesBefore(const DiagnosticResult& result,
                           const QString& currentFileName,
                           int currentLine,
                           int currentColumn)
{
    const QString resultFile = normalizedFileName(result.diagnostic.fileName);
    const QString currentFile = normalizedFileName(currentFileName);
    if (QString::compare(resultFile, currentFile, Qt::CaseInsensitive) != 0)
        return QString::compare(resultFile, currentFile, Qt::CaseInsensitive) < 0;
    if (result.diagnostic.line != currentLine)
        return result.diagnostic.line < currentLine;
    return result.diagnostic.column < currentColumn;
}

bool diagnosticLocationLess(const DiagnosticResult& lhs,
                            const DiagnosticResult& rhs)
{
    const QString leftFile = normalizedFileName(lhs.diagnostic.fileName);
    const QString rightFile = normalizedFileName(rhs.diagnostic.fileName);
    const int fileCompare = QString::compare(leftFile,
                                             rightFile,
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.diagnostic.line != rhs.diagnostic.line)
        return lhs.diagnostic.line < rhs.diagnostic.line;
    if (lhs.diagnostic.column != rhs.diagnostic.column)
        return lhs.diagnostic.column < rhs.diagnostic.column;
    return QString::compare(lhs.diagnostic.message,
                            rhs.diagnostic.message,
                            Qt::CaseInsensitive) < 0;
}
}

DiagnosticNavigationService::DiagnosticNavigationService(
    DiagnosticService* diagnosticService)
    : diagnostics(diagnosticService)
{
}

DiagnosticNavigationResult DiagnosticNavigationService::navigate(
    const DiagnosticNavigationQuery& query) const
{
    DiagnosticNavigationResult result;
    DiagnosticPanelQueryOptions options;
    options.scope = query.scope;
    options.severity = query.severity;
    options.currentFileName = query.currentFileName;
    options.workspaceFiles = query.workspaceFiles;
    const DiagnosticQuery diagnosticQuery =
        diagnosticService()->queryForPanel(options);
    QList<DiagnosticResult> diagnostics =
        diagnosticService()->findDiagnostics(diagnosticQuery);
    if (diagnostics.isEmpty()) {
        result.failureReason = QStringLiteral("No diagnostics");
        return result;
    }
    std::sort(diagnostics.begin(), diagnostics.end(), diagnosticLocationLess);

    if (!query.previous) {
        for (const DiagnosticResult& item : diagnostics) {
            if (diagnosticComesAfter(item,
                                     query.currentFileName,
                                     query.currentLine,
                                     query.currentColumn)) {
                result.found = true;
                result.diagnostic = item;
                return result;
            }
        }
        result.found = true;
        result.diagnostic = diagnostics.first();
        return result;
    }

    for (auto it = diagnostics.crbegin(); it != diagnostics.crend(); ++it) {
        if (diagnosticComesBefore(*it,
                                  query.currentFileName,
                                  query.currentLine,
                                  query.currentColumn)) {
            result.found = true;
            result.diagnostic = *it;
            return result;
        }
    }
    result.found = true;
    result.diagnostic = diagnostics.last();
    return result;
}

DiagnosticService* DiagnosticNavigationService::diagnosticService() const
{
    return diagnostics ? diagnostics : DiagnosticService::getInstance();
}
