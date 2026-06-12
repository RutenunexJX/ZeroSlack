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
    struct NavigationTargets {
        TabManager* tabManager = nullptr;
        NavigationManager* navigationManager = nullptr;

        void set(TabManager* tabManager,
                 NavigationManager* navigationManager);
        bool hasNavigationManager() const;
        NavigationManager* navigationManagerObject() const;
        bool activateOrOpenFile(const QString& filePath) const;
        MyCodeEditor* currentEditor() const;
    };

    struct LineNavigationResolver {
        bool applyToEditor(MyCodeEditor* editor,
                           int lineNumber,
                           int columnNumber) const;
    };

    NavigationTargets targets;
    LineNavigationResolver lineResolver;
    bool signalsConnected = false;
};

#endif // NAVIGATIONCOMMANDCOORDINATOR_H
