#include "symbolpresentationservice.h"

SemanticSymbolPresentation SymbolPresentationService::presentationForRecord(
    const SemanticSymbolRecord& record)
{
    return record.presentation;
}
