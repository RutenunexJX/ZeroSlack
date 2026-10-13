#include "codetemplatecontextanalyzer.h"
#include "tsdocument.h"
#include "semanticindex.h"
#include "editorfileidentity.h"
#include <algorithm>
#include <cstring>

namespace {
bool is(TSNode node, const char* type)
{
    return !ts_node_is_null(node) && std::strcmp(ts_node_type(node), type) == 0;
}
TSNode moduleAt(TSNode node, uint32_t byte)
{
    if (byte < ts_node_start_byte(node) || byte >= ts_node_end_byte(node)) return {};
    if (is(node, "module_declaration")) return node;
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        const auto found = moduleAt(ts_node_named_child(node, i), byte);
        if (!ts_node_is_null(found)) return found;
    }
    return {};
}
}

CodeTemplateSignalContext CodeTemplateContextAnalyzer::analyze(
    const QString& text, int cursor, const QString& file)
{
    TSDocument document;
    document.setText(text);
    return analyze(document, cursor, file);
}

CodeTemplateSignalContext CodeTemplateContextAnalyzer::analyze(
    const TSDocument& document, int cursor, const QString& file)
{
    CodeTemplateSignalContext result;
    const auto& text = document.text().materialized();
    if (cursor < 0 || cursor >= text.size()) return result;
    const uint32_t byte = uint32_t(cursor) * 2u;
    const auto module = moduleAt(document.rootNode(), byte);
    if (ts_node_is_null(module)) return result;
    result.currentModuleFound = true;
    auto* index = SemanticIndex::getInstance();
    if (file.isEmpty() || !index->snapshotToken().isValid()
        || index->getCachedFileContent(file) != text) {
        return result;
    }
    QString moduleName;
    for (const auto& record : index->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Module)) {
        if (EditorFileIdentity::same(record.location.fileName, file)
            && record.location.position * 2 >= int(ts_node_start_byte(module))
            && record.location.position * 2 < int(ts_node_end_byte(module))) {
            if (!moduleName.isEmpty()) return result;
            moduleName = record.name;
        }
    }
    if (moduleName.isEmpty()) {
        return result;
    }
    auto records = index->getSymbolRecordsByOwner(moduleName);
    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.location.position < b.location.position;
    });
    for (const auto& record : records) {
        if (record.usageRole != SymbolTaxonomy::SymbolUsageRole::Declaration) continue;
        const auto& facts = record.presentation.defaultInfo;
        // Slang proves scalar candidates; their order does not assert a hardware role.
        if (!(record.presentation.directModuleVariable
              || (record.declarationKind == SymbolTaxonomy::DeclarationKind::Port
                  && record.collectorKind == SymbolTaxonomy::CollectorKind::PortInput))
            || !facts.available || !facts.integral || !facts.fixedSize
            || facts.unpackedArray || facts.interfaceType || facts.structuredLeavesAvailable
            || !facts.enumValues.isEmpty() || !facts.bitWidth
            || !EditorFileIdentity::same(record.location.fileName, file))
            continue;
        if (facts.bitWidth == 1) {
            if (result.clockName.isEmpty()) result.clockName = record.name;
            else if (result.resetName.isEmpty()) result.resetName = record.name;
        }
    }
    return result;
}
