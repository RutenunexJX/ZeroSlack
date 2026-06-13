#ifndef EDITORSOURCENAVIGATIONQUERY_H
#define EDITORSOURCENAVIGATIONQUERY_H

#include "editorsemanticcontextservice.h"

#include <functional>

class EditorSourceNavigationQuery
{
public:
    static SourceSymbolActionContext sourceSymbolActionContext(
        const EditorSemanticContext& context);
    static EditorSourceSymbolShortcutState sourceSymbolShortcutState(
        const EditorSourceSymbolShortcutContext& context);
    static EditorSourceSymbolContextMenuState sourceSymbolContextMenuState(
        const EditorSemanticContext& context);
    static EditorSourceSymbolActionRequestState sourceSymbolActionRequestState(
        SourceSymbolAction action,
        const EditorSemanticContext& context);
    static SourceEditorNavigationTarget sourceNavigationTarget(
        const EditorSemanticContext& context,
        const std::function<bool(const QString&)>& canResolveIdentifier);
    static SourceEditorNavigationTarget definitionSourceNavigationTarget(
        const EditorSemanticContext& context);
    static EditorSourceNavigationTarget editorSourceNavigationTarget(
        const EditorSemanticContext& context,
        int blockPosition);
    static EditorSourceNavigationClickState sourceNavigationClickState(
        const EditorSourceNavigationTarget& target);
    static SourceIdentifierTarget sourceIdentifierTarget(
        const EditorSemanticContext& context);
    static DefinitionNavigationQuery definitionNavigationQuery(
        const QString& symbolName,
        const EditorSemanticContext& context);
    static DefinitionNavigationTarget resolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context);
    static bool canResolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context);
    static QString definitionTooltipText(
        const QString& symbolName,
        const EditorSemanticContext& context);
};

#endif // EDITORSOURCENAVIGATIONQUERY_H
