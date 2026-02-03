#include "mainwindow.h"
#include "symbolrelationshipengine.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 注册 RelationType，避免跨线程/队列传递 relationshipAdded 信号时出现
    // "Cannot queue arguments of type 'RelationType'" 并导致卡顿或崩溃
    qRegisterMetaType<SymbolRelationshipEngine::RelationType>("SymbolRelationshipEngine::RelationType");

    MainWindow w;
    w.show();
    return a.exec();
}
