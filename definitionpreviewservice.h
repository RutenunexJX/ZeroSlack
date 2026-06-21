#ifndef DEFINITIONPREVIEWSERVICE_H
#define DEFINITIONPREVIEWSERVICE_H

#include "definitionnavigationservice.h"
#include "symbolhoverreports.h"

#include <memory>

class DocumentModel;
struct EditorSemanticContext;

class DefinitionPreviewService
{
public:
    static DefinitionPreviewService* getInstance();

    explicit DefinitionPreviewService(SemanticIndex* semanticIndex = nullptr);
    ~DefinitionPreviewService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setDocumentModel(DocumentModel* documentModel);
    DefinitionPreviewReport previewForContext(
        const EditorSemanticContext& context) const;

private:
    SemanticIndex* index = nullptr;
    DocumentModel* documents = nullptr;
    std::unique_ptr<DefinitionNavigationService> definitionNavigation;
    static std::unique_ptr<DefinitionPreviewService> instance;

    QString previewTextForFile(const QString& fileName) const;
};

#endif // DEFINITIONPREVIEWSERVICE_H
