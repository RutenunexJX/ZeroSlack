QT       += core gui concurrent

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
    sv_lexer.cpp \
    sv_symbol_parser.cpp \
    sv_treesitter_parser.cpp \
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
    scope_tree.h \
    completionmodel.h \
    sv_lexer.h \
    sv_symbol_parser.h \
    sv_treesitter_parser.h \
    sv_token.h \
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

INCLUDEPATH += $$PWD/thirdparty/tree_sitter/lib/include

INCLUDEPATH += $$PWD/thirdparty/tree_sitter/lib/src
SOURCES += $$PWD/thirdparty/tree_sitter/lib/src/lib.c

SOURCES += \
    $$PWD/thirdparty/tree_sitter_systemverilog/src/parser.c
	
QMAKE_CFLAGS += -std=c99 -Wno-unused-parameter

win32-g++: DEFINES += __USE_MINGW_ANSI_STDIO=1
