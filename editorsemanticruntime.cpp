#include "editorsemanticruntime.h"

#include "editorsemanticcontextservice.h"

#include <QTextBlock>
#include <QTextDocument>

void EditorSemanticRuntime::init()
{
    service = EditorSemanticContextService::getInstance();
}

void EditorSemanticRuntime::setService(
    EditorSemanticContextService* nextService)
{
    service = nextService
        ? nextService
        : EditorSemanticContextService::getInstance();
}

EditorSemanticContextService* EditorSemanticRuntime::contextService() const
{
    return service
        ? service
        : EditorSemanticContextService::getInstance();
}

EditorSemanticContext EditorSemanticRuntime::contextForDocument(
    QTextDocument* document,
    const QString& fileName,
    const QString& moduleName,
    int cursorPosition,
    bool includeDocumentText) const
{
    EditorSemanticContext context;
    context.fileName = fileName;
    context.moduleName = moduleName;
    if (includeDocumentText)
        context.documentText = document->toPlainText();
    context.cursorPosition = cursorPosition;

    const QTextBlock block = document->findBlock(cursorPosition);
    if (block.isValid()) {
        context.lineText = block.text();
        context.column = cursorPosition - block.position();
        context.cursorLine = block.blockNumber() + 1;
        context.lineUpToCursor = context.lineText.left(context.column);
    }

    return context;
}
