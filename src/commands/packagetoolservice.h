#ifndef PACKAGETOOLSERVICE_H
#define PACKAGETOOLSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>

enum class StructuredInlineInsertionStatus {
    Ok,
    Duplicate,
    InvalidLocation,
    InvalidTarget
};

struct StructuredInlineInsertionPlan {
    StructuredInlineInsertionStatus status =
        StructuredInlineInsertionStatus::InvalidLocation;
    int replacementStart = -1;
    int replacementEnd = -1;
    QString replacementText;
    QString failureMessage;

    bool ok() const
    {
        return status == StructuredInlineInsertionStatus::Ok
            && replacementStart >= 0
            && replacementEnd >= replacementStart;
    }

    bool duplicate() const
    {
        return status == StructuredInlineInsertionStatus::Duplicate;
    }
};

struct PackageImportSite {
    bool valid = false;
    QString enclosingPackageName;
    QStringList wildcardImportedPackages;
    QString failureMessage;

    bool canImport(const QString& packageName) const
    {
        return valid
            && !packageName.isEmpty()
            && packageName != enclosingPackageName
            && !wildcardImportedPackages.contains(packageName);
    }
};

struct HeaderIncludeCandidate {
    QString absoluteFilePath;
    QString includePath;
};

class PackageToolService
{
public:
    static PackageImportSite analyzePackageImportSite(
        const QString& documentText,
        int replacementStart,
        int replacementEnd);
    static StructuredInlineInsertionPlan packageImportPlan(
        const QString& documentText,
        int replacementStart,
        int replacementEnd,
        const QString& packageName);
    static StructuredInlineInsertionPlan headerIncludePlan(
        const QString& documentText,
        int replacementStart,
        int replacementEnd,
        const QString& includePath);
    static QList<HeaderIncludeCandidate> headerIncludeCandidates(
        const QStringList& headerFiles,
        const QString& currentFile,
        const QStringList& configuredIncludeDirs);
};

#endif // PACKAGETOOLSERVICE_H
