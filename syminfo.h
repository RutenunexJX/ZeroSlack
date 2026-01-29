#ifndef SYMINFO_H
#define SYMINFO_H

#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <memory>
#include <QDateTime>
#include <QReadWriteLock>

class MainWindow;
class MyCodeEditor;
class SymbolRelationshipEngine;

class sym_list{
public:
    sym_list();
    ~sym_list();

    void setCodeEditor(MyCodeEditor* codeEditor);

    // UPDATED: Smart pointer singleton pattern
    static sym_list* getInstance();

    enum sym_type_e{
        sym_reg,
        sym_wire,
        sym_logic,

        sym_interface,
        sym_interface_assco_struct,
        sym_interface_parameter,
        sym_interface_modport,

        sym_enum,                    // 枚举类型定义
        sym_enum_var,               // 枚举变量
        sym_enum_value,             // 枚举值

        // Enhanced struct support
        sym_packed_struct,          // packed struct类型
        sym_unpacked_struct,        // unpacked struct类型
        sym_packed_struct_var,      // packed struct变量
        sym_unpacked_struct_var,    // unpacked struct变量
        sym_struct_member,          // 结构体成员

        sym_typedef,                // typedef定义

        sym_generate_if,
        sym_generate_for,
        sym_generate_case,

        sym_always,
        sym_always_ff,
        sym_always_comb,
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

        sym_package
    };

    // 🚀 UPDATED: 简化的符号信息结构
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

        // 🚀 NEW: 简化的索引系统
        int symbolId;              // 全局唯一ID (替代symbolAbsoluteIndex)

        // 🚀 REMOVED: 复杂的关系字段全部移除，由SymbolRelationshipEngine管理
        // 删除: int symbolAbsoluteIndex;
        // 删除: QList<int> bidirIndexTable;

        // 🚀 NEW: 可选的快速访问字段(由关系引擎同步维护)
        QString moduleScope;       // 所属模块名称(用于快速过滤和显示)
        int scopeLevel = 0;        // 作用域层级(0=全局, 1=模块内, 2=块内等)
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
    QList<SymbolInfo> findSymbolsByType(sym_type_e symbolType);
    QList<SymbolInfo> getAllSymbols();
    void clearSymbolsForFile(const QString& fileName);

    SymbolInfo getSymbolById(int symbolId) const;
    bool hasSymbol(int symbolId) const;

    QStringList getSymbolNamesByType(sym_type_e symbolType);
    QSet<QString> getUniqueSymbolNames();
    int getSymbolCountByType(sym_type_e symbolType);

    SymbolRelationshipEngine* getRelationshipEngine() const;
    void setRelationshipEngine(SymbolRelationshipEngine* engine);

    QList<CommentRegion> commentRegions;

    bool isPositionInComment(int position);
    bool isPositionInMultiLineComment(int pos);
    QList<CommentRegion> getCommentRegions() const;
    QList<RegexMatch> findMatchesOutsideComments(const QString &text, const QRegExp &pattern);
    void findVariableDeclarations();

    void setCodeEditorIncremental(MyCodeEditor* codeEditor);
    bool needsAnalysis(const QString& fileName, const QString& content);
    
    // 查找模块的结束行号
    int findEndModuleLine(const QString &fileName, const SymbolInfo &moduleSymbol);
    void refreshStructTypedefEnumForFile(const QString &fileName, const QString &content);

private:
    mutable QReadWriteLock symbolDbLock;  // 供后台线程只读访问 findSymbolsByName/getSymbolById 等
    // Central symbol storage
    QList<SymbolInfo> symbolDatabase;

    QHash<sym_type_e, QList<int>> symbolTypeIndex;        // 类型 -> 数据库索引列表
    QHash<QString, QList<int>> symbolNameIndex;          // 名称 -> 数据库索引列表
    QHash<QString, QList<int>> fileNameIndex;            // 文件名 -> 数据库索引列表
    QHash<int, int> symbolIdToIndex;                     // 🚀 NEW: symbolId -> 数据库索引映射

    mutable QHash<sym_type_e, QStringList> cachedSymbolNamesByType;
    mutable QSet<QString> cachedUniqueNames;
    mutable bool indexesDirty = false;

    int nextSymbolId = 1;
    int allocateSymbolId();

    SymbolRelationshipEngine* relationshipEngine = nullptr;

    static std::unique_ptr<sym_list> instance;

    void getModuleName(const QString &text);
    void buildCommentRegions(const QString &text);
    void findSingleLineComments(const QString &text);
    void findMultiLineComments(const QString &text);
    void calculateLineColumn(const QString &text, int position, int &line, int &column);
    bool isMatchInComment(int matchStart, int matchLength);

    QString currentFileName;

    void getVariableDeclarations(const QString &text);
    void getTasksAndFunctions(const QString &text);

    // File state tracking
    struct FileState {
        QString contentHash;
        QDateTime lastModified;
        bool needsFullAnalysis = true;
    };
    QHash<QString, FileState> fileStates;

    // Line-based symbol mapping
    QHash<QString, QHash<int, QList<SymbolInfo>>> lineBasedSymbols; // fileName -> line -> symbols

    QString calculateContentHash(const QString& content);
    QList<int> detectChangedLines(const QString& fileName, const QString& newContent);
    void clearSymbolsForLines(const QString& fileName, const QList<int>& lines);
    void analyzeSpecificLines(const QString& fileName, const QString& content, const QList<int>& lines);
    void updateLineBasedSymbols(const SymbolInfo& symbol);

    // Cache file content for line-level comparison
    QHash<QString, QString> previousFileContents;

    // Line-level analysis helper methods
    void analyzeModulesInLine(const QString& lineText, int lineStartPos, int lineNum);
    void analyzeVariablesInLine(const QString& lineText, int lineStartPos, int lineNum, const QString& fullText = QString());
    void analyzeTasksFunctionsInLine(const QString& lineText, int lineStartPos, int lineNum);
    void analyzeVariablePattern(const QString& lineText, int lineStartPos, int lineNum,
                                const QRegExp& pattern, sym_type_e symbolType);
    void analyzeTaskFunctionPattern(const QString& lineText, int lineStartPos, int lineNum,
                                    const QRegExp& pattern, sym_type_e symbolType);

    void rebuildAllIndexes();
    void addToIndexes(int symbolIndex);
    void removeFromIndexes(int symbolIndex);
    void invalidateCache();
    void updateCachedData() const;

    void rebuildAllRelationships();
    void buildSymbolRelationships(const QString& fileName);
    void analyzeModuleContainment(const QString& fileName);
    void analyzeVariableReferences(const QString& fileName, const QString& content);

    void analyzeInterfaces(const QString &text);
    void analyzeDataTypes(const QString &text);
    void analyzePackages(const QString &text);
    void analyzePreprocessorDirectives(const QString &text);
    void analyzeAlwaysAndAssign(const QString &text);
    void analyzeParameters(const QString &text);
    void analyzeConstraints(const QString &text);
    void getAdditionalSymbols(const QString &text);
    QString getCurrentModuleScope(const QString &fileName, int lineNumber);
    void analyzeStructVariables(const QString &text);
    void analyzeStructMembers(const QString &membersText, const QString &structName, int basePosition, const QString &fullText);
    void analyzeEnumsAndStructs(const QString &text);
    void clearStructTypedefEnumSymbolsForFile(const QString &fileName);

    // 辅助结构：存储struct的范围
    struct StructRange {
        int startPos;  // struct开始位置（'{'的位置）
        int endPos;    // struct结束位置（'}'的位置）
    };
    
    // 查找所有struct的范围（包括packed和unpacked）
    QList<StructRange> findStructRanges(const QString &text);
    
    // 检查位置是否在struct范围内
    bool isPositionInStructRange(int position, const QList<StructRange> &structRanges);
};

// 🚀 NEW: 符号关系工具函数
bool isSymbolInModule(const sym_list::SymbolInfo& symbol, const sym_list::SymbolInfo& module);
QString getModuleNameContainingSymbol(const sym_list::SymbolInfo& symbol, const QList<sym_list::SymbolInfo>& allSymbols);

#endif // SYMINFO_H
