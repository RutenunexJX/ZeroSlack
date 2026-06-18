#ifndef REFERENCESERVICE_H
#define REFERENCESERVICE_H

#include "relationshipservice.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

struct ReferenceQuery {
    SymbolStableKey symbolStableKey;
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool workspaceFilesOnly = false;
    bool currentFileOnly = false;
    QStringList workspaceFiles;
    QList<SymbolRelationshipEngine::RelationType> types;
};

enum class ReferencePanelScope {
    AllFiles,
    WorkspaceFiles,
    CurrentFile
};

enum class ReferenceReportNotFoundReason {
    None,
    NoSubjectSymbol,
    NoReferences
};

struct ReferencePanelQueryOptions {
    QString symbolName;
    QString fileName;
    QString moduleName;
    ReferencePanelScope scope = ReferencePanelScope::AllFiles;
    QStringList workspaceFiles;
    int typeFilter = -1;
};

struct ReferenceResult {
    RelationshipResult relationship;
    sym_list::SymbolInfo referencingSymbol;
    sym_list::SymbolInfo referencedSymbol;
    SemanticSymbolRecord referencingSymbolRecord;
    SemanticSymbolRecord referencedSymbolRecord;
    SymbolStableKey referencingStableKey;
    SymbolStableKey referencedStableKey;
    QString symbolDisplayName;
    QString fileDisplayName;
    QString lineDisplayName;
    QString relationshipTypeDisplayName;
};

struct ReferenceTypeGroup {
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    QString displayName;
    QList<ReferenceResult> references;
    int count = 0;
};

struct ReferenceFileGroup {
    QString fileName;
    QString fileKey;
    QString displayName;
    QList<ReferenceTypeGroup> typeGroups;
    int count = 0;
};

struct ReferenceReport {
    int subjectSymbolId = -1;
    sym_list::SymbolInfo subjectSymbol = {};
    SemanticSymbolRecord subjectSymbolRecord;
    SymbolStableKey subjectStableKey;
    ReferenceReportNotFoundReason notFoundReason =
        ReferenceReportNotFoundReason::None;
    QString notFoundReasonDisplayName;
    QList<ReferenceResult> references;
    QList<ReferenceFileGroup> fileGroups;
    int totalCount = 0;
    QMap<QString, int> fileCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
    QMap<QString, QMap<SymbolRelationshipEngine::RelationType, int>> fileTypeCounts;
};

class ReferenceService
{
public:
    static ReferenceService* getInstance();

    explicit ReferenceService(SemanticIndex* semanticIndex = nullptr);
    ~ReferenceService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<ReferenceResult> findReferences(const ReferenceQuery& query) const;
    ReferenceReport findReferenceReport(const ReferenceQuery& query) const;
    bool hasReferences(const ReferenceQuery& query) const;
    ReferenceQuery queryForPanel(const ReferencePanelQueryOptions& options) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<ReferenceService> instance;

    SemanticIndex* semanticIndex() const;
    sym_list::SymbolInfo resolveSubjectSymbol(const ReferenceQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(
        const ReferenceQuery& query) const;
    bool scopeMatches(const ReferenceQuery& query,
                      const SemanticSymbolRecord& record,
                      const sym_list::SymbolInfo& fallbackSymbol) const;
    ReferenceResult toReferenceResult(const RelationshipResult& relationship) const;
    static ReferenceQuery normalizedQuery(const ReferenceQuery& query);
    static QString referenceFileDisplayName(const QString& fileName);
    static QString referenceLineDisplayName(int line);
};

#endif // REFERENCESERVICE_H
