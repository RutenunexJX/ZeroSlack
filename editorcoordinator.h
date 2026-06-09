#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QString>

#include <functional>

class ModeManager;
class MyCodeEditor;
class TabManager;

class EditorCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit EditorCoordinator(TabManager* tabManager,
                               ModeManager* modeManager,
                               QObject* parent = nullptr);

    void setIncludePathResolver(
        std::function<QString(const QString&, const QString&)> resolver);
    void setFileOpenHandler(std::function<bool(const QString&)> handler);
    void setDefinitionNavigationHandler(std::function<void(const QString&, int)> handler);
    void setRelationshipAnalysisHandler(
        std::function<void(const QString&, const QString&)> handler);
    void setSaveFileHandler(std::function<void()> handler);
    void setSaveFileAsHandler(std::function<void()> handler);
    void setOpenFileHandler(std::function<void()> handler);
    void setNewFileHandler(std::function<void()> handler);
    void setReferenceSearchHandler(
        std::function<void(const QString&, const QString&, const QString&)> handler);
    void setRelationshipBrowseHandler(
        std::function<void(const QString&, const QString&, const QString&)> handler);
    void setActiveEditorChangedHandler(std::function<void(MyCodeEditor*)> handler);

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);

private:
    void applyAlternateMode(MyCodeEditor* editor) const;
    void applyAlternateModeToOpenEditors() const;
    void handleActiveEditorChanged(MyCodeEditor* editor);

    TabManager* tabManager = nullptr;
    ModeManager* modeManager = nullptr;
    bool signalsConnected = false;

    std::function<QString(const QString&, const QString&)> includePathResolver;
    std::function<bool(const QString&)> fileOpenHandler;
    std::function<void(const QString&, int)> definitionNavigationHandler;
    std::function<void(const QString&, const QString&)> relationshipAnalysisHandler;
    std::function<void()> saveFileHandler;
    std::function<void()> saveFileAsHandler;
    std::function<void()> openFileHandler;
    std::function<void()> newFileHandler;
    std::function<void(const QString&, const QString&, const QString&)> referenceSearchHandler;
    std::function<void(const QString&, const QString&, const QString&)> relationshipBrowseHandler;
    std::function<void(MyCodeEditor*)> activeEditorChangedHandler;
};

#endif // EDITORCOORDINATOR_H
