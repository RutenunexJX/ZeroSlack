#include "mainwindow.h"
#include "symbolrelationshipengine.h"

#include <QApplication>
#include <QDebug>
#include <slang/syntax/SyntaxTree.h>

static void runSlangSanityCheck() {
    qDebug() << "========================================";
    qDebug() << "   Starting Slang Integration Check";
    qDebug() << "========================================";

    try {
        // 1. 尝试解析一段简单的 SystemVerilog 代码
        auto tree = slang::syntax::SyntaxTree::fromText("module SlangVerification; endmodule");

        // 2. 获取根节点并打印类型
        // toString 返回的是 std::string_view，需转换为 QString 或 std::string 输出
        auto rootKind = tree->root().kind;
        QString kindName = QString::fromStdString(std::string(slang::syntax::toString(rootKind)));

        qDebug() << "Slang parsed successfully!";
        qDebug() << "Root Node Kind:" << kindName; // 预期输出 CompilationUnit

        // 3. 简单的诊断检查 (预期无错误)
        if (tree->diagnostics().empty()) {
            qDebug() << "Diagnostics: None (OK)";
        } else {
            qDebug() << "Diagnostics found:" << tree->diagnostics().size();
        }

    } catch (const std::exception& e) {
        qDebug() << "CRITICAL: Slang threw an exception:" << e.what();
    } catch (...) {
        qDebug() << "CRITICAL: Slang threw an unknown exception.";
    }

    qDebug() << "========================================";
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // --- Slang 验证 ---
    runSlangSanityCheck();
    // ------------------

    // 注册 RelationType，避免跨线程/队列传递 relationshipAdded 信号时出现
    // "Cannot queue arguments of type 'RelationType'" 并导致卡顿或崩溃
    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
    w.show();
    return a.exec();
}
