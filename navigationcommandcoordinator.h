#ifndef NAVIGATIONCOMMANDCOORDINATOR_H
#define NAVIGATIONCOMMANDCOORDINATOR_H

#include "syminfo.h"

#include <QObject>
#include <QString>

class NavigationManager;
class MyCodeEditor;
class TabManager;

class NavigationCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit NavigationCommandCoordinator(TabManager* tabManager,
                                          NavigationManager* navigationManager,
                                          QObject* parent = nullptr);

    void connectSignals();
    void navigateToFileAndLine(const QString& filePath,
                               int lineNumber = -1,
                               int columnNumber = -1);
    void navigateEditorToLine(MyCodeEditor* editor,
                              int lineNumber,
                              int columnNumber = -1);
    void navigateToSymbol(const sym_list::SymbolInfo& symbol);

private:
    TabManager* tabManager = nullptr;
    NavigationManager* navigationManager = nullptr;
    bool signalsConnected = false;
};

#endif // NAVIGATIONCOMMANDCOORDINATOR_H
