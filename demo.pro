QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

#CONFIG += c++11
CONFIG += c++14

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    completionmanager.cpp \
    completionmodel.cpp \
    main.cpp \
    mainwindow.cpp \
    modemanager.cpp \
    mycodeeditor.cpp \
    myhighlighter.cpp \
    navigationmanager.cpp \
    navigationwidget.cpp \
    relationshipprogressdialog.cpp \
    smartrelationshipbuilder.cpp \
    symbolanalyzer.cpp \
    symbolrelationshipengine.cpp \
    syminfo.cpp \
    tabmanager.cpp \
    workspacemanager.cpp

HEADERS += \
    completionmanager.h \
    completionmodel.h \
    mainwindow.h \
    modemanager.h \
    mycodeeditor.h \
    myhighlighter.h \
    navigationmanager.h \
    navigationwidget.h \
    relationshipprogressdialog.h \
    smartrelationshipbuilder.h \
    symbolanalyzer.h \
    symbolrelationshipengine.h \
    syminfo.h \
    tabmanager.h \
    workspacemanager.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    code.qrc \
    images.qrc

DISTFILES += \
    readme.txt
