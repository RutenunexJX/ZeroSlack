#ifndef SYMINFO_H
#define SYMINFO_H

#include "symbolsemanticmetadata.h"

#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <QVector>
#include <memory>
#include <QDateTime>
#include <QReadWriteLock>
#include <QRegularExpression>

class MainWindow;
class ScopeManager;
class SymbolRelationshipEngine;

class sym_list{
public:
    sym_list();
    ~sym_list();

    static sym_list* getInstance();

    enum sym_type_e{
        sym_reg,
        sym_wire,
        sym_logic,

        sym_interface,
        sym_interface_assco_struct,
        sym_interface_parameter,
        sym_interface_modport,

        sym_enum,
        sym_enum_var,
        sym_enum_value,

        sym_packed_struct,
        sym_unpacked_struct,
        sym_packed_struct_var,
        sym_unpacked_struct_var,
        sym_struct_member,

        sym_typedef,

        sym_generate_if,
        sym_generate_for,
        sym_generate_case,

        sym_always,
        sym_always_ff,
        sym_always_comb,
        sym_always_latch,
        sym_assign,

        sym_def_ifdef,
        sym_def_ifndef,
        sym_def_else,
        sym_def_elsif,
        sym_def_endif,
        sym_def_define,
        sym_def_parameter,

        sym_case,
        sym_casex,
        sym_casez,
        sym_endcase,
        sym_case_default,
        sym_fsm_state,

        sym_initial,
        sym_task,
        sym_function,

        sym_xilinx_constraint,
        sym_user,
        sym_localparam,
        sym_parameter,

        sym_module,
        sym_module_parameter,
        sym_inst,
        sym_inst_pin,

        sym_port_input,
        sym_port_output,
        sym_port_inout,
        sym_port_ref,
        sym_port_interface,
        sym_port_interface_modport,

        sym_package
    };

    struct SymbolInfo {
        QString fileName;
        QString symbolName;
        sym_type_e symbolType;
        int startLine;
        int startColumn;
        int endLine;
        int endColumn;
        int position;
        int length;

        int symbolId;
        QString moduleScope;
        int scopeLevel = 0;
        QString dataType;

        bool hasSemanticMetadata = false;
        SymbolSemanticMetadata::DeclarationKind semanticDeclarationKind =
            SymbolSemanticMetadata::DeclarationKind::Unknown;
        SymbolSemanticMetadata::SymbolUsageRole semanticUsageRole =
            SymbolSemanticMetadata::SymbolUsageRole::Unknown;
        SymbolSemanticMetadata::SymbolOwnerScope semanticOwnerScope =
            SymbolSemanticMetadata::SymbolOwnerScope::Unknown;
        SymbolSemanticMetadata::SymbolVisibility semanticVisibility =
            SymbolSemanticMetadata::SymbolVisibility::Unknown;
        SymbolSemanticMetadata::SourceRole semanticSourceRole =
            SymbolSemanticMetadata::SourceRole::Unknown;
        sym_type_e rawCollectorKind = sym_user;
        bool interfaceLikeOwner = false;
    };

    struct RegexMatch {
        sym_type_e sym_type;
        int position;
        int length;
        QString captured;
        int lineNumber;
        int columnNumber;
    };

    struct CommentRegion {
        int startPos;
        int endPos;
        int startLine;
        int startColumn;
        int endLine;
        int endColumn;
    };

    void addSymbol(const SymbolInfo& symbol);
    QList<SymbolInfo> findSymbolsByFileName(const QString& fileName);
    QList<SymbolInfo> findSymbolsByName(const QString& symbolName);
    int findSymbolIdByName(const QString& symbolName) const;
    QList<SymbolInfo> findSymbolsByType(sym_type_e symbolType);
    QList<SymbolInfo> getAllSymbols();
    void clearSymbolsForFile(const QString& fileName);

    /** Replace all symbols for a file with the given list (e.g. from Slang), then rebuild scope tree and CONTAINS relationships. */
    void setSymbolsForFile(const QString& fileName, const QList<SymbolInfo>& symbols);
    /** Same as above; if content is non-empty, updates fileStates (contentHash, symbolRelevantHash, lastAnalyzedLineCount) for needsAnalysis. */
    void setSymbolsForFile(const QString& fileName, const QList<SymbolInfo>& symbols, const QString& content);

    bool hasSymbol(int symbolId) const;

    QStringList getSymbolNamesByType(sym_type_e symbolType);
    QSet<QString> getUniqueSymbolNames();
    int getSymbolCountByType(sym_type_e symbolType);

    SymbolRelationshipEngine* getRelationshipEngine() const;
    void setRelationshipEngine(SymbolRelationshipEngine* engine);

    ScopeManager* getScopeManager() const;

    QString getCurrentModuleScope(const QString& fileName, int lineNumber);

    QString getCachedFileContent(const QString& fileName) const;

    static bool isValidModuleName(const QString& name);

    QList<CommentRegion> commentRegions;

    bool isPositionInComment(int position);
    bool isPositionInMultiLineComment(int pos);
    QList<CommentRegion> getCommentRegions() const;

    bool needsAnalysis(const QString& fileName, const QString& content);

    bool contentAffectsSymbols(const QString& fileName, const QString& content);

    int findEndModuleLine(const QString &fileName, const SymbolInfo &moduleSymbol);
    void refreshStructTypedefEnumForFile(const QString &fileName, const QString &content);

private:
    mutable QReadWriteLock symbolDbLock;
    QList<SymbolInfo> symbolDatabase;

    QHash<sym_type_e, QList<int>> symbolTypeIndex;
    QHash<QString, QList<int>> symbolNameIndex;
    QHash<QString, QList<int>> fileNameIndex;
    QHash<int, int> symbolIdToIndex;

    mutable QHash<sym_type_e, QStringList> cachedSymbolNamesByType;
    mutable QSet<QString> cachedUniqueNames;
    mutable bool indexesDirty = false;

    int nextSymbolId = 1;
    int allocateSymbolId();

    SymbolRelationshipEngine* relationshipEngine = nullptr;
    mutable ScopeManager* m_scopeManager = nullptr;

    static std::unique_ptr<sym_list> instance;

    void calculateLineColumn(const QString &text, int position, int &line, int &column);
    bool isMatchInComment(int matchStart, int matchLength);

    QString currentFileName;

    struct FileState {
        QString contentHash;
        QString symbolRelevantHash;
        QDateTime lastModified;
        bool needsFullAnalysis = true;
        int lastAnalyzedLineCount = 0;
    };
    QHash<QString, FileState> fileStates;

    QHash<QString, QHash<int, QList<SymbolInfo>>> lineBasedSymbols;

    QString calculateContentHash(const QString& content);
    QString calculateSymbolRelevantHash(const QString& content);
    QList<int> detectChangedLines(const QString& fileName, const QString& newContent);
    void clearSymbolsForLines(const QString& fileName, const QList<int>& lines);
    void updateLineBasedSymbols(const SymbolInfo& symbol);

    QHash<QString, QString> previousFileContents;

    void rebuildAllIndexes();
    void addToIndexes(int symbolIndex);
    void removeFromIndexes(int symbolIndex);
    void invalidateCache();
    void updateCachedData() const;

    void rebuildAllRelationships();
    void buildSymbolRelationships(const QString& fileName);
    void analyzeModuleContainment(const QString& fileName);
    /** Rebuild ScopeManager tree and CONTAINS relationships for fileName from current symbolDatabase entries. */
    void rebuildScopeAndRelationshipsForFile(const QString& fileName);
    void analyzeVariableReferences(const QString& fileName, const QString& content);

    void clearStructTypedefEnumSymbolsForFile(const QString &fileName);

};

bool isSymbolInModule(const sym_list::SymbolInfo& symbol, const sym_list::SymbolInfo& module);
QString getModuleNameContainingSymbol(const sym_list::SymbolInfo& symbol, const QList<sym_list::SymbolInfo>& allSymbols);

#endif // SYMINFO_H
