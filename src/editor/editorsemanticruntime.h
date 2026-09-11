#ifndef EDITORSEMANTICRUNTIME_H
#define EDITORSEMANTICRUNTIME_H

#include <QString>

class EditorSemanticContextService;
class QTextDocument;
struct EditorSemanticContext;

class EditorSemanticRuntime
{
public:
    void init();
    void setService(EditorSemanticContextService* nextService);
    EditorSemanticContextService* contextService() const;

    EditorSemanticContext contextForDocument(
        QTextDocument* document,
        const QString& fileName,
        const QString& moduleName,
        int cursorPosition,
        bool includeDocumentText,
        std::uint64_t documentRevision) const;

private:
    EditorSemanticContextService* service = nullptr;
};

#endif // EDITORSEMANTICRUNTIME_H
