#ifndef SEMANTICINDEX_H
#define SEMANTICINDEX_H

#include "syminfo.h"
#include "symbolrelationshipengine.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

struct SemanticQueryContext {
    QString fileName;
    QString moduleName;
    QString prefix;
    int cursorLine = -1;      // 1-based
    int cursorPosition = -1;  // QTextDocument position, when available
};

struct SemanticRelationship {
    int fromId = -1;
    int toId = -1;
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
};

struct SemanticDiagnostic {
    enum Severity {
        Info,
        Warning,
        Error
    };

    QString fileName;
    int line = 0;
    int column = 0;
    QString message;
    Severity severity = Info;
};

// Thin facade over the current sym_list-backed semantic store.
//
// This is the migration boundary for new code: the first implementation delegates to sym_list
// and existing services, while later versions can swap in snapshots without changing callers.
class SemanticIndex
{
public:
    static SemanticIndex* getInstance();

    explicit SemanticIndex(sym_list* symbolDatabase = nullptr);
    ~SemanticIndex();

    void setSymbolDatabase(sym_list* symbolDatabase);
    sym_list* symbolDatabase() const;

    QList<sym_list::SymbolInfo> getSymbols(const QString& fileName = QString()) const;
    QList<sym_list::SymbolInfo> getSymbolsByType(sym_list::sym_type_e type) const;

    QList<sym_list::SymbolInfo> findDefinitions(const QString& name,
                                                const SemanticQueryContext& context = {}) const;
    QStringList findCompletions(const SemanticQueryContext& context) const;

    QList<SemanticRelationship> getRelationships(int symbolId, bool outgoing = true) const;
    QList<SemanticRelationship> getRelationships(const QString& scopeName,
                                                 bool outgoing = true) const;

    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    sym_list* m_symbolDatabase = nullptr;
    static std::unique_ptr<SemanticIndex> instance;

    QList<sym_list::SymbolInfo> sortedDefinitions(const QList<sym_list::SymbolInfo>& symbols,
                                                  const SemanticQueryContext& context) const;
    QList<SymbolRelationshipEngine::RelationType> relationshipTypes() const;
};

#endif // SEMANTICINDEX_H
