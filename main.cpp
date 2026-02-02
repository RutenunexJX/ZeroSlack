#include "mainwindow.h"
#include "symbolrelationshipengine.h"
#include "sv_treesitter_parser.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    // === SVTreeSitterParser 集成验证 ===
    {
        SVTreeSitterParser parser;
        QString code = QStringLiteral("module test(input clk); endmodule");
        parser.parse(code);
        QList<sym_list::SymbolInfo> symbols = parser.getSymbols();
        qDebug() << "SVTreeSitterParser: got" << symbols.size() << "symbol(s)";
        if (!symbols.isEmpty()) {
            const sym_list::SymbolInfo &first = symbols.first();
            qDebug() << "  first: name=" << first.symbolName << "type=" << first.symbolType
                     << "line=" << first.startLine << "col=" << first.startColumn;
        }
    }
    // === 集成验证结束 ===


    QApplication a(argc, argv);

    // 注册 RelationType，避免跨线程/队列传递 relationshipAdded 信号时出现
    // "Cannot queue arguments of type 'RelationType'" 并导致卡顿或崩溃
    qRegisterMetaType<SymbolRelationshipEngine::RelationType>("SymbolRelationshipEngine::RelationType");

    MainWindow w;
    w.show();
    return a.exec();
}
