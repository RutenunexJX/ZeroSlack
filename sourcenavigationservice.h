#ifndef SOURCENAVIGATIONSERVICE_H
#define SOURCENAVIGATIONSERVICE_H

#include <QString>
#include <functional>
#include <memory>

struct IncludeDirectiveTarget {
    bool matched = false;
    QString includePath;
    int startColumn = -1;
    int endColumn = -1;
};

struct PackageImportTarget {
    bool matched = false;
    QString packageName;
    int startColumn = -1;
    int endColumn = -1;
};

struct SourceIdentifierTarget {
    bool matched = false;
    QString identifier;
    int startColumn = -1;
    int endColumn = -1;
};

struct SourceMemberAccessTarget {
    bool matched = false;
    QString accessPath;
    QString rootIdentifier;
    QString memberPath;
    int startColumn = -1;
    int endColumn = -1;
};

enum class SourceNavigationTargetKind {
    None,
    IncludeDirective,
    PackageImport,
    Identifier
};

struct SourceNavigationTarget {
    bool matched = false;
    SourceNavigationTargetKind kind = SourceNavigationTargetKind::None;
    QString text;
    int startColumn = -1;
    int endColumn = -1;
};

struct SourceSymbolActionContext {
    bool available = false;
    QString symbolName;
    QString memberAccessPath;
    QString memberAccessRootName;
    QString fileName;
    QString moduleName;
};

enum class SourceSymbolAction {
    FindReferences,
    ShowRelationships,
    ShowSignalKernelGraph,
    ShowStateTransitionGraph
};

struct SourceEditorNavigationTarget {
    bool matched = false;
    bool jumpable = false;
    bool includeTarget = false;
    bool identifierTarget = false;
    QString text;
    int startColumn = -1;
    int endColumn = -1;
    int cursorColumn = -1;
};

struct SourceLineNavigationTarget {
    bool matched = false;
    int lineNumber = -1;
    int columnNumber = -1;
    int lineMoves = 0;
    int columnMoves = 0;
};

class SourceNavigationService
{
public:
    static SourceNavigationService* getInstance();

    SourceNavigationService();
    ~SourceNavigationService();

    IncludeDirectiveTarget includeAtColumn(const QString& lineText,
                                           int column) const;
    PackageImportTarget packageImportAtColumn(const QString& lineText,
                                              int column) const;
    SourceIdentifierTarget identifierAtColumn(const QString& lineText,
                                              int column) const;
    SourceMemberAccessTarget memberAccessAtColumn(const QString& lineText,
                                                  int column) const;
    SourceNavigationTarget targetAtColumn(const QString& lineText,
                                          int column) const;
    SourceSymbolActionContext symbolActionContextAtColumn(
        const QString& lineText,
        int column,
        const QString& fileName,
        const QString& moduleName) const;
    SourceEditorNavigationTarget editorNavigationTargetAtColumn(
        const QString& lineText,
        int column,
        const std::function<bool(const QString&)>& canResolveIdentifier) const;
    SourceLineNavigationTarget lineNavigationTarget(
        int lineNumber,
        int columnNumber = -1) const;

private:
    static std::unique_ptr<SourceNavigationService> instance;

    static bool isIdentifierStart(QChar ch);
    static bool isIdentifierPart(QChar ch);
};

#endif // SOURCENAVIGATIONSERVICE_H
