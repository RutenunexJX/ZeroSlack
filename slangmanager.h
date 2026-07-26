#ifndef SLANGMANAGER_H
#define SLANGMANAGER_H

#include "semanticindex.h"
#include "effectivevalueservice.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QHash>
#include <functional>

/// Result of one module instantiation: instance name, module (definition) name, and source line (1-based).
struct ModuleInstantiationInfo {
    QString instanceName;
    QString moduleName;
    int lineNumber;  // 1-based for Qt/UI
    SemanticSourceRange sourceRange;
};

/// Result of one task/function call resolved by Slang.
struct SubroutineCallInfo {
    QString subroutineName;
    int lineNumber;  // 1-based for Qt/UI
    SemanticSourceRange sourceRange;
};

/// Result of one assignment resolved by Slang.
struct AssignmentInfo {
    QString leftName;
    QString leftAccessPath;
    QStringList rightNames;
    QStringList rightAccessPaths;
    int lineNumber;  // 1-based for Qt/UI
    SemanticSourceRange sourceRange;
    // Slang proved the RHS is one whole value symbol, possibly through only
    // an implicit conversion, rather than an expression that merely reads it.
    bool exactValueForward = false;
};

/// Result of one condition/control expression and the value symbols it reads.
struct ConditionReferenceInfo {
    QStringList symbolNames;
    QStringList symbolAccessPaths;
    int lineNumber;  // 1-based for Qt/UI
    SemanticSourceRange sourceRange;
};

/// Result of one timing-control signal reference.
struct TimingSignalInfo {
    QString signalName;
    QString signalAccessPath;
    int lineNumber;      // 1-based for Qt/UI
    bool edgeSensitive;  // posedge/negedge/both-edge event
    SemanticSourceRange sourceRange;
};

/// All relationship facts extracted from one Slang parse.
struct RelationshipExtractionInfo {
    QVector<ModuleInstantiationInfo> moduleInstantiations;
    QVector<SubroutineCallInfo> subroutineCalls;
    QVector<AssignmentInfo> assignments;
    QVector<ConditionReferenceInfo> conditionReferences;
    QVector<TimingSignalInfo> timingSignals;
};

/// Manages Slang parsing/elaboration for semantic analysis (e.g. module instantiations, symbol extraction).
/// Tree-sitter remains used for UI (highlighting, outline); Slang is used here for accuracy.
class SlangManager
{
public:
    SlangManager() = default;
    ~SlangManager() = default;

    /// Parses all workspace files together and returns relationship facts grouped
    /// by normalized source file path. This preserves cross-compilation-unit
    /// typedefs/packages for relationship extraction.
    QHash<QString, RelationshipExtractionInfo> extractWorkspaceRelationshipInfo(
        const QStringList& filePaths,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr);

    /// Workspace / hot-edit overlay: compile the supplied in-memory sources
    /// together and return relationship facts grouped by absolute source path.
    /// orderedFilePaths preserves the project's compilation order; any
    /// remaining supplied sources are appended deterministically.
    QHash<QString, RelationshipExtractionInfo>
    extractOverlayWorkspaceRelationshipInfo(
        const QHash<QString, QString>& fileContents,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr,
        const QStringList& orderedFilePaths = {});

    /// Single-file / hot-edit: parse content and extract semantic-native symbol records.
    /// Returns empty list on parse/elaboration failure (exceptions caught).
    QList<SemanticSymbolRecord> extractSymbolRecords(
        const QString& fileName,
        const QString& content,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        QList<EffectiveValueFact>* effectiveValueFacts = nullptr);

    /// Single-file / hot-edit: parse content and return Slang diagnostics as semantic data.
    QList<SemanticDiagnostic> extractDiagnostics(const QString& fileName,
                                                 const QString& content,
                                                 const QStringList& includeDirs = {},
                                                 const QHash<QString, QString>& defines = {});

    /// Workspace-wide: load all SV files, compile together, and extract semantic-native records.
    QList<SemanticSymbolRecord> extractWorkspaceSymbolRecords(
        const QStringList& filePaths,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr,
        QList<EffectiveValueFact>* effectiveValueFacts = nullptr,
        QHash<QString, QString>* analyzedFileContents = nullptr);

    /// Workspace / hot-edit overlay: compile the supplied in-memory sources
    /// together so unsaved text participates in package and instance
    /// elaboration without writing it to disk.
    QList<SemanticSymbolRecord> extractOverlayWorkspaceSymbolRecords(
        const QHash<QString, QString>& fileContents,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr,
        QList<EffectiveValueFact>* effectiveValueFacts = nullptr,
        const QStringList& orderedFilePaths = {});

    /// Workspace-wide: load all SV files (by path), compile together, and return diagnostics.
    QList<SemanticDiagnostic> extractWorkspaceDiagnostics(
        const QStringList& filePaths,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr);

    /// Workspace / hot-edit overlay: parse and compile one immutable set of
    /// in-memory sources, including cross-file and undefined-macro diagnostics.
    QList<SemanticDiagnostic> extractOverlayWorkspaceDiagnostics(
        const QHash<QString, QString>& fileContents,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr,
        const QStringList& orderedFilePaths = {});
};

#endif // SLANGMANAGER_H
