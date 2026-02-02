#include "mainwindow.h"
#include "symbolrelationshipengine.h"

#include <QApplication>

extern "C" {
#include <tree_sitter/api.h>
}
extern "C" TSLanguage *tree_sitter_systemverilog();

int main(int argc, char *argv[])
{
    // === Tree-sitter 测试开始 ===
    qDebug() << "Initializing Tree-sitter...";
    // 1. 创建解析器
    TSParser *parser = ts_parser_new();

    // 2. 设置语言
    TSLanguage *lang = tree_sitter_systemverilog();
    bool success = ts_parser_set_language(parser, lang);

    if (success) {
        qDebug() << "Tree-sitter language set successfully!";

        // 3. 解析一段简单的代码
        const char *source_code = "module test(input clk); endmodule";
        TSTree *tree = ts_parser_parse_string(
            parser,
            NULL,
            source_code,
            strlen(source_code)
        );

        // 4. 获取根节点并打印类型，验证解析是否工作
        TSNode root_node = ts_tree_root_node(tree);
        const char *type = ts_node_type(root_node);
        qDebug() << "Root node type:" << type; // 应该输出 "source_file" 或类似内容

        // 5. 清理
        ts_tree_delete(tree);
    } else {
        qCritical() << "Failed to set SystemVerilog language!";
    }

    ts_parser_delete(parser);
    // === Tree-sitter 测试结束 ===


    QApplication a(argc, argv);

    // 注册 RelationType，避免跨线程/队列传递 relationshipAdded 信号时出现
    // "Cannot queue arguments of type 'RelationType'" 并导致卡顿或崩溃
    qRegisterMetaType<SymbolRelationshipEngine::RelationType>("SymbolRelationshipEngine::RelationType");

    MainWindow w;
    w.show();
    return a.exec();
}
