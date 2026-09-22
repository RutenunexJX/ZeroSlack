#ifndef SEMANTICDEPENDENCYGRAPH_H
#define SEMANTICDEPENDENCYGRAPH_H

#include "projectmodel.h"

#include <QFlags>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

enum class SemanticDependencyKind : unsigned int {
    None = 0,
    Include = 1u << 0,
    Macro = 1u << 1,
    Package = 1u << 2,
    Instantiation = 1u << 3,
    TypeOrApi = 1u << 4,
    ActiveTop = 1u << 5,
    All = 0xffffffffu
};
Q_DECLARE_FLAGS(SemanticDependencyKinds, SemanticDependencyKind)
Q_DECLARE_OPERATORS_FOR_FLAGS(SemanticDependencyKinds)

struct SemanticModuleAssociationFact {
    QString name;
    int position = 0;
};

struct SemanticModuleInstantiationFact {
    QString ownerName;
    QString targetName;
    QString instanceName;
    QString constructKind;
    QList<SemanticModuleAssociationFact> parameterAssociations;
    QList<SemanticModuleAssociationFact> portAssociations;
    int sourceLine = 0;
    int sourceColumn = 0;
    bool syntaxComplete = false;
    QString failureReason;
};

struct SemanticFileDependencyFacts {
    QString fileName;
    QStringList includeNames;
    QSet<QString> moduleDeclarations;
    QSet<QString> packageDeclarations;
    QSet<QString> macroDefinitions;
    QSet<QString> apiDeclarations;
    QSet<QString> instantiatedModules;
    QList<SemanticModuleInstantiationFact> moduleInstantiations;
    QSet<QString> importedPackages;
    QSet<QString> macroUses;
    QSet<QString> symbolReferences;
    bool parseError = false;
};

class SemanticDependencyGraph
{
public:
    static SemanticFileDependencyFacts extractFacts(const QString& fileName, const QString& content);
    static SemanticDependencyGraph build(
        const ProjectSnapshot& project,
        const QHash<QString, QString>& contents);

    SemanticDependencyGraph withUpdatedFile(
        const ProjectSnapshot& project,
        const QString& fileName,
        const QString& content) const;

    bool isValidFor(const ProjectSnapshot& project) const;
    bool hasParseError(const QString& fileName) const;
    bool isEmpty() const { return factsByFile.isEmpty(); }

    QStringList dependenciesOf(
        const QStringList& files,
        SemanticDependencyKinds kinds = SemanticDependencyKind::All,
        bool recursive = true) const;
    QStringList dependentsOf(
        const QStringList& files,
        SemanticDependencyKinds kinds = SemanticDependencyKind::All,
        bool recursive = true) const;

    QString activeTopFile() const { return topFile; }
    QHash<QString, SemanticFileDependencyFacts> fileFacts() const
    {
        return factsByFile;
    }

private:
    ProjectSnapshot graphProject;
    QString projectIdentity;
    QString topFile;
    QHash<QString, QString> originalPathByKey;
    QHash<QString, SemanticFileDependencyFacts> factsByFile;
    QHash<QString, QHash<QString, SemanticDependencyKinds>> dependencies;
    QHash<QString, QHash<QString, SemanticDependencyKinds>> dependents;

    static QString projectKey(const ProjectSnapshot& project);
    static QString normalizedPath(const QString& fileName);
    void rebuildEdges();
    void addDependency(const QString& dependentFile,
                       const QString& dependencyFile,
                       SemanticDependencyKind kind);
    QStringList orderedFiles(const QSet<QString>& normalizedFiles) const;
    QString resolveInclude(const QString& sourceFile,
                           const QString& includeName) const;
};

#endif // SEMANTICDEPENDENCYGRAPH_H
