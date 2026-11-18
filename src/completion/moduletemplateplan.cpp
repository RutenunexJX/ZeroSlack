#include "moduletemplateplan.h"
#include "editorfileidentity.h"
#include "slangmanager.h"
#include "slangparseoptions.h"
#include "tsdocument.h"
#include <slang/ZeroSlackCancellation.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/parsing/Lexer.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxPrinter.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {
using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;
using namespace slang::parsing;
QString qt(std::string_view text) { return QString::fromUtf8(text.data(), qsizetype(text.size())); }
QString printed(const SyntaxNode* node) {
    if (!node) return {};
    SyntaxPrinter printer;
    printer.setIncludeComments(false).setIncludeDirectives(false).setIncludeSkipped(false)
        .setExpandIncludes(true).setExpandMacros(true).setSquashNewlines(true);
    printer.printExcludingLeadingComments(*node);
    return qt(printer.str()).trimmed();
}
QString dimensions(const DeclaredType& type) {
    QStringList parts;
    if (const auto* list = type.getDimensionSyntax())
        for (const auto* node : *list) parts << printed(node);
    return parts.join(' ');
}
struct Lexeme { TokenKind kind; QString name; int start, length; };
QList<Lexeme> lexemes(const QString& text) {
    const auto bytes = text.toUtf8();
    SourceManager manager;
    const auto buffer = manager.assignText(std::string_view(bytes.constData(), size_t(bytes.size())));
    BumpAllocator allocator;
    Diagnostics diagnostics;
    Lexer lexer(buffer, allocator, diagnostics, manager);
    QList<Lexeme> result;
    for (auto token = lexer.lex(); token.kind != TokenKind::EndOfFile; token = lexer.lex()) {
        const auto offset = token.location().offset();
        const auto start = QString::fromUtf8(bytes.constData(), qsizetype(offset)).size();
        result << Lexeme{token.kind, qt(token.valueText()), int(start), int(qt(token.rawText()).size())};
    }
    return result;
}
bool identifier(const QString& name) {
    const auto tokens = lexemes(name);
    return tokens.size() == 1 && tokens[0].kind == TokenKind::Identifier
        && tokens[0].start == 0 && tokens[0].length == name.size();
}
QString emittedName(const QString& name) {
    if (identifier(name)) return name;
    if (!name.isEmpty() && std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.isSpace(); }))
        return '\\' + name + ' ';
    return {};
}
enum class Fragment { Expression, Type, Dimensions };
// Only syntax name uses are rewritten; declarator / struct member names are not references.
QString rewrite(const QString& text, const QHash<QString, QString>& names, QSet<QString>* used = nullptr,
                Fragment fragment = Fragment::Expression) {
    if (text.trimmed().isEmpty() || (fragment == Fragment::Type && text == "type")) return text;
    const bool implicit = fragment == Fragment::Type && (text.startsWith("signed") || text.startsWith("unsigned") || text.startsWith('['));
    const QString prefix = fragment == Fragment::Type ? QString("module __fragment; ") + (implicit ? "logic " : "")
        : fragment == Fragment::Dimensions ? "module __fragment; logic __value " : "module __fragment; localparam __value = ";
    const QString suffix = fragment == Fragment::Type ? " __value; endmodule" : "; endmodule";
    const auto bytes = (prefix + text + suffix).toUtf8();
    SourceManager manager;
    const auto tree = SyntaxTree::fromText(std::string_view(bytes.constData(), size_t(bytes.size())), manager);
    QSet<int> references;
    auto record = [&](const SyntaxNode& node, Token token) {
        if (node.parent && node.parent->kind == SyntaxKind::ScopedName) {
            const auto& scope = node.parent->as<ScopedNameSyntax>();
            if (scope.right.get() == &node || scope.separator.kind == TokenKind::DoubleColon) return;
        }
        references.insert(int(QString::fromUtf8(bytes.constData(), qsizetype(token.location().offset())).size() - prefix.size()));
    };
    auto visitor = makeSyntaxVisitor([&](auto&, const IdentifierNameSyntax& node) { record(node, node.identifier); },
        [&](auto& visitor, const IdentifierSelectNameSyntax& node) { record(node, node.identifier); visitor.visitDefault(node); });
    tree->root().visit(visitor);
    auto result = text;
    const auto tokens = lexemes(text);
    for (int i = tokens.size() - 1; i >= 0; --i) {
        const auto& token = tokens[i];
        if (token.kind != TokenKind::Identifier || !names.contains(token.name) || !references.contains(token.start)) continue;
        if (used) used->insert(token.name);
        result.replace(token.start, token.length, names.value(token.name));
    }
    return result;
}
QString typeText(const DeclaredType& type, bool signal) {
    const auto* syntax = type.getTypeSyntax();
    auto text = printed(syntax);
    if (signal && (!syntax || syntax->kind == SyntaxKind::ImplicitType))
        text = text.isEmpty() ? QStringLiteral("logic") : QStringLiteral("logic ") + text;
    return text;
}
QStringList ordered(const SimulationCommandRequest& source) {
    auto result = source.orderedFiles;
    result.removeAll(QString());
    if (!result.contains(source.context.fileName)) result << source.context.fileName;
    return result;
}
QString diagnosticKey(const SemanticDiagnostic& diagnostic) {
    return EditorFileIdentity::lookupKey(diagnostic.fileName) + '\n' + diagnostic.codeName + '\n' + diagnostic.message;
}
bool validValue(const QString& value, bool typeParameter) {
    SourceManager manager;
    const auto text = (QString("module __value; localparam ") + (typeParameter ? "type " : "")
        + "__input = " + value + "; endmodule").toUtf8();
    const auto tree = SyntaxTree::fromText(std::string_view(text.constData(), size_t(text.size())), manager);
    if (!tree->diagnostics().empty() || tree->root().kind != SyntaxKind::ModuleDeclaration) return false;
    const auto& module = tree->root().as<ModuleDeclarationSyntax>();
    if (module.members.size() != 1 || module.members[0]->kind != SyntaxKind::ParameterDeclarationStatement) return false;
    const auto& parameter = *module.members[0]->as<ParameterDeclarationStatementSyntax>().parameter;
    return typeParameter ? parameter.kind == SyntaxKind::TypeParameterDeclaration
            && parameter.as<TypeParameterDeclarationSyntax>().declarators.size() == 1
        : parameter.kind == SyntaxKind::ParameterDeclaration
            && parameter.as<ParameterDeclarationSyntax>().declarators.size() == 1;
}
}

bool ModuleTemplatePlanner::handles(const QVariantMap& configuration) {
    return configuration.value("kind").toString() == "moduleDeclarations";
}
QString ModuleTemplatePlanner::configurationError(const QVariantMap& configuration) {
    if (!handles(configuration)) return {};
    if (!QStringList{"signals", "parameters"}.contains(configuration.value("mode").toString()))
        return "Module declarations require signals or parameters mode.";
    if (configuration.value("context").toString() != "module")
        return "Module declarations require a module body.";
    return {};
}
ModuleTemplateModel ModuleTemplatePlanner::inspect(const SimulationCommandRequest& source,
                                                   const SemanticSymbolRecord& module) {
    ModuleTemplateModel result;
    result.name = module.name; result.file = module.location.fileName;
    const std::function<bool()> cancelled = [flag = source.cancelled] { return flag && flag->load(); };
    try {
        const slang::zeroslack::CancellationScope cancellation(cancelled);
        SemanticAnalysisRequest request;
        request.sourceOverrides = source.contents;
        request.sourceOverrides.insert(source.context.fileName, source.context.documentText);
        SemanticInputCapture capture(request);
        capture.beginCompilationObservation();
        SourceManager manager;
        manager.setDisableProximatePaths(true);
        manager.setSourceLoader([&](const std::filesystem::path& path, SmallVector<char>& buffer) {
            const auto raw = path.generic_u8string();
            const auto& input = capture.source(QString::fromUtf8(reinterpret_cast<const char*>(raw.data()), qsizetype(raw.size())));
            if (!input.readable) return std::make_error_code(std::errc::no_such_file_or_directory);
            const auto bytes = input.text.toUtf8();
            buffer.insert(buffer.end(), bytes.cbegin(), bytes.cend()); buffer.push_back('\0');
            return std::error_code{};
        });
        std::vector<SourceBuffer> buffers;
        auto files = ordered(source);
        if (!files.contains(module.location.fileName)) files << module.location.fileName;
        QSet<QString> seen;
        for (const auto& file : files) {
            const auto key = SemanticInputCapture::pathKey(file);
            if (seen.contains(key)) continue;
            seen.insert(key);
            const auto& input = capture.source(file);
            if (!input.readable) { result.error = "Source is unavailable: " + file; return result; }
            const auto bytes = input.text.toUtf8();
            buffers.push_back(manager.assignText(QDir::fromNativeSeparators(file).toStdString(),
                std::string_view(bytes.constData(), size_t(bytes.size()))));
        }
        auto tree = SyntaxTree::fromBuffers(buffers, manager, slang_parse_options::makeSyntaxOptions(
            slang_parse_options::effectiveIncludeDirsForFiles(files, source.includeDirectories), source.defines));
        Compilation compilation(slang_parse_options::makeCompilationOptions());
        compilation.addSyntaxTree(tree);
        const auto& root = compilation.getRoot();
        const DefinitionSymbol* selected = nullptr;
        for (const auto* symbol : compilation.getDefinitions()) {
            const auto* definition = symbol->as_if<DefinitionSymbol>();
            if (!definition || qt(definition->name) != module.name) continue;
            const auto location = manager.getFullyExpandedLoc(definition->location);
            const auto path = manager.getFullPath(location.buffer()).generic_u8string();
            const auto file = QString::fromUtf8(reinterpret_cast<const char*>(path.data()), qsizetype(path.size()));
            if (!EditorFileIdentity::same(file, module.location.fileName)) continue;
            if (module.location.startLine > 0 && int(manager.getLineNumber(location)) != module.location.startLine) continue;
            if (selected) { result.error = "Module identity is ambiguous. Resolve duplicate definitions first."; return result; }
            selected = definition;
        }
        if (!selected) { result.error = "The selected module changed. Reopen module selection."; return result; }
        auto& instance = InstanceSymbol::createDefault(compilation, *selected);
        instance.setParent(root);
        for (const auto& member : instance.body.members()) {
            ModuleTemplateMember item;
            item.name = qt(member.name);
            if (const auto* parameter = member.as_if<ParameterSymbol>()) {
                item.local = parameter->isLocalParam();
                const auto& type = *parameter->getDeclaredType();
                item.type = typeText(type, false); item.dimensions = dimensions(type);
                const auto* syntax = parameter->defaultValSyntax;
                if (syntax && syntax->kind == SyntaxKind::Declarator) {
                    const auto& declarator = syntax->as<DeclaratorSyntax>();
                    if (declarator.initializer) syntax = declarator.initializer->expr;
                    else syntax = nullptr;
                }
                item.value = printed(syntax);
            } else if (const auto* parameter = member.as_if<TypeParameterSymbol>()) {
                item.local = parameter->isLocalParam(); item.typeParameter = true;
                item.type = "type"; item.value = printed(parameter->targetType.getTypeSyntax());
            } else continue;
            result.parameters << item;
        }
        for (const auto* member : instance.body.getPortList()) {
            const auto* port = member->as_if<PortSymbol>();
            if (!port || port->isNullPort || port->name.empty() || !port->internalSymbol
                || !port->internalSymbol->isValue()) {
                result.portsError = "This module contains an interface or expression port that cannot become an internal signal.";
                continue;
            }
            const auto& type = *port->internalSymbol->as<ValueSymbol>().getDeclaredType();
            ModuleTemplateMember item;
            item.name = qt(port->name); item.type = typeText(type, true); item.dimensions = dimensions(type);
            if (port->direction == ArgumentDirection::InOut) item.type.prepend("wire ");
            result.ports << item;
        }
        if (cancelled() || !capture.stillMatchesDisk(cancelled)) result.error = "Sources changed during preparation. Reopen module selection.";
        result.captured = capture.seal();
    } catch (const slang::zeroslack::CompilationCancelled&) { result.error = "Module preparation was cancelled."; }
    catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
    return result;
}

ModuleTemplatePlan ModuleTemplatePlanner::prepare(const ModuleTemplateModel& model,
                                                  const ModuleTemplateOptions& requested,
                                                  const QSet<QString>& occupied) {
    auto options = requested;
    if (options.mode == "signals") { options.prefix.clear(); options.suffix.clear(); }
    ModuleTemplatePlan result;
    if (!model.error.isEmpty()) { result.error = model.error; return result; }
    if (options.mode == "signals" && !model.portsError.isEmpty()) { result.error = model.portsError; return result; }
    if (!QStringList{"signals", "parameters"}.contains(options.mode)
        || !QStringList{"parameter", "localparam"}.contains(options.parameterKeyword)) {
        result.error = "Unsupported declaration options."; return result;
    }
    QHash<QString, QString> names;
    QHash<QString, int> parameters;
    for (int i = 0; i < model.parameters.size(); ++i) {
        const auto& item = model.parameters[i];
        parameters.insert(item.name, i);
        names.insert(item.name, emittedName(options.prefix + item.name + options.suffix));
    }
    for (auto it = options.values.cbegin(); it != options.values.cend(); ++it) {
        if (!parameters.contains(it.key()) || model.parameters[parameters[it.key()]].local) {
            result.error = "Only overridable parameters can be edited: " + it.key(); return result;
        }
        if (!validValue(it.value(), model.parameters[parameters[it.key()]].typeParameter)) {
            result.error = "Enter one valid SystemVerilog " + QString(model.parameters[parameters[it.key()]].typeParameter ? "type" : "expression") + " for " + it.key() + '.';
            return result;
        }
    }
    QSet<QString> needed;
    if (options.mode == "parameters") {
        needed = options.selectedParameters;
        for (const auto& name : needed) if (!parameters.contains(name)) {
            result.error = "Unknown parameter: " + name; return result;
        }
    } else {
        for (const auto& item : model.ports) {
            rewrite(item.type, names, &needed, Fragment::Type);
            rewrite(item.dimensions, names, &needed, Fragment::Dimensions);
        }
    }
    QStringList order;
    QSet<QString> visiting, visited;
    std::function<bool(const QString&)> visit = [&](const QString& name) {
        if (visited.contains(name)) return true;
        if (visiting.contains(name)) { result.error = "Cyclic parameter dependency: " + name; return false; }
        visiting.insert(name);
        const auto& item = model.parameters[parameters[name]];
        QSet<QString> dependencies;
        rewrite(item.type, names, &dependencies, Fragment::Type);
        rewrite(item.dimensions, names, &dependencies, Fragment::Dimensions);
        rewrite(options.values.value(name, item.value), names, &dependencies, item.typeParameter ? Fragment::Type : Fragment::Expression);
        for (const auto& candidate : model.parameters)
            if (dependencies.contains(candidate.name) && !visit(candidate.name)) return false;
        visiting.remove(name); visited.insert(name); order << name;
        return true;
    };
    for (const auto& item : model.parameters) if (needed.contains(item.name) && !visit(item.name)) return result;
    result.requiredParameters = order;
    QStringList declarations, conflicts;
    QSet<QString> generated;
    auto declareName = [&](const QString& name) {
        const auto raw = options.prefix + name + options.suffix;
        const auto rendered = emittedName(raw);
        if (rendered.isEmpty() || ((!options.prefix.isEmpty() || !options.suffix.isEmpty()) && !identifier(raw))) {
            result.error = "Invalid generated identifier: " + raw; return QString();
        }
        if (options.mode != "signals" && (occupied.contains(raw) || generated.contains(raw))) conflicts << raw;
        generated.insert(raw);
        return rendered;
    };
    if (options.mode == "parameters" || options.includeParameters) {
        for (const auto& name : order) {
            const auto& item = model.parameters[parameters[name]];
            const auto value = options.values.value(name, item.value).trimmed();
            if (value.isEmpty()) { result.error = "Enter a value for " + name + '.'; return result; }
            const auto keyword = item.local ? QString("localparam") : options.parameterKeyword;
            QString line = keyword + ' ';
            const auto type = rewrite(item.type, names, nullptr, Fragment::Type).trimmed();
            if (!type.isEmpty()) line += type + ' ';
            line += declareName(name);
            if (!item.dimensions.isEmpty()) line += ' ' + rewrite(item.dimensions, names, nullptr, Fragment::Dimensions);
            line += " = " + rewrite(value, names, nullptr, item.typeParameter ? Fragment::Type : Fragment::Expression) + ';';
            declarations << line;
        }
    }
    if (options.mode == "signals") {
        for (const auto& item : model.ports) {
            auto line = rewrite(item.type, names, nullptr, Fragment::Type).trimmed() + ' ' + declareName(item.name);
            if (!item.dimensions.isEmpty()) line += ' ' + rewrite(item.dimensions, names, nullptr, Fragment::Dimensions);
            declarations << line + ';';
        }
    }
    result.text = declarations.join('\n');
    if (!result.text.isEmpty()) result.text += '\n';
    if (!conflicts.isEmpty()) result.error = "Names already declared: " + conflicts.join(", ") + ". Choose a prefix or suffix.";
    if (result.text.isEmpty() && result.error.isEmpty()) result.error = options.mode == "signals" ? "This module has no ports." : "Select at least one parameter.";
    return result;
}

QString ModuleTemplatePlanner::validateInsertion(const SimulationCommandRequest& source,
                                                 const ModuleTemplateModel& model, const QString& text, bool validateSemantics) {
    if (!model.captured) return "Module sources are unavailable.";
    if (source.context.readOnly || source.context.cursorPosition < 0
        || source.context.cursorPosition > source.context.documentText.size()) return "The insertion position is no longer editable.";
    TSDocument syntax; syntax.setText(source.context.documentText);
    const int position = source.context.cursorPosition;
    const int lineStart = source.context.documentText.lastIndexOf('\n', qMax(0, position - 1)) + 1;
    const int newline = source.context.documentText.indexOf('\n', position);
    const int lineEnd = newline < 0 ? source.context.documentText.size() : newline;
    if (!source.context.documentText.mid(lineStart, lineEnd - lineStart).trimmed().isEmpty()
        || !syntax.moduleScopeTarget(position).ok() || syntax.alwaysScopeTarget(position).ok()
        || syntax.isCommentAt(position) || syntax.isStringAt(position))
        return "Choose an empty line in a module body.";
    if (!validateSemantics) return {};
    const auto cancelled = [flag = source.cancelled] { return flag && flag->load(); };
    QHash<QString, QString> contents;
    for (auto it = model.captured->sources.cbegin(); it != model.captured->sources.cend(); ++it)
        if (it->readable) contents.insert(it.key(), it->text);
    const auto key = SemanticInputCapture::pathKey(source.context.fileName);
    auto original = source.context.documentText;
    contents.insert(key, original);
    auto files = ordered(source);
    if (!files.contains(model.file)) files << model.file;
    SlangManager manager;
    const auto before = manager.analyzeOverlayWorkspace(contents, files, source.includeDirectories,
        source.defines, cancelled, {false, true, false});
    if (before.cancelled || !before.error.isEmpty()) return "Source validation was cancelled or failed: " + before.error;
    QHash<QString, int> existing;
    for (const auto& diagnostic : before.diagnostics)
        if (diagnostic.severity == SemanticDiagnostic::Error) ++existing[diagnosticKey(diagnostic)];
    original.insert(source.context.cursorPosition, text);
    contents.insert(key, original);
    const auto after = manager.analyzeOverlayWorkspace(contents, files, source.includeDirectories,
        source.defines, cancelled, {false, true, false});
    if (after.cancelled || !after.error.isEmpty()) return "Source validation was cancelled or failed: " + after.error;
    for (const auto& diagnostic : after.diagnostics) {
        if (diagnostic.severity != SemanticDiagnostic::Error) continue;
        const int firstLine = source.context.documentText.left(position).count('\n') + 1;
        const int lastLine = firstLine + text.count('\n');
        if (EditorFileIdentity::same(diagnostic.fileName, source.context.fileName)
            && diagnostic.line >= firstLine && diagnostic.line <= lastLine) return diagnostic.message;
        auto& remaining = existing[diagnosticKey(diagnostic)];
        if (remaining > 0) { --remaining; continue; }
        return diagnostic.message;
    }
    return {};
}
