#ifndef SLANGMANAGER_H
#define SLANGMANAGER_H

#include "syminfo.h"
#include <QString>
#include <QVector>
#include <QList>

/// Result of one module instantiation: instance name, module (definition) name, and source line (1-based).
struct ModuleInstantiationInfo {
    QString instanceName;
    QString moduleName;
    int lineNumber;  // 1-based for Qt/UI
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

    /// Single-file / hot-edit: parse content and extract symbols into sym_list::SymbolInfo list.
    /// Returns empty list on parse/elaboration failure (exceptions caught).
    QList<sym_list::SymbolInfo> extractSymbols(const QString& fileName, const QString& content);

    /// Workspace-wide: load all SV files (by path), compile together, extract all symbols.
    /// filePaths are read from disk inside this call. Returns empty list on failure.
    QList<sym_list::SymbolInfo> extractWorkspaceSymbols(const QStringList& filePaths);
};

#endif // SLANGMANAGER_H
