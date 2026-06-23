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

QString assignmentRootName(const slang::ast::Expression& expr);

QString expressionAccessPath(const slang::ast::Expression& expr);

QStringList collectValueNames(const slang::ast::Expression& expr);

QStringList collectValueAccessPaths(const slang::ast::Expression& expr);

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
