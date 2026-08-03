#include "inlinecommandmode.h"
#include "packagetoolservice.h"
#include "tsdocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

bool writeFile(const QString& fileName, const QByteArray& contents)
{
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

QString appliedText(const QString& source,
                    const StructuredInlineInsertionPlan& plan)
{
    QString result = source;
    if (plan.ok()) {
        result.replace(plan.replacementStart,
                       plan.replacementEnd - plan.replacementStart,
                       plan.replacementText);
    }
    return result;
}

QString includePathForFile(
    const QList<HeaderIncludeCandidate>& candidates,
    const QString& fileName)
{
    const QString absolute =
        QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());
    for (const HeaderIncludeCandidate& candidate : candidates) {
        if (QDir::cleanPath(
                QFileInfo(candidate.absoluteFilePath).absoluteFilePath())
            == absolute) {
            return candidate.includePath;
        }
    }
    return QString();
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QString validPackageSource =
        QStringLiteral("module top;\n"
                       "  ;pk\n"
                       "endmodule\n");
    const int packageStart =
        validPackageSource.indexOf(QStringLiteral(";pk"));
    const PackageImportSite validSite =
        PackageToolService::analyzePackageImportSite(
            validPackageSource,
            packageStart,
            packageStart + 3);
    expect("package import site is accepted by Tree-sitter",
           validSite.valid);
    expect("available package remains selectable",
           validSite.canImport(QStringLiteral("common_pkg")));

    const StructuredInlineInsertionPlan packagePlan =
        PackageToolService::packageImportPlan(
            validPackageSource,
            packageStart,
            packageStart + 3,
            QStringLiteral("common_pkg"));
    expect("package import plan is structured and exact",
           packagePlan.ok()
               && packagePlan.replacementText
                      == QStringLiteral("import common_pkg::*;"));
    TSDocument packagePreview;
    packagePreview.setText(appliedText(validPackageSource, packagePlan));
    expect("planned package import parses without syntax errors",
           !packagePreview.hasError());

    const QString duplicatePackageSource =
        QStringLiteral("module top;\n"
                       "  import common_pkg::*;\n"
                       "  ;pk\n"
                       "endmodule\n");
    const int duplicatePackageStart =
        duplicatePackageSource.indexOf(QStringLiteral(";pk"));
    const PackageImportSite duplicateSite =
        PackageToolService::analyzePackageImportSite(
            duplicatePackageSource,
            duplicatePackageStart,
            duplicatePackageStart + 3);
    expect("wildcard package import is deduplicated in its syntax scope",
           duplicateSite.valid
               && !duplicateSite.canImport(
                   QStringLiteral("common_pkg")));
    expect("duplicate package plan is explicit",
           PackageToolService::packageImportPlan(
               duplicatePackageSource,
               duplicatePackageStart,
               duplicatePackageStart + 3,
               QStringLiteral("common_pkg")).duplicate());

    const QString namedPackageSource =
        QStringLiteral("module top;\n"
                       "  import common_pkg::one_symbol;\n"
                       "  ;pk\n"
                       "endmodule\n");
    const int namedPackageStart =
        namedPackageSource.indexOf(QStringLiteral(";pk"));
    const PackageImportSite namedImportSite =
        PackageToolService::analyzePackageImportSite(
            namedPackageSource,
            namedPackageStart,
            namedPackageStart + 3);
    expect("named import does not suppress a wildcard import",
           namedImportSite.valid
               && namedImportSite.canImport(
                   QStringLiteral("common_pkg")));

    const QString selfPackageSource =
        QStringLiteral("package common_pkg;\n"
                       "  ;pk\n"
                       "endpackage\n");
    const int selfPackageStart =
        selfPackageSource.indexOf(QStringLiteral(";pk"));
    expect("enclosing package cannot import itself",
           !PackageToolService::analyzePackageImportSite(
                selfPackageSource,
                selfPackageStart,
                selfPackageStart + 3)
                .canImport(QStringLiteral("common_pkg")));

    const QString commentPackageSource =
        QStringLiteral("module top;\n"
                       "  // ;pk\n"
                       "endmodule\n");
    const int commentPackageStart =
        commentPackageSource.indexOf(QStringLiteral(";pk"));
    expect("package command in comment is rejected",
           !PackageToolService::analyzePackageImportSite(
                commentPackageSource,
                commentPackageStart,
                commentPackageStart + 3)
                .valid);

    const QString stringPackageSource =
        QStringLiteral("module top;\n"
                       "  string value = \";pk\";\n"
                       "endmodule\n");
    const int stringPackageStart =
        stringPackageSource.indexOf(QStringLiteral(";pk"));
    expect("package command in string is rejected",
           !PackageToolService::analyzePackageImportSite(
                stringPackageSource,
                stringPackageStart,
                stringPackageStart + 3)
                .valid);

    const QString expressionPackageSource =
        QStringLiteral("module top;\n"
                       "  assign value = ;pk;\n"
                       "endmodule\n");
    const int expressionPackageStart =
        expressionPackageSource.indexOf(QStringLiteral(";pk"));
    expect("package import in an expression is rejected structurally",
           !PackageToolService::analyzePackageImportSite(
                expressionPackageSource,
                expressionPackageStart,
                expressionPackageStart + 3)
                .valid);

    const QString headerSource =
        QStringLiteral("module top;\n"
                       "  ;h\n"
                       "endmodule\n");
    const int headerStart =
        headerSource.indexOf(QStringLiteral(";h"));
    const StructuredInlineInsertionPlan headerPlan =
        PackageToolService::headerIncludePlan(
            headerSource,
            headerStart,
            headerStart + 2,
            QStringLiteral("rtl/defs.svh"));
    expect("header include plan is structured and exact",
           headerPlan.ok()
               && headerPlan.replacementText
                      == QStringLiteral("`include \"rtl/defs.svh\""));
    TSDocument headerPreview;
    headerPreview.setText(appliedText(headerSource, headerPlan));
    expect("planned header include parses without syntax errors",
           !headerPreview.hasError());

    const QString duplicateHeaderSource =
        QStringLiteral("`include \"rtl/defs.svh\"\n"
                       ";h\n");
    const int duplicateHeaderStart =
        duplicateHeaderSource.indexOf(QStringLiteral(";h"));
    expect("duplicate quoted include is rejected",
           PackageToolService::headerIncludePlan(
               duplicateHeaderSource,
               duplicateHeaderStart,
               duplicateHeaderStart + 2,
               QStringLiteral("rtl\\defs.svh")).duplicate());

    const QString commentHeaderSource =
        QStringLiteral("// ;h\n");
    const int commentHeaderStart =
        commentHeaderSource.indexOf(QStringLiteral(";h"));
    expect("header command in comment is rejected",
           !PackageToolService::headerIncludePlan(
                commentHeaderSource,
                commentHeaderStart,
                commentHeaderStart + 2,
                QStringLiteral("defs.svh")).ok());

    const QString expressionHeaderSource =
        QStringLiteral("module top;\n"
                       "  assign value = ;h;\n"
                       "endmodule\n");
    const int expressionHeaderStart =
        expressionHeaderSource.indexOf(QStringLiteral(";h"));
    expect("header include in an expression is rejected structurally",
           !PackageToolService::headerIncludePlan(
                expressionHeaderSource,
                expressionHeaderStart,
                expressionHeaderStart + 2,
                QStringLiteral("defs.svh")).ok());

    QTemporaryDir temporary;
    expect("temporary header project is available", temporary.isValid());
    if (temporary.isValid()) {
        const QString root = temporary.path();
        const QString currentFile =
            QDir(root).filePath(QStringLiteral("src/top.sv"));
        const QString commonA =
            QDir(root).filePath(QStringLiteral("a/common.svh"));
        const QString commonB =
            QDir(root).filePath(QStringLiteral("b/common.svh"));
        const QString uniqueHeader =
            QDir(root).filePath(QStringLiteral("inc/unique.svh"));
        const bool filesWritten =
            writeFile(currentFile, QByteArray("module top; endmodule\n"))
            && writeFile(commonA, QByteArray("// a\n"))
            && writeFile(commonB, QByteArray("// b\n"))
            && writeFile(uniqueHeader, QByteArray("// unique\n"));
        expect("temporary header files are written", filesWritten);

        const QList<HeaderIncludeCandidate> candidates =
            PackageToolService::headerIncludeCandidates(
                {commonA, commonB, uniqueHeader, currentFile},
                currentFile,
                {QDir(root).filePath(QStringLiteral("a")),
                 QDir(root).filePath(QStringLiteral("b")),
                 QDir(root).filePath(QStringLiteral("inc"))});
        expect("ambiguous basename uses shortest unique path for first file",
               includePathForFile(candidates, commonA)
                   == QStringLiteral("a/common.svh"));
        expect("ambiguous basename uses shortest unique path for second file",
               includePathForFile(candidates, commonB)
                   == QStringLiteral("b/common.svh"));
        expect("unambiguous include root uses basename",
               includePathForFile(candidates, uniqueHeader)
                   == QStringLiteral("unique.svh"));
        expect("current source file is never an include candidate",
               includePathForFile(candidates, currentFile).isEmpty());
    }

    const InlineCommandMatch newHeaderMatch =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            QStringLiteral(";h -n defs"));
    expect(";h -n remains the only create-and-include command",
           newHeaderMatch.matched
               && newHeaderMatch.intent
                      == InlineCommandIntent::HeaderInclude
               && newHeaderMatch.input
                      == QStringLiteral("-n defs"));
    const QString newHeaderSource = QStringLiteral(";h -n defs");
    const StructuredInlineInsertionPlan newHeaderPlan =
        PackageToolService::headerIncludePlan(
            newHeaderSource,
            0,
            newHeaderSource.size(),
            QStringLiteral("defs.svh"));
    expect(";h -n uses the same structured include plan",
           newHeaderPlan.ok()
               && newHeaderPlan.replacementText
                      == QStringLiteral("`include \"defs.svh\""));
    expect("legacy include-space entry does not reactivate command mode",
           !InlineCommandMode::matchAbbreviationBeforeCursor(
                QStringLiteral("`include "))
                .matched);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
