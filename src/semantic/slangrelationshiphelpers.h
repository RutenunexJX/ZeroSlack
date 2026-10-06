#ifndef SLANGRELATIONSHIPHELPERS_H
#define SLANGRELATIONSHIPHELPERS_H

#include "slangmanager.h"

#include <slang/ast/Expression.h>
#include <slang/ast/TimingControl.h>
#include <slang/text/SourceLocation.h>

namespace slang {
class SourceManager;
}

namespace slang_relationship::detail {

SemanticValueReference valueReference(const slang::ast::Expression& expr,
                                      const slang::SourceManager* sm);
QList<SemanticValueReference> collectValueReferences(
    const slang::ast::Expression& expr, const slang::SourceManager* sm);
void projectValueReferences(const QList<SemanticValueReference>& references,
                            QStringList& names, QStringList& accessPaths);
bool isDirectValueForwardExpression(
    const slang::ast::Expression& expr);

SemanticSourceRange relationshipEvidenceRange(
    const slang::SourceManager* sm,
    slang::SourceRange range);

void appendConditionReference(QVector<ConditionReferenceInfo>& result,
                              const slang::SourceManager* sm,
                              const slang::ast::Expression& expr);

void appendTimingSignal(QVector<TimingSignalInfo>& result,
                        const slang::SourceManager* sm,
                        const slang::ast::SignalEventControl& event);

} // namespace slang_relationship::detail

#endif // SLANGRELATIONSHIPHELPERS_H
