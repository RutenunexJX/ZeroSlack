#ifndef DEFINITIONPREVIEWSERVICE_H
#define DEFINITIONPREVIEWSERVICE_H

#include "definitionnavigationservice.h"
#include "symbolhoverreports.h"

#include <QPointer>

#include <memory>

class DocumentModel;
struct EditorSemanticContext;
struct SourceIdentifierTarget;

class DefinitionPreviewService
{
public:
    static DefinitionPreviewService* getInstance();

    explicit DefinitionPreviewService(SemanticIndex* semanticIndex = nullptr);
    ~DefinitionPreviewService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setDocumentModel(DocumentModel* documentModel);
    DefinitionPreviewReport previewForContext(
        const EditorSemanticContext& context,
        const SourceIdentifierTarget* sourceIdentifier = nullptr) const;

private:
    SemanticIndex* index = nullptr;
    QPointer<DocumentModel> documents;
    std::unique_ptr<DefinitionNavigationService> definitionNavigation;
    static std::unique_ptr<DefinitionPreviewService> instance;

};

#endif // DEFINITIONPREVIEWSERVICE_H
