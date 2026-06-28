#ifndef COMMODECOORDINATOR_H
#define COMMODECOORDINATOR_H

#include "commodeservice.h"

#include <QFrame>
#include <QObject>
#include <QSet>
#include <functional>
#include <memory>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QEvent;
class QStatusBar;
class QKeyEvent;
class QShortcut;
class ProjectModel;
class SemanticIndex;
class NavigationCommandCoordinator;
class MyCodeEditor;
class TabManager;
class QWidget;

class ComModuleSelectorPanel;
class ColumnNumberToolPanel;

class ComModeCoordinator : public QObject
{
public:
    explicit ComModeCoordinator(QStatusBar* statusBar,
                                QWidget* anchor,
                                TabManager* tabManager,
                                ProjectModel* projectModel,
                                SemanticIndex* semanticIndex,
                                NavigationCommandCoordinator* navigation,
                                QObject* parent = nullptr);
    ~ComModeCoordinator() override;

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);
    QLabel* commandStripWidget() const;
    ComModuleSelectorPanel* moduleSelectorPanel() const;
    ColumnNumberToolPanel* columnNumberToolPanel() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class PickerMode {
        Module,
        Package,
        Parameter,
        Signal
    };

    QStatusBar* statusBar = nullptr;
    QWidget* anchor = nullptr;
    TabManager* tabManager = nullptr;
    ProjectModel* projectModel = nullptr;
    SemanticIndex* semanticIndex = nullptr;
    NavigationCommandCoordinator* navigation = nullptr;
    QLabel* commandStrip = nullptr;
    std::unique_ptr<ComModuleSelectorPanel> moduleSelector;
    std::unique_ptr<ColumnNumberToolPanel> columnNumberTool;
    ComModeService service;
    QSet<MyCodeEditor*> attachedEditors;
    MyCodeEditor* activeComEditor = nullptr;
    PickerMode activePickerMode = PickerMode::Module;
    QString activePickerCommand = QStringLiteral("gm");
    bool connected = false;
    bool globalEscapeInstalled = false;
    bool forwardingEscapeToEditor = false;
    QShortcut* comToggleShortcut = nullptr;

    void ensureCommandStrip();
    void installGlobalEscapeFilter();
    bool handleGlobalEscape(QObject* watched, QEvent* event);
    void toggleCurrentEditorComMode();
    void updateCommandStrip(MyCodeEditor* editor,
                            bool active,
                            const QString& buffer,
                            const QString& message);
    void hideCommandStrip();
    void handleCommand(MyCodeEditor* editor, const QString& command);
    void handleRelativeLine(MyCodeEditor* editor, int moduleLine);
    void handlePortAppend(MyCodeEditor* editor);
    void handleSignalInsert(MyCodeEditor* editor);
    void handleInstanceInsert(MyCodeEditor* editor);
    void handleAssignInsert(MyCodeEditor* editor);
    void handleClearAssignmentRhs(MyCodeEditor* editor);
    void handleSelectInsideBeginEnd(MyCodeEditor* editor);
    void handleColumnNumberTool(MyCodeEditor* editor);
    void handleParameterInsert(MyCodeEditor* editor);
    void handleModuleEndInsert(MyCodeEditor* editor);
    void showPicker(MyCodeEditor* editor,
                    PickerMode mode,
                    const QString& command);
    bool refreshPicker(const QString& filter);
    void activatePickerItem(const ComModePickerItem& item);
    void exitComModeFromPicker();
    ProjectSnapshot projectSnapshot() const;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot() const;
};

class ComModuleSelectorPanel : public QFrame
{
public:
    explicit ComModuleSelectorPanel(QWidget* parent = nullptr);

    void setItems(const QList<ComModePickerItem>& items);
    void setPrompt(const QString& prompt);
    void setEmptyText(const QString& text);
    void showFor(QWidget* anchor);
    void focusSearch();
    QString filterText() const;
    void setFilterChangedHandler(std::function<void(const QString&)> handler);
    void setItemActivatedHandler(
        std::function<void(const ComModePickerItem&)> handler);
    void setCancelledHandler(std::function<void()> handler);
    void setExitRequestedHandler(std::function<void()> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* searchEdit = nullptr;
    QListWidget* resultList = nullptr;
    QList<ComModePickerItem> currentItems;
    QString emptyText;
    std::function<void(const QString&)> filterChangedHandler;
    std::function<void(const ComModePickerItem&)> itemActivatedHandler;
    std::function<void()> cancelledHandler;
    std::function<void()> exitRequestedHandler;

    void activateCurrentItem();
    void cancel();
    void moveSelection(int delta);
    bool handleKey(QKeyEvent* event);
};

#endif // COMMODECOORDINATOR_H
