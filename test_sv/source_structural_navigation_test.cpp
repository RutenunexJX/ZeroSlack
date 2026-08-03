#include "tsdocument.h"

#include <QCoreApplication>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString source =
        QStringLiteral(
            "module top;\n"
            "  logic selected;\n"
            "  logic other;\n"
            "  assign selected = other;\n"
            "  always_comb begin\n"
            "    other = selected;\n"
            "    selected <= other;\n"
            "    selected += 1;\n"
            "    $display(\"selected = ignored\");\n"
            "    // selected = ignored;\n"
            "  end\n"
            "endmodule\n");
    TSDocument syntax;
    syntax.setText(source);

    const int declaration =
        source.indexOf(QStringLiteral("selected"));
    const int continuousLhs =
        source.indexOf(QStringLiteral("selected"),
                       declaration + 1);
    const int rhsUse =
        source.indexOf(QStringLiteral("selected"),
                       continuousLhs + 1);
    const int nonblockingLhs =
        source.indexOf(QStringLiteral("selected"),
                       rhsUse + 1);
    const int operatorLhs =
        source.indexOf(QStringLiteral("selected"),
                       nonblockingLhs + 1);

    const TSAssignmentNavigationTarget fromDeclaration =
        syntax.assignmentNavigationTarget(
            declaration, false);
    expect("next assignment uses Tree-sitter lvalue nodes",
           fromDeclaration.ok()
               && fromDeclaration.identifier
                   == QStringLiteral("selected")
               && fromDeclaration.targetChar
                   == continuousLhs
               && fromDeclaration.assignmentCount == 3
               && !fromDeclaration.wrapped);

    const TSAssignmentNavigationTarget fromRhs =
        syntax.assignmentNavigationTarget(rhsUse, false);
    expect("RHS occurrence navigates to the next LHS",
           fromRhs.ok()
               && fromRhs.targetChar
                   == nonblockingLhs);

    const TSAssignmentNavigationTarget previousFromRhs =
        syntax.assignmentNavigationTarget(rhsUse, true);
    expect("previous assignment navigates to prior LHS",
           previousFromRhs.ok()
               && previousFromRhs.targetChar
                   == continuousLhs);

    const TSAssignmentNavigationTarget wrappedNext =
        syntax.assignmentNavigationTarget(
            operatorLhs, false);
    expect("next assignment wraps within the lexical scope",
           wrappedNext.ok()
               && wrappedNext.targetChar
                   == continuousLhs
               && wrappedNext.wrapped);

    const int commentSelected =
        source.indexOf(
            QStringLiteral("selected = ignored"),
            operatorLhs);
    expect("comments do not produce an identifier target",
           syntax.assignmentNavigationTarget(
                     commentSelected, false)
                   .status
               == TSAssignmentNavigationStatus::
                   NoIdentifier);

    const QString conditionalSource =
        QStringLiteral(
            "`ifdef OUTER\n"
            "module a;\n"
            "  `ifndef INNER\n"
            "  logic x;\n"
            "  `else\n"
            "  logic y;\n"
            "  `endif\n"
            "endmodule\n"
            "`elsif SECOND\n"
            "module b; endmodule\n"
            "`else\n"
            "module c; endmodule\n"
            "`endif\n"
            "// `ifdef COMMENT_ONLY\n");
    TSDocument conditionalSyntax;
    conditionalSyntax.setText(conditionalSource);

    const int outerIfdef =
        conditionalSource.indexOf(
            QStringLiteral("`ifdef OUTER"));
    const int innerIfndef =
        conditionalSource.indexOf(
            QStringLiteral("`ifndef INNER"));
    const int innerElse =
        conditionalSource.indexOf(
            QStringLiteral("`else"),
            innerIfndef);
    const int innerEndif =
        conditionalSource.indexOf(
            QStringLiteral("`endif"),
            innerElse);
    const int outerElsif =
        conditionalSource.indexOf(
            QStringLiteral("`elsif SECOND"));
    const int outerElse =
        conditionalSource.indexOf(
            QStringLiteral("`else"),
            outerElsif);
    const int outerEndif =
        conditionalSource.indexOf(
            QStringLiteral("`endif"),
            outerElse);

    const TSConditionalBranchNavigationTarget innerNext =
        conditionalSyntax.conditionalBranchNavigationTarget(
            innerIfndef, false);
    expect("nested conditional selects the innermost group",
           innerNext.ok()
               && innerNext.targetChar == innerElse
               && innerNext.targetDirective
                   == QStringLiteral("`else")
               && innerNext.branchStartChars
                   == QList<int>{
                       innerIfndef,
                       innerElse,
                       innerEndif});

    const int outerBody =
        conditionalSource.indexOf(
            QStringLiteral("module a"));
    const TSConditionalBranchNavigationTarget outerNext =
        conditionalSyntax.conditionalBranchNavigationTarget(
            outerBody, false);
    expect("next outer branch skips nested directives",
           outerNext.ok()
               && outerNext.targetChar == outerElsif
               && outerNext.branchStartChars
                   == QList<int>{
                       outerIfdef,
                       outerElsif,
                       outerElse,
                       outerEndif});

    const TSConditionalBranchNavigationTarget outerPrevious =
        conditionalSyntax.conditionalBranchNavigationTarget(
            outerElse + 2, true);
    expect("previous branch returns the containing branch marker",
           outerPrevious.ok()
               && outerPrevious.targetChar == outerElse);

    const TSConditionalBranchNavigationTarget outerWrap =
        conditionalSyntax.conditionalBranchNavigationTarget(
            outerEndif, false);
    expect("conditional branch navigation wraps",
           outerWrap.ok()
               && outerWrap.targetChar == outerIfdef
               && outerWrap.wrapped);

    const int commentOnly =
        conditionalSource.indexOf(
            QStringLiteral("`ifdef COMMENT_ONLY"));
    expect("comment text is not a conditional group",
           conditionalSyntax
                   .conditionalBranchNavigationTarget(
                       commentOnly, false)
                   .status
               == TSConditionalBranchNavigationStatus::
                   NoConditionalGroup);

    TSDocument incompleteSyntax;
    incompleteSyntax.setText(
        QStringLiteral(
            "`ifdef OPEN\n"
            "logic dangling;\n"));
    expect("incomplete conditional groups are rejected",
           incompleteSyntax
                   .conditionalBranchNavigationTarget(
                       QStringLiteral("`ifdef OPEN\n").size(),
                       false)
                   .status
               == TSConditionalBranchNavigationStatus::
                   IncompleteConditionalGroup);

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
