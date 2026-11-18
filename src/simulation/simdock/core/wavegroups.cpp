#include "wavegroups.h"
#include "questasession.h"
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/parsing/Lexer.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace simdock {
namespace {
using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;
using namespace slang::parsing;
QString text(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
struct Mark { QString group, file, error; int line = 0; bool assigned = false, instantiated = false; };
bool parseComment(const QString& comment, Mark& mark) {
    auto body = comment.mid(2, comment.size() - 4).trimmed();
    if (!body.startsWith("zs_group") || (body.size() > 8 && !body[8].isSpace() && body[8] != '=')) return false;
    body = body.mid(8).trimmed();
    if (body.startsWith('=')) mark.group = body.mid(1).trimmed();
    if (!validWaveGroupName(mark.group)) mark.error = "Expected /* zs_group = name */; use letters, digits and underscores, starting with a letter or underscore.";
    return true;
}
QString location(const Mark& mark) { return QString("%1:%2: zs_group: ").arg(mark.file).arg(mark.line); }
QString simulatorObjectPath(const QString& hierarchy) {
    QString result = "/";
    bool escaped = false;
    for (const auto ch : hierarchy) {
        if (!escaped && ch == '\\') { escaped = true; result += ch; }
        else if (escaped && ch.isSpace()) { escaped = false; result += ch; }
        else result += !escaped && ch == '.' ? QChar('/') : ch;
    }
    return result;
}
struct Collector : ASTVisitor<Collector, false, false> {
    QList<Mark>& marks;
    const QHash<const SyntaxNode*, int>& declarations;
    WaveGroupPlan& plan;
    QSet<QString> emitted;
    Collector(QList<Mark>& m, const QHash<const SyntaxNode*, int>& d, WaveGroupPlan& p) : marks(m), declarations(d), plan(p) {}
    void handle(const GenerateBlockSymbol& block) { if (!block.isUninstantiated) visitDefault(block); }
    void handle(const VariableSymbol& symbol) { add(symbol); }
    void handle(const NetSymbol& symbol) { add(symbol); }
    void handle(const PortSymbol& symbol) { add(symbol); }
    void add(const Symbol& symbol) {
        int index = -1;
        for (auto* syntax = symbol.getSyntax(); syntax; syntax = syntax->parent) {
            if (declarations.contains(syntax)) { index = declarations.value(syntax); break; }
        }
        if (index < 0) return;
        const auto* scope = symbol.getParentScope();
        if (!scope || (scope->asSymbol().kind != SymbolKind::InstanceBody && scope->asSymbol().kind != SymbolKind::GenerateBlock)) return;
        auto& mark = marks[index];
        const auto path = simulatorObjectPath(QString::fromStdString(symbol.getHierarchicalPath()));
        mark.instantiated = true;
        if (emitted.contains(path)) return;
        emitted.insert(path);
        plan.objects << WaveGroupObject{mark.group, path, mark.file, mark.line};
    }
};
}
bool validWaveGroupName(const QString& name) {
    const auto letter = [](QChar c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (name.isEmpty() || !letter(name.front())) return false;
    for (const auto ch : name) if (!letter(ch) && (ch < '0' || ch > '9')) return false;
    return true;
}
QStringList waveGroupNames(const QHash<QString, QString>& sources) {
    QSet<QString> names;
    for (const auto& source : sources) {
        if (!source.contains("zs_group")) continue;
        const auto bytes = source.toUtf8(); SourceManager manager;
        const auto buffer = manager.assignText(std::string_view(bytes.constData(), size_t(bytes.size())));
        BumpAllocator allocator; Diagnostics diagnostics; Lexer lexer(buffer, allocator, diagnostics, manager);
        for (;;) {
            const auto token = lexer.lex();
            for (const auto& trivia : token.trivia()) if (trivia.kind == TriviaKind::BlockComment) {
                Mark mark;
                if (parseComment(text(trivia.getRawText()), mark) && mark.error.isEmpty()) names.insert(mark.group);
            }
            if (token.kind == TokenKind::EndOfFile) break;
        }
    }
    auto result = names.values(); result.sort(); return result;
}
WaveGroupPlan collectWaveGroups(Compilation& compilation, const SourceManager& manager) {
    WaveGroupPlan result;
    QList<Mark> marks;
    QMap<SourceLocation, QList<int>> byToken;
    QHash<const SyntaxNode*, int> declarations;
    for (const auto& tree : compilation.getSyntaxTrees()) {
        for (auto it = tree->root().tokens_begin(); it != tree->root().tokens_end(); ++it) {
            const auto token = *it;
            auto anchor = token.location();
            const auto trivia = token.trivia();
            for (auto piece = trivia.rbegin(); piece != trivia.rend(); ++piece) {
                const auto raw = piece->getRawText();
                if (auto explicitLocation = piece->getExplicitLocation()) anchor = *explicitLocation;
                else if (anchor.valid() && anchor.offset() >= raw.size()) anchor = anchor - raw.size();
                if (piece->kind != TriviaKind::BlockComment) continue;
                Mark mark;
                if (!parseComment(text(raw), mark)) continue;
                const auto loc = manager.getFullyExpandedLoc(anchor);
                mark.file = QString::fromStdString(manager.getFullPath(loc.buffer()).generic_string());
                mark.line = int(manager.getLineNumber(loc));
                byToken[token.location()] << marks.size(); marks << mark;
            }
        }
    }
    auto annotation = [&](const SyntaxNode& node) {
        const auto ids = byToken.value(node.getFirstToken().location());
        if (ids.isEmpty()) return -1;
        for (const auto id : ids) marks[id].assigned = true;
        if (ids.size() != 1) {
            for (const auto id : ids) marks[id].error = "More than one group annotation precedes this declaration.";
            return -1;
        }
        return marks[ids.front()].error.isEmpty() ? ids.front() : -1;
    };
    if (marks.isEmpty()) return result;
    auto bind = [&](const auto& node) {
        const int id = annotation(node);
        if (id >= 0) for (const auto* declarator : node.declarators) declarations.insert(declarator, id);
    };
    for (const auto& tree : compilation.getSyntaxTrees()) {
        AllSyntaxVisitor visitor([&](const SyntaxNode& node) {
            switch (node.kind) {
            case SyntaxKind::DataDeclaration: bind(node.as<DataDeclarationSyntax>()); break;
            case SyntaxKind::NetDeclaration: bind(node.as<NetDeclarationSyntax>()); break;
            case SyntaxKind::UserDefinedNetDeclaration: bind(node.as<UserDefinedNetDeclarationSyntax>()); break;
            case SyntaxKind::PortDeclaration: bind(node.as<PortDeclarationSyntax>()); break;
            case SyntaxKind::AnsiPortList: {
                int inherited = -1;
                for (const auto* member : node.as<AnsiPortListSyntax>().ports) {
                    if (member->kind != SyntaxKind::ImplicitAnsiPort) { inherited = -1; continue; }
                    const auto& port = member->as<ImplicitAnsiPortSyntax>();
                    // An inherited ANSI header contains only a zero-width placeholder.
                    const bool inherits = std::none_of(port.header->tokens_begin(), port.header->tokens_end(),
                        [](Token token) { return token.kind != TokenKind::Placeholder; });
                    if (!inherits) inherited = -1;
                    const auto ids = byToken.value(port.getFirstToken().location());
                    if (!ids.isEmpty()) inherited = annotation(port);
                    if (inherited >= 0) declarations.insert(port.declarator, inherited);
                }
                break;
            }
            default: break;
            }
        });
        visitor.visit(tree->root());
    }
    Collector collector(marks, declarations, result);
    for (const auto* instance : compilation.getRoot().topInstances) instance->visit(collector);
    for (const auto& mark : marks) {
        if (!mark.error.isEmpty()) result.diagnostics << location(mark) + mark.error;
        else if (!mark.assigned) result.diagnostics << location(mark) + "Place the annotation immediately before a port or internal signal declaration.";
        else if (!mark.instantiated) result.diagnostics << location(mark) + "No active module signal is instantiated for this declaration in the selected simulation.";
    }
    result.diagnostics.removeDuplicates();
    return result;
}
QString waveGroupsScript(const WaveGroupPlan& groups, WaveTheme theme) {
    QString script;
    for (const auto& diagnostic : groups.diagnostics) script += "puts " + tclWord(diagnostic) + '\n';
    if (groups.objects.isEmpty()) return script + "if {[llength $signals]} {add wave -r /*}\n";
    script += "set zs_visible [dict create]\nforeach zs_object $signals {\n"
              "    set zs_key $zs_object\n    if {[string first {sim:} $zs_key] == 0} {set zs_key [string range $zs_key 4 end]}\n    dict set zs_visible $zs_key $zs_object\n}\n"
              "set zs_grouped [dict create]\n";
    script += waveSignalColorsScript(theme, "[dict values $zs_visible]", true);
    const QString colorOptions =
        "set zs_colorOptions {}\nset zs_leaf [lindex [split $zs_object /] end]\n"
        "if {[dict exists $::simdock::waveLeafColors $zs_leaf]} {set zs_colorOptions [list -color [dict get $::simdock::waveLeafColors $zs_leaf]]}\n";
    for (const auto& object : groups.objects) {
        script += "set zs_path " + tclWord(object.path) + "\nset zs_group " + tclWord(object.group) + '\n';
        const auto origin = QString("%1:%2: zs_group %3: ").arg(object.file).arg(object.line).arg(object.group);
        script += "if {[dict exists $zs_visible $zs_path]} {\n"
                  "    set zs_object [dict get $zs_visible $zs_path]\n"
                  + colorOptions +
                  "    if {![dict exists $zs_grouped $zs_path]} {\n"
                  "        if {[catch {add wave -noupdate {*}$zs_colorOptions -group $zs_group $zs_object} zs_problem]} {\n"
                  "            puts [string cat " + tclWord(origin) + " $zs_problem]\n"
                  "        } else {dict set zs_grouped $zs_path 1}\n    }\n"
                  "} else {puts " + tclWord(origin + "Signal is not visible in Questa: " + object.path) + "}\n";
    }
    script += "dict for {zs_path zs_object} $zs_visible {\n" + colorOptions +
              "    if {![dict exists $zs_grouped $zs_path]} {add wave -noupdate {*}$zs_colorOptions $zs_object}\n}\nupdate\n";
    return script;
}
}
