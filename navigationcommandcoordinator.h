#ifndef NAVIGATIONCOMMANDCOORDINATOR_H
#define NAVIGATIONCOMMANDCOORDINATOR_H

#include "symboloutlinemodel.h"

#include <QObject>
#include <QString>
#include <QVector>

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
    void revealFileAndFlashLine(const QString& filePath,
                                int lineNumber);
    void navigateEditorToLine(MyCodeEditor* editor,
                              int lineNumber,
                              int columnNumber = -1);
    void navigateToSymbol(const SymbolOutlineSymbolRow& row);
    void navigateBack();
    void navigateForward();

private:
    struct NavigationLocation {
        QString filePath;
        int lineNumber = -1;
        int columnNumber = -1;

        bool isValid() const;
        bool operator==(const NavigationLocation& other) const;
    };

    struct NavigationTargets {
        TabManager* tabManager = nullptr;
        NavigationManager* navigationManager = nullptr;

        void set(TabManager* tabManager,
                 NavigationManager* navigationManager);
        bool hasNavigationManager() const;
        NavigationManager* navigationManagerObject() const;
        bool activateOrOpenFile(const QString& filePath) const;
        MyCodeEditor* currentEditor() const;
        NavigationLocation currentLocation() const;
    };

    struct LineNavigationResolver {
        bool applyToEditor(MyCodeEditor* editor,
                           int lineNumber,
                           int columnNumber) const;
    };

    NavigationTargets targets;
    LineNavigationResolver lineResolver;
    QVector<NavigationLocation> backStack;
    QVector<NavigationLocation> forwardStack;
    bool replayingHistory = false;
    bool signalsConnected = false;

    void recordCurrentLocationBeforeNavigation(
        const NavigationLocation& destination);
    bool applyLocation(const NavigationLocation& location);
};

#endif // NAVIGATIONCOMMANDCOORDINATOR_H
