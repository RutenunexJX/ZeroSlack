#ifndef SYMBOLHOVERSERVICE_H
#define SYMBOLHOVERSERVICE_H

#include "definitionnavigationservice.h"
#include "symbolhoverreports.h"

#include <memory>

struct EditorSemanticContext;
struct SourceIdentifierTarget;

class SymbolHoverService
{
public:
    static SymbolHoverService* getInstance();

    explicit SymbolHoverService(SemanticIndex* semanticIndex = nullptr);
    ~SymbolHoverService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    SymbolHoverReport hoverForContext(
        const EditorSemanticContext& context,
        const SourceIdentifierTarget* sourceIdentifier = nullptr) const;

private:
    SemanticIndex* index = nullptr;
    std::unique_ptr<DefinitionNavigationService> definitionNavigation;
    static std::unique_ptr<SymbolHoverService> instance;
};

#endif // SYMBOLHOVERSERVICE_H
