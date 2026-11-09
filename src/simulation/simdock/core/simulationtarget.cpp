#include "simulationtarget.h"
#include "workspace.h"
#include "slangparseoptions.h"
#include <zeroslack/semantic/semanticanalysisinput.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>
#include <QDir>
#include <QFileInfo>

namespace simdock {
namespace {
struct Tops : slang::syntax::SyntaxVisitor<Tops> {
    QStringList names;
    void handle(const slang::syntax::ModuleDeclarationSyntax& node) {
        if (node.kind == slang::syntax::SyntaxKind::ModuleDeclaration || node.kind == slang::syntax::SyntaxKind::ProgramDeclaration) {
            const auto name = node.header->name.valueText();
            names << QString::fromUtf8(name.data(), qsizetype(name.size()));
        }
        // Nested declarations cannot be selected as simulation roots.
    }
};
}
QStringList declaredSimulationTops(const QString& root, const QString& file, const QStringList& includes,
    const QHash<QString, QString>& defines, const QMap<QString, QByteArray>& overrides, QString* error)
{
    error->clear();
    const auto path = QDir(root).absoluteFilePath(file);
    if (!insideWorkspace(root, path)) { *error = QStringLiteral("The TB must belong to this workspace."); return {}; }
    SemanticAnalysisRequest request;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        request.sourceOverrides.insert(QDir(root).absoluteFilePath(it.key()), QString::fromUtf8(it.value()));
    SemanticInputCapture capture(request);
    const auto& source = capture.source(path);
    if (!source.readable) { *error = QStringLiteral("Cannot read the selected TB: %1").arg(path); return {}; }
    slang::SourceManager manager;
    manager.setDisableProximatePaths(true);
    manager.setSourceLoader([&](const std::filesystem::path& name, slang::SmallVector<char>& buffer) {
        const auto u8 = name.generic_u8string();
        const auto includedPath = QString::fromUtf8(reinterpret_cast<const char*>(u8.data()), qsizetype(u8.size()));
        if (!insideWorkspace(root, includedPath)) return std::make_error_code(std::errc::permission_denied);
        const auto& included = capture.source(includedPath);
        if (!included.readable) return std::make_error_code(std::errc::no_such_file_or_directory);
        const auto bytes = included.text.toUtf8();
        buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend()); buffer.push_back('\0');
        return std::error_code{};
    });
    const auto bytes = source.text.toUtf8();
    auto buffer = manager.assignText(path.toUtf8().toStdString(), std::string_view(bytes.constData(), size_t(bytes.size())));
    auto directories = includes;
    directories << QFileInfo(path).absolutePath() << root;
    const auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, manager,
        slang_parse_options::makeUnitSyntaxOptions(path, slang_parse_options::uniqueOrderedIncludeDirs(directories), defines));
    slang::DiagnosticEngine engine(manager);
    for (const auto& diagnostic : tree->diagnostics()) {
        const auto severity = engine.getSeverity(diagnostic.code, diagnostic.location);
        if (severity == slang::DiagnosticSeverity::Error || severity == slang::DiagnosticSeverity::Fatal) {
            const auto location = manager.getFullyExpandedLoc(diagnostic.location);
            *error = QStringLiteral("%1:%2: %3").arg(path).arg(manager.getLineNumber(location))
                .arg(QString::fromStdString(engine.formatMessage(diagnostic)));
            return {};
        }
    }
    if (!capture.stillMatchesDisk({})) { *error = QStringLiteral("The TB changed while its declarations were being read."); return {}; }
    Tops tops;
    tree->root().visit(tops);
    return tops.names;
}
QString validateSimulationTarget(const QString& root, const SimulationTarget& target, const QStringList& includes,
    const QHash<QString, QString>& defines, const QMap<QString, QByteArray>& overrides)
{
    if (target.file.isEmpty() || target.top.isEmpty()) return QStringLiteral("Select a TB file and its declared module or program top.");
    QString error;
    const auto names = declaredSimulationTops(root, target.file, includes, defines, overrides, &error);
    if (!error.isEmpty()) return error;
    return names.count(target.top) == 1 ? QString() : QStringLiteral("%1: top '%2' is absent or ambiguous. Select the TB top again.").arg(target.file, target.top);
}
}
