#ifndef SEMANTICDIFFSERVICE_H
#define SEMANTICDIFFSERVICE_H

#include "semanticindex.h"
#include "semanticindexsnapshot.h"

#include <QList>
#include <QString>
#include <memory>

enum class SemanticDiffChangeKind {
    Added,
    Removed,
    Modified
};

enum class SemanticDiffSymbolCategory {
    Port,
    Parameter,
    Instance,
    Signal
};

struct SemanticDiffQuery {
    std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot;
    QString moduleName;
    QString beforeFileName;
    QString afterFileName;
};

struct SemanticDiffSymbolChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    SemanticDiffSymbolCategory category = SemanticDiffSymbolCategory::Signal;
    QString key;
    sym_list::SymbolInfo beforeSymbol = {};
    sym_list::SymbolInfo afterSymbol = {};
};

struct SemanticDiffRelationshipChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    QString key;
    SemanticRelationship beforeRelationship;
    sym_list::SymbolInfo beforeFromSymbol = {};
    sym_list::SymbolInfo beforeToSymbol = {};
    SemanticRelationship afterRelationship;
    sym_list::SymbolInfo afterFromSymbol = {};
    sym_list::SymbolInfo afterToSymbol = {};
};

struct SemanticDiffDiagnosticChange {
    SemanticDiffChangeKind kind = SemanticDiffChangeKind::Added;
    QString key;
    SemanticDiagnostic beforeDiagnostic;
    SemanticDiagnostic afterDiagnostic;
};

struct SemanticDiffReport {
    bool found = false;
    QList<SemanticDiffSymbolChange> symbolChanges;
    QList<SemanticDiffRelationshipChange> relationshipChanges;
    QList<SemanticDiffDiagnosticChange> diagnosticChanges;
};

class SemanticDiffService
{
public:
    static SemanticDiffService* getInstance();

    explicit SemanticDiffService(SemanticIndex* semanticIndex = nullptr);
    ~SemanticDiffService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    SemanticDiffReport buildSemanticDiff(const SemanticDiffQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SemanticDiffService> instance;

    SemanticIndex* semanticIndex() const;

    static QList<SemanticDiffSymbolChange> symbolChanges(
        const SemanticDiffQuery& query);
    static QList<SemanticDiffRelationshipChange> relationshipChanges(
        const SemanticDiffQuery& query);
    static QList<SemanticDiffDiagnosticChange> diagnosticChanges(
        const SemanticDiffQuery& query);

    static bool symbolCategory(sym_list::sym_type_e type,
                               SemanticDiffSymbolCategory* category);
    static bool symbolInScope(const sym_list::SymbolInfo& symbol,
                              const QString& moduleName,
                              const QString& fileName);
    static bool relationshipInScope(
        const SemanticRelationship& relationship,
        const SemanticIndexSnapshot& snapshot,
        const SemanticDiffQuery& query,
        bool afterSide);
    static bool diagnosticInScope(const SemanticDiagnostic& diagnostic,
                                  const QString& fileName);

    static QString symbolKey(const sym_list::SymbolInfo& symbol,
                             SemanticDiffSymbolCategory category);
    static QString symbolSignature(const sym_list::SymbolInfo& symbol);
    static QString relationshipKey(const SemanticRelationship& relationship,
                                   const SemanticIndexSnapshot& snapshot);
    static QString diagnosticKey(const SemanticDiagnostic& diagnostic);
    static QString normalizedFileName(const QString& fileName);
    static QString changeKindName(SemanticDiffChangeKind kind);

    static void sortSymbolChanges(QList<SemanticDiffSymbolChange>& changes);
    static void sortRelationshipChanges(
        QList<SemanticDiffRelationshipChange>& changes);
    static void sortDiagnosticChanges(QList<SemanticDiffDiagnosticChange>& changes);
};

#endif // SEMANTICDIFFSERVICE_H
