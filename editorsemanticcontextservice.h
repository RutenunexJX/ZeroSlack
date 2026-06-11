#ifndef EDITORSEMANTICCONTEXTSERVICE_H
#define EDITORSEMANTICCONTEXTSERVICE_H

#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "sourcenavigationservice.h"

#include <QString>
#include <memory>
#include <functional>

struct EditorSemanticContext {
    QString fileName;
    QString moduleName;
    QString documentText;
    QString lineText;
    QString lineUpToCursor;
    QString wordPrefix;
    int cursorLine = -1;
    int cursorPosition = -1;
    int column = -1;
    bool commandModeActive = false;
};

class EditorSemanticContextService
{
public:
    static EditorSemanticContextService* getInstance();

    EditorSemanticContextService();
    ~EditorSemanticContextService();

    SourceSymbolActionContext sourceSymbolActionContext(
        const EditorSemanticContext& context) const;
    SourceEditorNavigationTarget sourceNavigationTarget(
        const EditorSemanticContext& context,
        const std::function<bool(const QString&)>& canResolveIdentifier) const;
    SourceIdentifierTarget sourceIdentifierTarget(
        const EditorSemanticContext& context) const;
    DefinitionNavigationQuery definitionNavigationQuery(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    DefinitionNavigationTarget resolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    bool canResolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    QString definitionTooltipText(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    CompletionTriggerQuery completionTriggerQuery(
        const EditorSemanticContext& context) const;
    CompletionQuery completionQuery(const QString& prefix,
                                    const EditorSemanticContext& context) const;
    CommandModeCompletionQuery commandModeCompletionQuery(
        const EditorSemanticContext& context) const;
    EditorCompletionQuery editorCompletionQuery(
        const EditorSemanticContext& context) const;

private:
    static std::unique_ptr<EditorSemanticContextService> instance;
};

#endif // EDITORSEMANTICCONTEXTSERVICE_H
