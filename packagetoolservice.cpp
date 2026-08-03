#include "packagetoolservice.h"

#include "actionregistry.h"
#include "editorfileidentity.h"
#include "slangparseoptions.h"
#include "tsdocument.h"

#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace {
CodeTemplateSlot makeSlot(const QString& name, int start, int length)
{
    CodeTemplateSlot slot;
    slot.name = name;
    slot.start = start;
    slot.length = length;
    return slot;
}

int newlineCountBefore(const QString& text, int offset)
{
    int count = 0;
    const int limit = qBound(0, offset, text.size());
    for (int i = 0; i < limit; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++count;
    }
    return count;
}

int newlineCountInRange(const QString& text, int start, int length)
{
    int count = 0;
    const int rangeStart = qBound(0, start, text.size());
    const int rangeEnd = qBound(rangeStart, start + length, text.size());
    for (int i = rangeStart; i < rangeEnd; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++count;
    }
    return count;
}

void appendNeedleSlot(CodeTemplateItem& item,
                      const QString& name,
                      const QString& needle)
{
    const int start = item.insertText.indexOf(needle);
    if (start < 0)
        return;
    item.templateSlots.append(makeSlot(name, start, needle.size()));
}

QString adapterKeyForPackageToolKind(PackageToolKind kind)
{
    switch (kind) {
    case PackageToolKind::Parameter:
        return QStringLiteral("parameter");
    case PackageToolKind::Localparam:
        return QStringLiteral("localparam");
    case PackageToolKind::TypedefEnum:
        return QStringLiteral("typedefEnum");
    case PackageToolKind::TypedefStruct:
        return QStringLiteral("typedefStruct");
    case PackageToolKind::TypedefStructPacked:
        return QStringLiteral("typedefStructPacked");
    case PackageToolKind::Function:
        return QStringLiteral("function");
    }
    return QString();
}

bool packageToolKindForAdapterKey(const QString& adapterKey,
                                  PackageToolKind* kind)
{
    for (const PackageToolKind candidate :
         {PackageToolKind::Parameter,
          PackageToolKind::Localparam,
          PackageToolKind::TypedefEnum,
          PackageToolKind::TypedefStruct,
          PackageToolKind::TypedefStructPacked,
          PackageToolKind::Function}) {
        if (adapterKeyForPackageToolKind(candidate)
            != adapterKey) {
            continue;
        }
        if (kind)
            *kind = candidate;
        return true;
    }
    return false;
}

const ActionDescriptor* actionForPackageToolKind(
    PackageToolKind kind,
    const ActionAliasDescriptor** resultAlias = nullptr)
{
    const QString adapterKey =
        adapterKeyForPackageToolKind(kind);
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::PackageTools)) {
        if (!descriptor)
            continue;
        for (const ActionAliasDescriptor& packageAlias :
             descriptor->aliases) {
            if (packageAlias.surface
                    == ActionSurface::PackageTools
                && packageAlias.adapterKey == adapterKey) {
                if (resultAlias)
                    *resultAlias = &packageAlias;
                return descriptor;
            }
        }
    }
    if (resultAlias)
        *resultAlias = nullptr;
    return nullptr;
}

CodeTemplateItem makePackageTemplate(PackageToolKind kind,
                                     const QString& text)
{
    CodeTemplateItem item;
    const ActionAliasDescriptor* packageAlias = nullptr;
    const ActionDescriptor* descriptor =
        actionForPackageToolKind(kind, &packageAlias);
    item.commandToken =
        QStringLiteral("package:%1").arg(PackageToolService::idForKind(kind));
    item.label = packageAlias
        ? packageAlias->label : QString();
    item.description = packageAlias
        ? packageAlias->description : QString();
    item.insertText = text;
    item.defaultValue = text;
    if (descriptor) {
        item.actionId = descriptor->id;
        item.executionRoute = descriptor->executionRoute;
    }
    return item;
}

QString indentMultiline(const QString& text, const QString& indent)
{
    QString result;
    result.reserve(text.size() + indent.size() * 4);
    result += indent;
    for (const QChar ch : text) {
        result += ch;
        if (ch == QLatin1Char('\n'))
            result += indent;
    }
    return result;
}

bool inlineNodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node) || !expected)
        return false;
    const char* type = ts_node_type(node);
    return type && qstrcmp(type, expected) == 0;
}

int inlineNodeStartChar(TSNode node)
{
    return static_cast<int>(ts_node_start_byte(node) / 2u);
}

int inlineNodeEndChar(TSNode node)
{
    return static_cast<int>(ts_node_end_byte(node) / 2u);
}

QPair<int, int> containingLineRange(const QString& text,
                                    int startChar,
                                    int endChar)
{
    const int boundedStart =
        qBound(0, startChar, text.size());
    const int boundedEnd =
        qBound(boundedStart, endChar, text.size());
    const int previousNewline =
        boundedStart > 0
        ? text.lastIndexOf(
              QLatin1Char('\n'),
              boundedStart - 1)
        : -1;
    const int nextNewline =
        text.indexOf(QLatin1Char('\n'), boundedEnd);
    return {
        previousNewline < 0 ? 0 : previousNewline + 1,
        nextNewline < 0 ? text.size() : nextNewline,
    };
}

QString inlineNodeText(const QString& text, TSNode node)
{
    if (ts_node_is_null(node))
        return QString();
    const int start = inlineNodeStartChar(node);
    const int end = inlineNodeEndChar(node);
    if (start < 0 || end < start || end > text.size())
        return QString();
    return text.mid(start, end - start);
}

TSNode namedNodeOfTypeCovering(TSNode node,
                               const char* expected,
                               int startChar,
                               int endChar)
{
    if (ts_node_is_null(node)
        || inlineNodeStartChar(node) > startChar
        || inlineNodeEndChar(node) < endChar) {
        return TSNode{};
    }

    const uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (inlineNodeStartChar(child) > startChar
            || inlineNodeEndChar(child) < endChar) {
            continue;
        }
        const TSNode descendant =
            namedNodeOfTypeCovering(child,
                                   expected,
                                   startChar,
                                   endChar);
        if (!ts_node_is_null(descendant))
            return descendant;
    }

    return inlineNodeTypeIs(node, expected) ? node : TSNode{};
}

bool nodeIsInlineInsertionScope(TSNode node)
{
    return inlineNodeTypeIs(node, "source_file")
        || inlineNodeTypeIs(node, "module_declaration")
        || inlineNodeTypeIs(node, "interface_declaration")
        || inlineNodeTypeIs(node, "program_declaration")
        || inlineNodeTypeIs(node, "package_declaration")
        || inlineNodeTypeIs(node, "class_declaration")
        || inlineNodeTypeIs(node, "interface_class_declaration")
        || inlineNodeTypeIs(node, "checker_declaration")
        || inlineNodeTypeIs(node, "function_declaration")
        || inlineNodeTypeIs(node, "task_declaration")
        || inlineNodeTypeIs(node, "clocking_declaration")
        || inlineNodeTypeIs(node, "generate_block")
        || inlineNodeTypeIs(node, "seq_block")
        || inlineNodeTypeIs(node, "par_block");
}

TSNode nearestInlineInsertionScope(TSNode node)
{
    node = ts_node_parent(node);
    while (!ts_node_is_null(node)) {
        if (nodeIsInlineInsertionScope(node))
            return node;
        if (inlineNodeTypeIs(node, "ERROR") || ts_node_is_missing(node))
            return TSNode{};
        node = ts_node_parent(node);
    }
    return TSNode{};
}

bool syntaxErrorTouchesRange(TSNode node,
                             int startChar,
                             int endChar)
{
    if (ts_node_is_null(node)
        || inlineNodeEndChar(node) < startChar
        || inlineNodeStartChar(node) > endChar) {
        return false;
    }
    if (inlineNodeTypeIs(node, "ERROR") || ts_node_is_missing(node))
        return true;

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        if (syntaxErrorTouchesRange(ts_node_child(node, index),
                                    startChar,
                                    endChar)) {
            return true;
        }
    }
    return false;
}

TSNode firstDirectNamedChildOfType(TSNode node, const char* expected)
{
    const uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (inlineNodeTypeIs(child, expected))
            return child;
    }
    return TSNode{};
}

QString wildcardImportedPackage(const QString& text, TSNode item)
{
    if (!inlineNodeTypeIs(item, "package_import_item"))
        return QString();

    bool wildcard = false;
    const uint32_t childCount = ts_node_child_count(item);
    for (uint32_t index = 0; index < childCount; ++index) {
        if (inlineNodeTypeIs(ts_node_child(item, index), "*")) {
            wildcard = true;
            break;
        }
    }
    if (!wildcard)
        return QString();

    const uint32_t namedChildCount = ts_node_named_child_count(item);
    for (uint32_t index = 0; index < namedChildCount; ++index) {
        const TSNode child = ts_node_named_child(item, index);
        if (inlineNodeTypeIs(child, "simple_identifier")
            || inlineNodeTypeIs(child, "escaped_identifier")) {
            return inlineNodeText(text, child).trimmed();
        }
    }
    return QString();
}

void collectWildcardPackageImports(TSNode node,
                                   TSNode scope,
                                   const QString& text,
                                   int insertedStart,
                                   int insertedEnd,
                                   QSet<QString>* packages)
{
    if (!packages || ts_node_is_null(node))
        return;
    if (!ts_node_eq(node, scope) && nodeIsInlineInsertionScope(node))
        return;

    if (inlineNodeTypeIs(node, "package_import_declaration")) {
        if (inlineNodeStartChar(node) == insertedStart
            && inlineNodeEndChar(node) == insertedEnd) {
            return;
        }
        const uint32_t childCount = ts_node_named_child_count(node);
        for (uint32_t index = 0; index < childCount; ++index) {
            const QString packageName =
                wildcardImportedPackage(text,
                                        ts_node_named_child(node, index));
            if (!packageName.isEmpty())
                packages->insert(packageName);
        }
        return;
    }

    const uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        collectWildcardPackageImports(ts_node_named_child(node, index),
                                      scope,
                                      text,
                                      insertedStart,
                                      insertedEnd,
                                      packages);
    }
}

void collectQuotedIncludePaths(TSNode node,
                               const QString& text,
                               QSet<QString>* includePaths)
{
    if (!includePaths || ts_node_is_null(node))
        return;

    if (inlineNodeTypeIs(node, "include_compiler_directive")) {
        const TSNode quoted =
            firstDirectNamedChildOfType(node, "quoted_string");
        const QString quotedText = inlineNodeText(text, quoted);
        if (quotedText.size() >= 2
            && quotedText.startsWith(QLatin1Char('"'))
            && quotedText.endsWith(QLatin1Char('"'))) {
            includePaths->insert(
                QDir::cleanPath(
                    QDir::fromNativeSeparators(
                        quotedText.mid(1, quotedText.size() - 2))));
        }
        return;
    }

    const uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        collectQuotedIncludePaths(ts_node_named_child(node, index),
                                  text,
                                  includePaths);
    }
}

bool inlineRangeIsEditable(const TSDocument& document,
                           int startChar,
                           int endChar)
{
    if (startChar < 0
        || endChar < startChar
        || endChar > document.text().size()) {
        return false;
    }
    if (document.text().isEmpty())
        return true;

    const int startProbe =
        qBound(0, startChar, document.text().size() - 1);
    const int endProbe =
        qBound(0,
               endChar > startChar ? endChar - 1 : startChar,
               document.text().size() - 1);
    return !document.isCommentAt(startProbe)
        && !document.isStringAt(startProbe)
        && !document.isCommentAt(endProbe)
        && !document.isStringAt(endProbe);
}

QString packageImportReplacementText(const QString& packageName)
{
    QString identifier = packageName.trimmed();
    if (identifier.startsWith(QLatin1Char('\\')))
        identifier.append(QLatin1Char(' '));
    return QStringLiteral("import %1::*;").arg(identifier);
}

QString normalizedPathIdentity(const QString& fileName)
{
    return EditorFileIdentity::lookupKey(fileName);
}

QString normalizedIncludePath(QString includePath)
{
    includePath =
        QDir::cleanPath(QDir::fromNativeSeparators(includePath.trimmed()));
#ifdef Q_OS_WIN
    return includePath.toCaseFolded();
#else
    return includePath;
#endif
}

bool relativePathStaysInsideRoot(const QString& relativePath)
{
    const QString clean =
        QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    return !clean.isEmpty()
        && !QDir::isAbsolutePath(clean)
        && clean != QStringLiteral("..")
        && !clean.startsWith(QStringLiteral("../"));
}

QSet<QString> includePathResolutionIdentities(
    const QString& includePath,
    const QStringList& includeDirs)
{
    QSet<QString> identities;
    for (const QString& includeDir : includeDirs) {
        const QFileInfo candidate(
            QDir(includeDir).absoluteFilePath(includePath));
        if (!candidate.exists() || !candidate.isFile())
            continue;
        const QString identity =
            normalizedPathIdentity(candidate.absoluteFilePath());
        if (!identity.isEmpty())
            identities.insert(identity);
    }
    return identities;
}

int pathComponentCount(const QString& path)
{
    return path.count(QLatin1Char('/')) + 1;
}

bool shorterUniqueIncludePath(const QString& candidate,
                              const QString& best)
{
    if (best.isEmpty())
        return true;
    if (candidate.size() != best.size())
        return candidate.size() < best.size();
    const int candidateComponents = pathComponentCount(candidate);
    const int bestComponents = pathComponentCount(best);
    if (candidateComponents != bestComponents)
        return candidateComponents < bestComponents;
    const int folded =
        QString::compare(candidate, best, Qt::CaseInsensitive);
    return folded < 0 || (folded == 0 && candidate < best);
}
} // namespace

QList<PackageToolKind> PackageToolService::toolOrder()
{
    QList<PackageToolKind> result;
    for (const ActionDescriptor* descriptor :
         actionDescriptorsForSurface(
             ActionSurface::PackageTools)) {
        if (!descriptor)
            continue;
        const ActionAliasDescriptor* packageAlias =
            findActionAlias(*descriptor,
                            ActionSurface::PackageTools);
        PackageToolKind kind;
        if (packageAlias
            && packageToolKindForAdapterKey(
                packageAlias->adapterKey, &kind)) {
            result.append(kind);
        }
    }
    return result;
}

QString PackageToolService::actionIdForKind(PackageToolKind kind)
{
    const ActionDescriptor* descriptor =
        actionForPackageToolKind(kind);
    return descriptor ? descriptor->id : QString();
}

QString PackageToolService::idForKind(PackageToolKind kind)
{
    const ActionAliasDescriptor* packageAlias = nullptr;
    actionForPackageToolKind(kind, &packageAlias);
    return packageAlias ? packageAlias->token : QString();
}

QString PackageToolService::labelForKind(PackageToolKind kind)
{
    const ActionAliasDescriptor* packageAlias = nullptr;
    actionForPackageToolKind(kind, &packageAlias);
    return packageAlias ? packageAlias->label : QString();
}

CodeTemplateItem PackageToolService::templateForKind(PackageToolKind kind) const
{
    CodeTemplateItem item;
    switch (kind) {
    case PackageToolKind::Parameter:
        item = makePackageTemplate(
            kind,
            QStringLiteral("parameter int PARAM = 0;"));
        appendNeedleSlot(item, QStringLiteral("type"), QStringLiteral("int"));
        appendNeedleSlot(item, QStringLiteral("name"), QStringLiteral("PARAM"));
        appendNeedleSlot(item, QStringLiteral("value"), QStringLiteral("0"));
        return item;
    case PackageToolKind::Localparam:
        item = makePackageTemplate(
            kind,
            QStringLiteral("localparam int LOCAL_PARAM = 0;"));
        appendNeedleSlot(item, QStringLiteral("type"), QStringLiteral("int"));
        appendNeedleSlot(item,
                         QStringLiteral("name"),
                         QStringLiteral("LOCAL_PARAM"));
        appendNeedleSlot(item, QStringLiteral("value"), QStringLiteral("0"));
        return item;
    case PackageToolKind::TypedefEnum:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef enum logic [1:0] {\n"
                           "    IDLE,\n"
                           "    BUSY\n"
                           "} state_e;"));
        appendNeedleSlot(item,
                         QStringLiteral("state_items"),
                         QStringLiteral("IDLE,\n    BUSY"));
        appendNeedleSlot(item,
                         QStringLiteral("enum_name"),
                         QStringLiteral("state_e"));
        return item;
    case PackageToolKind::TypedefStruct:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef struct {\n"
                           "    logic field_name;\n"
                           "} type_name_t;"));
        appendNeedleSlot(item,
                         QStringLiteral("field_type"),
                         QStringLiteral("logic"));
        appendNeedleSlot(item,
                         QStringLiteral("field_name"),
                         QStringLiteral("field_name"));
        appendNeedleSlot(item,
                         QStringLiteral("type_name"),
                         QStringLiteral("type_name_t"));
        return item;
    case PackageToolKind::TypedefStructPacked:
        item = makePackageTemplate(
            kind,
            QStringLiteral("typedef struct packed {\n"
                           "    logic field_name;\n"
                           "} type_name_t;"));
        appendNeedleSlot(item,
                         QStringLiteral("field_type"),
                         QStringLiteral("logic"));
        appendNeedleSlot(item,
                         QStringLiteral("field_name"),
                         QStringLiteral("field_name"));
        appendNeedleSlot(item,
                         QStringLiteral("type_name"),
                         QStringLiteral("type_name_t"));
        return item;
    case PackageToolKind::Function:
        item = makePackageTemplate(
            kind,
            QStringLiteral("function automatic int function_name(input int arg);\n"
                           "    return '0;\n"
                           "endfunction"));
        appendNeedleSlot(item, QStringLiteral("return_type"), QStringLiteral("int"));
        appendNeedleSlot(item,
                         QStringLiteral("function_name"),
                         QStringLiteral("function_name"));
        appendNeedleSlot(item,
                         QStringLiteral("arguments"),
                         QStringLiteral("input int arg"));
        appendNeedleSlot(item, QStringLiteral("body"), QStringLiteral("return '0;"));
        return item;
    }
    return item;
}

CodeTemplateItem PackageToolService::templateForInsertion(
    PackageToolKind kind,
    const QString& lineIndent,
    bool insertAfterLine) const
{
    const CodeTemplateItem base = templateForKind(kind);

    CodeTemplateItem expanded = base;
    const QString leading = insertAfterLine ? QStringLiteral("\n") : QString();
    const QString trailing = insertAfterLine ? QString() : QStringLiteral("\n");
    expanded.insertText =
        leading + indentMultiline(base.insertText, lineIndent) + trailing;
    expanded.defaultValue = expanded.insertText;
    expanded.templateSlots.clear();

    for (const CodeTemplateSlot& slot : base.templateSlots) {
        const int indentsBeforeSlot =
            1 + newlineCountBefore(base.insertText, slot.start);
        const int indentsInsideSlot =
            newlineCountInRange(base.insertText, slot.start, slot.length);
        expanded.templateSlots.append(
            makeSlot(slot.name,
                     leading.size() + slot.start
                         + lineIndent.size() * indentsBeforeSlot,
                     slot.length + lineIndent.size() * indentsInsideSlot));
    }

    return expanded;
}

PackageImportSite PackageToolService::analyzePackageImportSite(
    const QString& documentText,
    int replacementStart,
    int replacementEnd)
{
    PackageImportSite site;
    TSDocument originalDocument;
    originalDocument.setText(documentText);
    if (!inlineRangeIsEditable(originalDocument,
                               replacementStart,
                               replacementEnd)) {
        site.failureMessage =
            QStringLiteral("Package import is not valid at this location.");
        return site;
    }

    const QString probeText =
        QStringLiteral("import __zeroslack_package_probe::*;");
    QString previewText = documentText;
    previewText.replace(replacementStart,
                        replacementEnd - replacementStart,
                        probeText);

    TSDocument previewDocument;
    previewDocument.setText(previewText);
    const int insertedEnd = replacementStart + probeText.size();
    const auto probeLine =
        containingLineRange(previewText,
                            replacementStart,
                            insertedEnd);
    const TSNode importNode =
        namedNodeOfTypeCovering(previewDocument.rootNode(),
                               "package_import_declaration",
                               replacementStart,
                               insertedEnd);
    if (ts_node_is_null(importNode)
        || ts_node_has_error(importNode)
        || syntaxErrorTouchesRange(previewDocument.rootNode(),
                                   probeLine.first,
                                   probeLine.second)) {
        site.failureMessage =
            QStringLiteral("Package import is not valid at this location.");
        return site;
    }

    const TSNode scope = nearestInlineInsertionScope(importNode);
    if (ts_node_is_null(scope)) {
        site.failureMessage =
            QStringLiteral("Package import has no valid syntax scope.");
        return site;
    }

    QSet<QString> importedPackages;
    collectWildcardPackageImports(scope,
                                  scope,
                                  previewText,
                                  replacementStart,
                                  insertedEnd,
                                  &importedPackages);

    site.valid = true;
    site.enclosingPackageName =
        previewDocument.enclosingPackageName(replacementStart).trimmed();
    site.wildcardImportedPackages = importedPackages.values();
    site.wildcardImportedPackages.sort(Qt::CaseSensitive);
    return site;
}

StructuredInlineInsertionPlan PackageToolService::packageImportPlan(
    const QString& documentText,
    int replacementStart,
    int replacementEnd,
    const QString& packageName)
{
    StructuredInlineInsertionPlan plan;
    plan.replacementStart = replacementStart;
    plan.replacementEnd = replacementEnd;

    const PackageImportSite site =
        analyzePackageImportSite(documentText,
                                 replacementStart,
                                 replacementEnd);
    if (!site.valid) {
        plan.status = StructuredInlineInsertionStatus::InvalidLocation;
        plan.failureMessage = site.failureMessage;
        return plan;
    }

    const QString normalizedPackageName = packageName.trimmed();
    if (normalizedPackageName.isEmpty()
        || normalizedPackageName == site.enclosingPackageName) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage = normalizedPackageName.isEmpty()
            ? QStringLiteral("Package name is empty.")
            : QStringLiteral("A package cannot import itself.");
        return plan;
    }
    if (site.wildcardImportedPackages.contains(normalizedPackageName)) {
        plan.status = StructuredInlineInsertionStatus::Duplicate;
        plan.failureMessage =
            QStringLiteral("Package is already imported in this scope.");
        return plan;
    }

    const QString replacementText =
        packageImportReplacementText(normalizedPackageName);
    QString previewText = documentText;
    previewText.replace(replacementStart,
                        replacementEnd - replacementStart,
                        replacementText);
    TSDocument previewDocument;
    previewDocument.setText(previewText);
    const int insertedEnd = replacementStart + replacementText.size();
    const auto replacementLine =
        containingLineRange(previewText,
                            replacementStart,
                            insertedEnd);
    const TSNode importNode =
        namedNodeOfTypeCovering(previewDocument.rootNode(),
                               "package_import_declaration",
                               replacementStart,
                               insertedEnd);
    if (ts_node_is_null(importNode)
        || ts_node_has_error(importNode)
        || syntaxErrorTouchesRange(previewDocument.rootNode(),
                                   replacementLine.first,
                                   replacementLine.second)) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage =
            QStringLiteral("Package name does not form a valid import.");
        return plan;
    }

    QString parsedPackageName;
    const uint32_t childCount = ts_node_named_child_count(importNode);
    for (uint32_t index = 0; index < childCount; ++index) {
        parsedPackageName =
            wildcardImportedPackage(previewText,
                                    ts_node_named_child(importNode, index));
        if (!parsedPackageName.isEmpty())
            break;
    }
    if (parsedPackageName != normalizedPackageName) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage =
            QStringLiteral("Package name does not form a valid import.");
        return plan;
    }

    plan.status = StructuredInlineInsertionStatus::Ok;
    plan.replacementText = replacementText;
    plan.failureMessage.clear();
    return plan;
}

StructuredInlineInsertionPlan PackageToolService::headerIncludePlan(
    const QString& documentText,
    int replacementStart,
    int replacementEnd,
    const QString& includePath)
{
    StructuredInlineInsertionPlan plan;
    plan.replacementStart = replacementStart;
    plan.replacementEnd = replacementEnd;

    TSDocument originalDocument;
    originalDocument.setText(documentText);
    if (!inlineRangeIsEditable(originalDocument,
                               replacementStart,
                               replacementEnd)) {
        plan.status = StructuredInlineInsertionStatus::InvalidLocation;
        plan.failureMessage =
            QStringLiteral("Include is not valid at this location.");
        return plan;
    }

    const QString cleanIncludePath =
        QDir::cleanPath(QDir::fromNativeSeparators(includePath.trimmed()));
    if (cleanIncludePath.isEmpty()
        || cleanIncludePath == QStringLiteral(".")) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage = QStringLiteral("Include path is empty.");
        return plan;
    }

    QSet<QString> existingIncludePaths;
    collectQuotedIncludePaths(originalDocument.rootNode(),
                              documentText,
                              &existingIncludePaths);
    const QString includeKey = normalizedIncludePath(cleanIncludePath);
    for (const QString& existingPath : std::as_const(existingIncludePaths)) {
        if (normalizedIncludePath(existingPath) == includeKey) {
            plan.status = StructuredInlineInsertionStatus::Duplicate;
            plan.failureMessage =
                QStringLiteral("Header is already included.");
            return plan;
        }
    }

    const QString replacementText =
        QStringLiteral("`include \"%1\"").arg(cleanIncludePath);
    QString previewText = documentText;
    previewText.replace(replacementStart,
                        replacementEnd - replacementStart,
                        replacementText);
    TSDocument previewDocument;
    previewDocument.setText(previewText);
    const int insertedEnd = replacementStart + replacementText.size();
    const auto replacementLine =
        containingLineRange(previewText,
                            replacementStart,
                            insertedEnd);
    const TSNode includeNode =
        namedNodeOfTypeCovering(previewDocument.rootNode(),
                               "include_compiler_directive",
                               replacementStart,
                               insertedEnd);
    if (ts_node_is_null(includeNode)
        || ts_node_has_error(includeNode)
        || syntaxErrorTouchesRange(previewDocument.rootNode(),
                                   replacementLine.first,
                                   replacementLine.second)) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage =
            QStringLiteral("Include path does not form a valid directive.");
        return plan;
    }

    const TSNode quoted =
        firstDirectNamedChildOfType(includeNode, "quoted_string");
    const QString quotedText = inlineNodeText(previewText, quoted);
    if (quotedText.size() < 2
        || !quotedText.startsWith(QLatin1Char('"'))
        || !quotedText.endsWith(QLatin1Char('"'))
        || quotedText.mid(1, quotedText.size() - 2) != cleanIncludePath) {
        plan.status = StructuredInlineInsertionStatus::InvalidTarget;
        plan.failureMessage =
            QStringLiteral("Include path does not form a valid directive.");
        return plan;
    }

    plan.status = StructuredInlineInsertionStatus::Ok;
    plan.replacementText = replacementText;
    plan.failureMessage.clear();
    return plan;
}

QList<HeaderIncludeCandidate> PackageToolService::headerIncludeCandidates(
    const QStringList& headerFiles,
    const QString& currentFile,
    const QStringList& configuredIncludeDirs)
{
    QList<HeaderIncludeCandidate> result;
    const QStringList includeDirs =
        slang_parse_options::effectiveIncludeDirsForFile(
            currentFile,
            configuredIncludeDirs);
    if (includeDirs.isEmpty())
        return result;

    const QString currentIdentity = normalizedPathIdentity(currentFile);
    QSet<QString> emittedIdentities;
    for (const QString& headerFile : headerFiles) {
        const QFileInfo headerInfo(headerFile);
        const QString suffix = headerInfo.suffix().toLower();
        if (!headerInfo.exists()
            || !headerInfo.isFile()
            || (suffix != QStringLiteral("vh")
                && suffix != QStringLiteral("svh"))) {
            continue;
        }

        const QString headerIdentity =
            normalizedPathIdentity(headerInfo.absoluteFilePath());
        if (headerIdentity.isEmpty()
            || headerIdentity == currentIdentity
            || emittedIdentities.contains(headerIdentity)) {
            continue;
        }

        QStringList relativeCandidates;
        for (const QString& includeDir : includeDirs) {
            const QString relative =
                QDir::fromNativeSeparators(
                    QDir(includeDir).relativeFilePath(
                        headerInfo.absoluteFilePath()));
            if (relativePathStaysInsideRoot(relative))
                relativeCandidates.append(QDir::cleanPath(relative));
        }
        relativeCandidates.removeDuplicates();

        QString bestPath;
        for (const QString& candidate : std::as_const(relativeCandidates)) {
            const QSet<QString> resolutions =
                includePathResolutionIdentities(candidate, includeDirs);
            if (resolutions.size() != 1
                || !resolutions.contains(headerIdentity)) {
                continue;
            }
            if (shorterUniqueIncludePath(candidate, bestPath))
                bestPath = candidate;
        }
        if (bestPath.isEmpty())
            continue;

        emittedIdentities.insert(headerIdentity);
        HeaderIncludeCandidate candidate;
        candidate.absoluteFilePath =
            QDir::fromNativeSeparators(headerInfo.absoluteFilePath());
        candidate.includePath = bestPath;
        result.append(candidate);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const HeaderIncludeCandidate& left,
           const HeaderIncludeCandidate& right) {
            const int folded =
                QString::compare(left.includePath,
                                 right.includePath,
                                 Qt::CaseInsensitive);
            if (folded != 0)
                return folded < 0;
            return left.includePath < right.includePath;
        });
    return result;
}
