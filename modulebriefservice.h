#ifndef MODULEBRIEFSERVICE_H
#define MODULEBRIEFSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <memory>

struct ModuleBriefQuery {
    int moduleSymbolId = -1;
    QString moduleName;
    QString fileName;
};

struct ModuleBriefRelationshipRow {
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    int count = 0;
    QString directionDisplayName;
    QString typeDisplayName;
    QString detailDisplayName;
};

struct ModuleBriefRelationshipEvidenceRow {
    SemanticRelationshipResult relationship;
    sym_list::SymbolInfo peerSymbol = {};
    sym_list::SymbolInfo fromSymbol = {};
    sym_list::SymbolInfo toSymbol = {};
    RtlInsightCodeLink peerCodeLink;
    RtlInsightCodeLink fromCodeLink;
    RtlInsightCodeLink toCodeLink;
    bool outgoing = false;
    QString directionDisplayName;
    QString typeDisplayName;
    QString peerDisplayName;
    QString fromSymbolDisplayName;
    QString toSymbolDisplayName;
    QString detailDisplayName;
};

struct ModuleBriefRelationshipSummary {
    int outgoingCount = 0;
    int incomingCount = 0;
    int totalCount = 0;
    QMap<SymbolRelationshipEngine::RelationType, int> outgoingTypeCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> incomingTypeCounts;
    QList<ModuleBriefRelationshipRow> rows;
};

struct ModuleBriefSymbolRow {
    sym_list::SymbolInfo symbol = {};
    RtlInsightCodeLink codeLink;
    QString sectionDisplayName;
    QString typeDisplayName;
    QString detailDisplayName;
};

struct ModuleBriefDiagnosticRow {
    SemanticDiagnostic diagnostic;
    RtlInsightCodeLink codeLink;
    QString severityDisplayName;
    QString detailDisplayName;
};

struct ModuleBriefContextRow {
    sym_list::SymbolInfo symbol = {};
    RtlInsightCodeLink codeLink;
    QString sectionDisplayName;
    QString symbolDisplayName;
    QString detailDisplayName;
    QString sourceRoleDisplayName;
};

struct ModuleBriefReport {
    bool found = false;
    sym_list::SymbolInfo moduleSymbol = {};
    QList<sym_list::SymbolInfo> ports;
    QList<sym_list::SymbolInfo> parameters;
    QList<sym_list::SymbolInfo> instances;
    QList<sym_list::SymbolInfo> imports;
    QList<SemanticDiagnostic> diagnostics;
    QList<ModuleBriefSymbolRow> portRows;
    QList<ModuleBriefSymbolRow> parameterRows;
    QList<ModuleBriefSymbolRow> instanceRows;
    QList<ModuleBriefSymbolRow> importRows;
    QList<ModuleBriefDiagnosticRow> diagnosticRows;
    QList<ModuleBriefContextRow> contextRows;
    ModuleBriefRelationshipSummary relationshipSummary;
    QList<ModuleBriefRelationshipEvidenceRow> relationshipEvidenceRows;
};

class ModuleBriefService
{
public:
    static ModuleBriefService* getInstance();

    explicit ModuleBriefService(SemanticIndex* semanticIndex = nullptr);
    ~ModuleBriefService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    ModuleBriefReport buildModuleBrief(const ModuleBriefQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<ModuleBriefService> instance;

    SemanticIndex* semanticIndex() const;
    sym_list::SymbolInfo resolveModule(const ModuleBriefQuery& query) const;
    QList<sym_list::SymbolInfo> symbolsInModule(
        const sym_list::SymbolInfo& moduleSymbol,
        const QList<sym_list::SymbolInfo>& symbols,
        bool (*matchesType)(sym_list::sym_type_e)) const;
    QList<sym_list::SymbolInfo> importSymbols(
        const sym_list::SymbolInfo& moduleSymbol) const;
    QList<SemanticDiagnostic> diagnosticsForModule(
        const sym_list::SymbolInfo& moduleSymbol) const;
    ModuleBriefRelationshipSummary relationshipSummary(
        const sym_list::SymbolInfo& moduleSymbol) const;
    QList<ModuleBriefRelationshipEvidenceRow> relationshipEvidenceRows(
        const sym_list::SymbolInfo& moduleSymbol) const;

    static bool isInsideModule(const sym_list::SymbolInfo& symbol,
                               const sym_list::SymbolInfo& moduleSymbol);
    static QList<ModuleBriefSymbolRow> symbolRows(
        const QList<sym_list::SymbolInfo>& symbols,
        const QString& sectionDisplayName);
    static QList<ModuleBriefDiagnosticRow> diagnosticRows(
        const QList<SemanticDiagnostic>& diagnostics);
    static QList<ModuleBriefContextRow> contextRows(
        const QList<sym_list::SymbolInfo>& imports,
        const QList<sym_list::SymbolInfo>& ports,
        const QList<sym_list::SymbolInfo>& instances,
        const QList<sym_list::SymbolInfo>& allSymbols);
    static QList<ModuleBriefRelationshipRow> relationshipRows(
        const ModuleBriefRelationshipSummary& summary);
    static QString symbolTypeDisplayName(sym_list::sym_type_e type);
    static QString symbolDetailDisplayName(const sym_list::SymbolInfo& symbol);
    static QString diagnosticSeverityDisplayName(SemanticDiagnostic::Severity severity);
    static QString symbolDisplayName(const sym_list::SymbolInfo& symbol);
    static QString contextDetailDisplayName(const QString& kind,
                                            const sym_list::SymbolInfo& symbol);
    static QString sourceRoleDisplayName(SymbolTaxonomy::SourceRole role);
    static QString interfaceBaseName(const QString& dataType);
    static QSet<QString> interfaceNames(const QList<sym_list::SymbolInfo>& symbols);
    static QString relationshipDirectionDisplayName(bool outgoing);
    static QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type);
    static QString relationshipDetailDisplayName(int count);
    static QString relationshipEvidenceDetailDisplayName(
        const ModuleBriefRelationshipEvidenceRow& row);
    static void fillRelationshipEvidenceMetadata(
        ModuleBriefRelationshipEvidenceRow& row);
    static void sortRelationshipEvidenceRows(
        QList<ModuleBriefRelationshipEvidenceRow>& rows);
    static void sortSymbols(QList<sym_list::SymbolInfo>& symbols);
};

#endif // MODULEBRIEFSERVICE_H
