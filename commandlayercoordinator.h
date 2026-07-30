#ifndef COMMANDLAYERCOORDINATOR_H
#define COMMANDLAYERCOORDINATOR_H

#include "actionregistry.h"
#include "commandlayercommandregistry.h"
#include "commandlayerservice.h"

#include <QFrame>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QEvent;
class QKeyEvent;
class ProjectModel;
class SemanticIndex;
class NavigationCommandCoordinator;
class MyCodeEditor;
class TabManager;
class QWidget;

class CommandLayerPanel;
class CommandLayerPickerPanel;
class ColumnNumberToolPanel;

class CommandLayerCoordinator : public QObject
{
public:
    explicit CommandLayerCoordinator(
        QWidget* anchor,
        TabManager* tabManager,
        ProjectModel* projectModel,
        SemanticIndex* semanticIndex,
        NavigationCommandCoordinator* navigation,
        QObject* parent = nullptr);
    ~CommandLayerCoordinator() override;

    void connectSignals();
    bool isActive() const;
    bool isF24Held() const;
    QString query() const;
    CommandLayerPanel* panelWidget() const;
    CommandLayerPickerPanel* pickerPanel() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Phase {
        Inactive,
        Search,
        Picker,
        Help
    };

    enum class PickerMode {
        Module,
        Package
    };

    QWidget* anchor = nullptr;
    TabManager* tabManager = nullptr;
    ProjectModel* projectModel = nullptr;
    SemanticIndex* semanticIndex = nullptr;
    NavigationCommandCoordinator* navigation = nullptr;
    std::unique_ptr<CommandLayerPanel> panel;
    std::unique_ptr<CommandLayerPickerPanel> picker;
    std::unique_ptr<ColumnNumberToolPanel> columnNumberTool;
    CommandLayerService service;
    Phase phase = Phase::Inactive;
    PickerMode activePickerMode = PickerMode::Module;
    QString activePickerCommand;
    QPointer<MyCodeEditor> activePickerEditor;
    QPointer<MyCodeEditor> lastEditor;
    QPointer<MyCodeEditor> columnNumberEditor;
    QString queryText;
    QString failureReason;
    QList<CommandLayerCommandMatch> matches;
    int selectedMatch = 0;
    bool f24Held = false;
    bool connected = false;
    bool applicationFilterInstalled = false;

    void installApplicationEventFilter();
    bool handleApplicationEvent(QObject* watched, QEvent* event);
    bool handleKeyPress(QKeyEvent* event);
    bool handleF24Event(QKeyEvent* event);
    bool handleSearchKey(QKeyEvent* event);
    bool handleHelpKey(QKeyEvent* event);
    void enterSearch(bool clearFailure = true);
    void leaveCommandLayer(bool restoreFocus = true);
    void updateSearchPanel();
    void showHelp();
    void moveSelection(int delta);
    void appendQueryCharacter(QKeyEvent* event);
    void executeSelectedCommand();
    void executeCommand(MyCodeEditor* editor,
                        const CommandLayerCommandMetadata& command);
    void completeCommand(const QString& failure = QString());
    void reportFailure(MyCodeEditor* editor, const QString& message);
    MyCodeEditor* currentEditorForLocalCommand() const;

    void handleRelativeLine(MyCodeEditor* editor, int moduleLine);
    void handleAddPort(MyCodeEditor* editor);
    void handleAddSignal(MyCodeEditor* editor);
    void handleAddParameter(MyCodeEditor* editor);
    void handleGoEndmodule(MyCodeEditor* editor);
    void handleClearRight(MyCodeEditor* editor);
    void handleSelectBeginEnd(MyCodeEditor* editor);
    void handleSelectSignals(MyCodeEditor* editor);

    void openColumnNumberToolForCurrentEditor();
    void handleColumnNumberTool(MyCodeEditor* editor);

    void showPicker(MyCodeEditor* editor,
                    PickerMode mode,
                    const QString& command);
    void refreshPicker(const QString& filter);
    void activatePickerItem(const CommandLayerPickerItem& item);
    void finishPicker();
    ProjectSnapshot projectSnapshot() const;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot() const;
};

class CommandLayerPanel : public QFrame
{
public:
    explicit CommandLayerPanel(QWidget* parent = nullptr);

    void showSearch(const QString& query,
                    const QList<CommandLayerCommandMatch>& matches,
                    int selectedIndex,
                    const QString& failureReason,
                    QWidget* anchor);
    void showHelp(
        const QList<ActionCatalogEntry>& entries,
        QWidget* anchor);
    QListWidget* candidateListWidget() const;
    QLabel* queryLabelWidget() const;
    QLabel* failureLabelWidget() const;

private:
    QLabel* titleLabel = nullptr;
    QLabel* queryLabel = nullptr;
    QListWidget* candidateList = nullptr;
    QLabel* failureLabel = nullptr;

    void positionFor(QWidget* anchor);
};

class CommandLayerPickerPanel : public QFrame
{
public:
    explicit CommandLayerPickerPanel(QWidget* parent = nullptr);

    void setItems(const QList<CommandLayerPickerItem>& items);
    void setPrompt(const QString& prompt);
    void setEmptyText(const QString& text);
    void showFor(QWidget* anchor);
    void focusSearch();
    void setFilterChangedHandler(std::function<void(const QString&)> handler);
    void setItemActivatedHandler(
        std::function<void(const CommandLayerPickerItem&)> handler);
    void setCancelledHandler(std::function<void()> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* searchEdit = nullptr;
    QListWidget* resultList = nullptr;
    QList<CommandLayerPickerItem> currentItems;
    QString emptyText;
    std::function<void(const QString&)> filterChangedHandler;
    std::function<void(const CommandLayerPickerItem&)> itemActivatedHandler;
    std::function<void()> cancelledHandler;

    void activateCurrentItem();
    void cancel();
    void moveSelection(int delta);
    bool handleKey(QKeyEvent* event);
};

#endif // COMMANDLAYERCOORDINATOR_H
