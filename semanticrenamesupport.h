#ifndef SEMANTICRENAMESUPPORT_H
#define SEMANTICRENAMESUPPORT_H

#include "semanticindex.h"

#include <QString>

bool isSupportedSemanticRenameSubject(
    const SemanticSymbolRecord& record);
bool isStructuralSemanticRenameSubject(
    const SemanticSymbolRecord& record);
QString semanticRenameKindLabel(
    const SemanticSymbolRecord& record);
QString semanticRenameOwnerLabel(
    const SemanticSymbolRecord& record);
QString semanticRenameInlineValidation(
    SemanticIndex* index,
    const SemanticSymbolRecord& subject,
    const QString& newName);

#endif // SEMANTICRENAMESUPPORT_H
