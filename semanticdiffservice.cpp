#include "semanticdiffservice.h"

#include <QDir>
#include <QHash>
#include <QSet>
#include <algorithm>

std::unique_ptr<SemanticDiffService> SemanticDiffService::instance = nullptr;

SemanticDiffService* SemanticDiffService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticDiffService>();
    return instance.get();
}

SemanticDiffService::SemanticDiffService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SemanticDiffService::~SemanticDiffService() = default;

void SemanticDiffService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SemanticDiffReport SemanticDiffService::buildSemanticDiff(
    const SemanticDiffQuery& query) const
{
    SemanticDiffReport report;
    if (!query.beforeSnapshot || !query.afterSnapshot)
        return report;

    report.symbolChanges = symbolChanges(query);
    report.relationshipChanges = relationshipChanges(query);
    report.diagnosticChanges = diagnosticChanges(query);
    report.found = !report.symbolChanges.isEmpty()
        || !report.relationshipChanges.isEmpty()
        || !report.diagnosticChanges.isEmpty();
    return report;
}

SemanticIndex* SemanticDiffService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

QList<SemanticDiffSymbolChange> SemanticDiffService::symbolChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, sym_list::SymbolInfo> beforeSymbols;
    QHash<QString, sym_list::SymbolInfo> afterSymbols;
    QHash<QString, SemanticDiffSymbolCategory> categories;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       const QString& fileName,
                       QHash<QString, sym_list::SymbolInfo>* target) {
        const QList<sym_list::SymbolInfo> symbols = snapshot.getSymbols(fileName);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            SemanticDiffSymbolCategory category;
            if (!symbolCategory(symbol.symbolType, &category))
                continue;
            if (!symbolInScope(symbol, query.moduleName, fileName))
                continue;
            const QString key = symbolKey(symbol, category);
            target->insert(key, symbol);
            categories.insert(key, category);
        }
    };

    collect(*query.beforeSnapshot, query.beforeFileName, &beforeSymbols);
    collect(*query.afterSnapshot, query.afterFileName, &afterSymbols);

    QList<SemanticDiffSymbolChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeSymbols.constBegin(); it != beforeSymbols.constEnd(); ++it)
        allKeys.insert(it.key());
    for (auto it = afterSymbols.constBegin(); it != afterSymbols.constEnd(); ++it)
        allKeys.insert(it.key());

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeSymbols.contains(key);
        const bool hasAfter = afterSymbols.contains(key);
        SemanticDiffSymbolChange change;
        change.key = key;
        change.category = categories.value(key, SemanticDiffSymbolCategory::Signal);
        if (hasBefore)
            change.beforeSymbol = beforeSymbols.value(key);
        if (hasAfter)
            change.afterSymbol = afterSymbols.value(key);

        if (!hasBefore) {
            change.kind = SemanticDiffChangeKind::Added;
            changes.append(change);
            continue;
        }
        if (!hasAfter) {
            change.kind = SemanticDiffChangeKind::Removed;
            changes.append(change);
            continue;
        }
        if (symbolSignature(change.beforeSymbol)
            != symbolSignature(change.afterSymbol)) {
            change.kind = SemanticDiffChangeKind::Modified;
            changes.append(change);
        }
    }

    sortSymbolChanges(changes);
    return changes;
}

QList<SemanticDiffRelationshipChange> SemanticDiffService::relationshipChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, SemanticRelationship> beforeRelationships;
    QHash<QString, SemanticRelationship> afterRelationships;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       bool afterSide,
                       QHash<QString, SemanticRelationship>* target) {
        const QList<SemanticRelationship> relationships = snapshot.relationships();
        for (const SemanticRelationship& relationship : relationships) {
            if (!relationshipInScope(relationship, snapshot, query, afterSide))
                continue;
            target->insert(relationshipKey(relationship, snapshot), relationship);
        }
    };

    collect(*query.beforeSnapshot, false, &beforeRelationships);
    collect(*query.afterSnapshot, true, &afterRelationships);

    QList<SemanticDiffRelationshipChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeRelationships.constBegin();
         it != beforeRelationships.constEnd(); ++it) {
        allKeys.insert(it.key());
    }
    for (auto it = afterRelationships.constBegin();
         it != afterRelationships.constEnd(); ++it) {
        allKeys.insert(it.key());
    }

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeRelationships.contains(key);
        const bool hasAfter = afterRelationships.contains(key);
        if (hasBefore && hasAfter)
            continue;

        SemanticDiffRelationshipChange change;
        change.key = key;
        if (hasBefore) {
            change.kind = SemanticDiffChangeKind::Removed;
            change.beforeRelationship = beforeRelationships.value(key);
            change.beforeFromSymbol =
                query.beforeSnapshot->getSymbolById(change.beforeRelationship.fromId);
            change.beforeToSymbol =
                query.beforeSnapshot->getSymbolById(change.beforeRelationship.toId);
        } else {
            change.kind = SemanticDiffChangeKind::Added;
            change.afterRelationship = afterRelationships.value(key);
            change.afterFromSymbol =
                query.afterSnapshot->getSymbolById(change.afterRelationship.fromId);
            change.afterToSymbol =
                query.afterSnapshot->getSymbolById(change.afterRelationship.toId);
        }
        changes.append(change);
    }

    sortRelationshipChanges(changes);
    return changes;
}

QList<SemanticDiffDiagnosticChange> SemanticDiffService::diagnosticChanges(
    const SemanticDiffQuery& query)
{
    QHash<QString, SemanticDiagnostic> beforeDiagnostics;
    QHash<QString, SemanticDiagnostic> afterDiagnostics;

    auto collect = [&](const SemanticIndexSnapshot& snapshot,
                       const QString& fileName,
                       QHash<QString, SemanticDiagnostic>* target) {
        const QList<SemanticDiagnostic> diagnostics = snapshot.getDiagnostics(fileName);
        for (const SemanticDiagnostic& diagnostic : diagnostics) {
            if (!diagnosticInScope(diagnostic, fileName))
                continue;
            target->insert(diagnosticKey(diagnostic), diagnostic);
        }
    };

    collect(*query.beforeSnapshot, query.beforeFileName, &beforeDiagnostics);
    collect(*query.afterSnapshot, query.afterFileName, &afterDiagnostics);

    QList<SemanticDiffDiagnosticChange> changes;
    QSet<QString> allKeys;
    for (auto it = beforeDiagnostics.constBegin();
         it != beforeDiagnostics.constEnd(); ++it) {
        allKeys.insert(it.key());
    }
    for (auto it = afterDiagnostics.constBegin();
         it != afterDiagnostics.constEnd(); ++it) {
        allKeys.insert(it.key());
    }

    for (const QString& key : std::as_const(allKeys)) {
        const bool hasBefore = beforeDiagnostics.contains(key);
        const bool hasAfter = afterDiagnostics.contains(key);
        if (hasBefore && hasAfter)
            continue;

        SemanticDiffDiagnosticChange change;
        change.key = key;
        if (hasBefore) {
            change.kind = SemanticDiffChangeKind::Removed;
            change.beforeDiagnostic = beforeDiagnostics.value(key);
        } else {
            change.kind = SemanticDiffChangeKind::Added;
            change.afterDiagnostic = afterDiagnostics.value(key);
        }
        changes.append(change);
    }

    sortDiagnosticChanges(changes);
    return changes;
}

bool SemanticDiffService::symbolCategory(
    sym_list::sym_type_e type,
    SemanticDiffSymbolCategory* category)
{
    if (SymbolTaxonomy::isPortDeclaration(type)) {
        if (category)
            *category = SemanticDiffSymbolCategory::Port;
        return true;
    }
    if (SymbolTaxonomy::isParameterDeclaration(type)) {
        if (category)
            *category = SemanticDiffSymbolCategory::Parameter;
        return true;
    }
    if (SymbolTaxonomy::isInstanceDeclaration(type)) {
        if (category)
            *category = SemanticDiffSymbolCategory::Instance;
        return true;
    }
    if (SymbolTaxonomy::isSignalDeclaration(type)) {
        if (category)
            *category = SemanticDiffSymbolCategory::Signal;
        return true;
    }
    return false;
}

bool SemanticDiffService::symbolInScope(
    const sym_list::SymbolInfo& symbol,
    const QString& moduleName,
    const QString& fileName)
{
    if (!fileName.isEmpty()
        && normalizedFileName(symbol.fileName) != normalizedFileName(fileName)) {
        return false;
    }
    if (moduleName.isEmpty())
        return true;
    return symbol.moduleScope == moduleName;
}

bool SemanticDiffService::relationshipInScope(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot,
    const SemanticDiffQuery& query,
    bool afterSide)
{
    const sym_list::SymbolInfo fromSymbol =
        snapshot.getSymbolById(relationship.fromId);
    const sym_list::SymbolInfo toSymbol =
        snapshot.getSymbolById(relationship.toId);
    const QString fileName = afterSide ? query.afterFileName : query.beforeFileName;

    auto endpointInScope = [&](const sym_list::SymbolInfo& symbol) {
        if (symbol.symbolId < 0)
            return false;
        if (!fileName.isEmpty()
            && normalizedFileName(symbol.fileName) != normalizedFileName(fileName)) {
            return false;
        }
        if (query.moduleName.isEmpty())
            return true;
        return symbol.moduleScope == query.moduleName
            || symbol.symbolName == query.moduleName;
    };

    return endpointInScope(fromSymbol) || endpointInScope(toSymbol);
}

bool SemanticDiffService::diagnosticInScope(
    const SemanticDiagnostic& diagnostic,
    const QString& fileName)
{
    if (fileName.isEmpty())
        return true;
    return normalizedFileName(diagnostic.fileName) == normalizedFileName(fileName);
}

QString SemanticDiffService::symbolKey(
    const sym_list::SymbolInfo& symbol,
    SemanticDiffSymbolCategory category)
{
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(category))
        .arg(symbol.moduleScope)
        .arg(symbol.symbolName);
}

QString SemanticDiffService::symbolSignature(const sym_list::SymbolInfo& symbol)
{
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(symbol.symbolType))
        .arg(symbol.dataType);
}

QString SemanticDiffService::relationshipKey(
    const SemanticRelationship& relationship,
    const SemanticIndexSnapshot& snapshot)
{
    const sym_list::SymbolInfo fromSymbol =
        snapshot.getSymbolById(relationship.fromId);
    const sym_list::SymbolInfo toSymbol =
        snapshot.getSymbolById(relationship.toId);
    SemanticDiffSymbolCategory fromCategory = SemanticDiffSymbolCategory::Signal;
    SemanticDiffSymbolCategory toCategory = SemanticDiffSymbolCategory::Signal;
    symbolCategory(fromSymbol.symbolType, &fromCategory);
    symbolCategory(toSymbol.symbolType, &toCategory);
    const QString fromKey = SymbolTaxonomy::isModuleDeclaration(fromSymbol.symbolType)
        ? QStringLiteral("module:%1").arg(fromSymbol.symbolName)
        : symbolKey(fromSymbol, fromCategory);
    const QString toKey = SymbolTaxonomy::isModuleDeclaration(toSymbol.symbolType)
        ? QStringLiteral("module:%1").arg(toSymbol.symbolName)
        : symbolKey(toSymbol, toCategory);
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(relationship.type))
        .arg(fromKey)
        .arg(toKey);
}

QString SemanticDiffService::diagnosticKey(const SemanticDiagnostic& diagnostic)
{
    return QStringLiteral("%1:%2:%3:%4:%5")
        .arg(normalizedFileName(diagnostic.fileName))
        .arg(diagnostic.line)
        .arg(diagnostic.column)
        .arg(static_cast<int>(diagnostic.severity))
        .arg(diagnostic.message);
}

QString SemanticDiffService::normalizedFileName(const QString& fileName)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(fileName));
}

QString SemanticDiffService::changeKindName(SemanticDiffChangeKind kind)
{
    switch (kind) {
    case SemanticDiffChangeKind::Added:
        return QStringLiteral("added");
    case SemanticDiffChangeKind::Removed:
        return QStringLiteral("removed");
    case SemanticDiffChangeKind::Modified:
        return QStringLiteral("modified");
    }
    return QString();
}

void SemanticDiffService::sortSymbolChanges(
    QList<SemanticDiffSymbolChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffSymbolChange& lhs,
                 const SemanticDiffSymbolChange& rhs) {
                  if (lhs.category != rhs.category)
                      return static_cast<int>(lhs.category)
                          < static_cast<int>(rhs.category);
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}

void SemanticDiffService::sortRelationshipChanges(
    QList<SemanticDiffRelationshipChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffRelationshipChange& lhs,
                 const SemanticDiffRelationshipChange& rhs) {
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}

void SemanticDiffService::sortDiagnosticChanges(
    QList<SemanticDiffDiagnosticChange>& changes)
{
    std::sort(changes.begin(), changes.end(),
              [](const SemanticDiffDiagnosticChange& lhs,
                 const SemanticDiffDiagnosticChange& rhs) {
                  if (lhs.key != rhs.key)
                      return lhs.key < rhs.key;
                  return changeKindName(lhs.kind) < changeKindName(rhs.kind);
              });
}
