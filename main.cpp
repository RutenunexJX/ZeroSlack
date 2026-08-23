#include "mainwindow.h"
#include "symbolrelationshipengine.h"
#include "version.h"

#ifdef ZEROSLACK_HAS_SUITEAPP
#include "suiteappintegration.h"
#endif

#include <QApplication>
#include <QIcon>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));
    a.setWindowIcon(QIcon(QStringLiteral(":/images/zeroslack_app.png")));

    qRegisterMetaType<SymbolRelationshipEngine::RelationType>();

    MainWindow w;
    w.show();
#ifdef ZEROSLACK_HAS_SUITEAPP
    ZeroSlackSuiteIntegration suiteIntegration(&w);
    QTimer::singleShot(0, &a, [&suiteIntegration]() {
        suiteIntegration.start();
    });
#endif
    return a.exec();
}
