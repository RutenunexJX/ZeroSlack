#include "mainwindow.h"
#include "symbolrelationshipengine.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/images/zeroslack_app.png")));

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
    w.show();
    return a.exec();
}
