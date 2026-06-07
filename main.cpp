#include "mainwindow.h"
#include "symbolrelationshipengine.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
    w.show();
    return a.exec();
}
