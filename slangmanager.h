#ifndef SLANGMANAGER_H
#define SLANGMANAGER_H

#include "semanticindex.h"
#include "syminfo.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>

/// Result of one module instantiation: instance name, module (definition) name, and source line (1-based).
struct ModuleInstantiationInfo {
    QString instanceName;
    QString moduleName;
    int lineNumber;  // 1-based for Qt/UI
};

/// Result of one task/function call resolved by Slang.
struct SubroutineCallInfo {
    QString subroutineName;
    int lineNumber;  // 1-based for Qt/UI
};

/// Result of one assignment resolved by Slang.
struct AssignmentInfo {
    QString leftName;
    QStringList rightNames;
    int lineNumber;  // 1-based for Qt/UI
};

/// Result of one condition/control expression and the value symbols it reads.
struct ConditionReferenceInfo {
    QStringList symbolNames;
    int lineNumber;  // 1-based for Qt/UI
};

/// Result of one timing-control signal reference.
struct TimingSignalInfo {
    QString signalName;
    int lineNumber;      // 1-based for Qt/UI
    bool edgeSensitive;  // posedge/negedge/both-edge event
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
                                                                  const QString& content);

    /// Parses file content once with Slang and returns all relationship facts used by the
    /// relationship builder.
    RelationshipExtractionInfo extractRelationshipInfo(const QString& fileName,
                                                       const QString& content);

    /// Parses file content with Slang and returns resolved task/function calls, excluding system calls.
    QVector<SubroutineCallInfo> extractSubroutineCalls(const QString& fileName,
                                                       const QString& content);

    /// Parses file content with Slang and returns assignment left/right symbol references.
    QVector<AssignmentInfo> extractAssignments(const QString& fileName, const QString& content);

    /// Parses file content with Slang and returns symbols read by condition/control expressions.
    QVector<ConditionReferenceInfo> extractConditionReferences(const QString& fileName,
                                                               const QString& content);

    /// Parses file content with Slang and returns signal references from event/timing controls.
    QVector<TimingSignalInfo> extractTimingSignals(const QString& fileName, const QString& content);

    /// Single-file / hot-edit: parse content and extract symbols into sym_list::SymbolInfo list.
    /// Returns empty list on parse/elaboration failure (exceptions caught).
    QList<sym_list::SymbolInfo> extractSymbols(const QString& fileName, const QString& content);

    /// Single-file / hot-edit: parse content and return Slang diagnostics as semantic data.
    QList<SemanticDiagnostic> extractDiagnostics(const QString& fileName, const QString& content);

    /// Workspace-wide: load all SV files (by path), compile together, extract all symbols.
    /// filePaths are read from disk inside this call. Returns empty list on failure.
    QList<sym_list::SymbolInfo> extractWorkspaceSymbols(const QStringList& filePaths);

    /// Workspace-wide: load all SV files (by path), compile together, and return diagnostics.
    QList<SemanticDiagnostic> extractWorkspaceDiagnostics(const QStringList& filePaths);
};

#endif // SLANGMANAGER_H
