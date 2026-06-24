#ifndef SLANGMANAGER_H
#define SLANGMANAGER_H

#include "semanticindex.h"
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

    /// Parses the file content with Slang, elaborates, and collects all module/interface/program
    /// instantiations. Returns empty list on parse/elaboration failure (exceptions caught).
    /// Line numbers in the result are 1-based.
    QVector<ModuleInstantiationInfo> extractModuleInstantiations(const QString& fileName,
                                                                  const QString& content,
                                                                  const QStringList& includeDirs = {},
                                                                  const QHash<QString, QString>& defines = {});

    /// Parses file content once with Slang and returns all relationship facts used by the
    /// relationship builder.
    RelationshipExtractionInfo extractRelationshipInfo(const QString& fileName,
                                                       const QString& content,
                                                       const QStringList& includeDirs = {},
                                                       const QHash<QString, QString>& defines = {});

    /// Parses all workspace files together and returns relationship facts grouped
    /// by normalized source file path. This preserves cross-compilation-unit
    /// typedefs/packages for relationship extraction.
    QHash<QString, RelationshipExtractionInfo> extractWorkspaceRelationshipInfo(
        const QStringList& filePaths,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {});

    /// Parses file content with Slang and returns resolved task/function calls, excluding system calls.
    QVector<SubroutineCallInfo> extractSubroutineCalls(const QString& fileName,
                                                       const QString& content,
                                                       const QStringList& includeDirs = {},
                                                       const QHash<QString, QString>& defines = {});

    /// Parses file content with Slang and returns assignment left/right symbol references.
    QVector<AssignmentInfo> extractAssignments(const QString& fileName,
                                               const QString& content,
                                               const QStringList& includeDirs = {},
                                               const QHash<QString, QString>& defines = {});

    /// Parses file content with Slang and returns symbols read by condition/control expressions.
    QVector<ConditionReferenceInfo> extractConditionReferences(const QString& fileName,
                                                               const QString& content,
                                                               const QStringList& includeDirs = {},
                                                               const QHash<QString, QString>& defines = {});

    /// Parses file content with Slang and returns signal references from event/timing controls.
    QVector<TimingSignalInfo> extractTimingSignals(const QString& fileName,
                                                   const QString& content,
                                                   const QStringList& includeDirs = {},
                                                   const QHash<QString, QString>& defines = {});

    /// Single-file / hot-edit: parse content and extract semantic-native symbol records.
    /// Returns empty list on parse/elaboration failure (exceptions caught).
    QList<SemanticSymbolRecord> extractSymbolRecords(
        const QString& fileName,
        const QString& content,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {});

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
        std::function<bool()> isCancelled = nullptr);

    /// Workspace-wide: load all SV files (by path), compile together, and return diagnostics.
    QList<SemanticDiagnostic> extractWorkspaceDiagnostics(
        const QStringList& filePaths,
        const QStringList& includeDirs = {},
        const QHash<QString, QString>& defines = {},
        std::function<bool()> isCancelled = nullptr);
};

#endif // SLANGMANAGER_H
