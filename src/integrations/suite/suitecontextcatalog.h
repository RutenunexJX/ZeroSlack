#ifndef SUITECONTEXTCATALOG_H
#define SUITECONTEXTCATALOG_H

#include "zeroslackexport.h"
#include "semanticindex.h"

#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>

enum class SuiteContextProvider {
    Source,
    Pinloom,
    Wave,
    RegMap
};

enum class SuiteContextAvailability {
    Available,
    Stale,
    Warning,
    Missing,
    Unavailable
};

struct ZEROSLACK_API SuiteContextDiagnostic {
    QString providerId;
    QString code;
    QString message;
    QString resourceId;

    QJsonObject toJson() const;
};

struct ZEROSLACK_API SuiteContextResource {
    SuiteContextProvider provider = SuiteContextProvider::Source;
    SuiteContextAvailability availability =
        SuiteContextAvailability::Available;
    QString stableId;
    QString title;
    QString summary;
    QString filePath;
    QUrl uri;
    QStringList symbols;
    QVariantMap metadata;

    bool isValid() const;
    QString providerId() const;
    QString stableKey() const;
    QJsonObject toJson(const QString& workspaceRoot = {}) const;
};

struct ZEROSLACK_API SuiteContextSnapshot {
    QString workspaceRoot;
    QList<SuiteContextResource> resources;
    QList<SuiteContextDiagnostic> diagnostics;
    QHash<QString, int> discoveredCounts;
    QHash<QString, qint64> revisionFiles;
    int catalogOmittedCount = 0;

    QList<SuiteContextResource> resourcesFor(
        SuiteContextProvider provider) const;
};

struct ZEROSLACK_API SuiteContextCatalogRequest {
    QString workspaceRoot;
    QString filePath;
    QString documentText;
    QString symbolName;
    int cursorPosition = -1;
    int maxItemsPerProvider = 32;
    QStringList includedProviders;
    QList<SemanticSymbolRecord> semanticSymbols;
    bool semanticSymbolsSupplied = false;
    std::function<bool()> isCancelled;
};

class ZEROSLACK_API SuiteContextCatalog
{
public:
    static constexpr int kVersion = 1;
    static constexpr int kMaximumReferences = 512;
    static constexpr int kMaximumReferenceIdLength = 160;
    static constexpr int kMaximumReferencePathLength = 4096;
    static constexpr int kMaximumReferenceUriLength = 4096;
    static constexpr int kMaximumObjectIdLength = 256;
    static constexpr int kMaximumSymbols = 64;
    static constexpr int kMaximumSymbolLength = 256;
    static constexpr qint64 kMaximumManifestBytes = 256 * 1024;
    static constexpr qint64 kMaximumPinloomStoreBytes = 8 * 1024 * 1024;
    static constexpr qint64 kMaximumProjectBytes = 1024 * 1024;

    static SuiteContextSnapshot inspect(
        const SuiteContextCatalogRequest& request);
    static QString referenceFilePath(const QString& workspaceRoot);
    static QString providerId(SuiteContextProvider provider);
    static QString availabilityId(
        SuiteContextAvailability availability);
    static bool providerFromId(const QString& id,
                               SuiteContextProvider* provider);
};

#endif // SUITECONTEXTCATALOG_H
