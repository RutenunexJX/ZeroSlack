#include "commodeservice.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
QString normalizedComFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QSet<QString> normalizedFileSet(const QStringList& files)
{
    QSet<QString> result;
    for (const QString& fileName : files) {
        const QString normalized = normalizedComFileName(fileName);
        if (!normalized.isEmpty())
            result.insert(normalized);
    }
    return result;
}

QString displayPathForFile(const QString& fileName, const QString& root)
{
    if (fileName.isEmpty())
        return QString();

    const QString normalizedFile = normalizedComFileName(fileName);
    const QString normalizedRoot = normalizedComFileName(root);
    QString display = normalizedFile;
    if (!normalizedRoot.isEmpty()
        && (normalizedFile == normalizedRoot
            || normalizedFile.startsWith(normalizedRoot + QLatin1Char('/')))) {
        display = QDir(normalizedRoot).relativeFilePath(normalizedFile);
    }
    return QDir::toNativeSeparators(display);
}

bool fuzzySubsequenceMatch(const QString& haystack,
                           const QString& needle,
                           int* gapPenalty)
{
    int searchFrom = 0;
    int gaps = 0;
    int lastMatch = -1;
    for (const QChar needleChar : needle) {
        bool matched = false;
        for (int i = searchFrom; i < haystack.size(); ++i) {
            if (haystack.at(i) != needleChar)
                continue;
            if (lastMatch >= 0)
                gaps += i - lastMatch - 1;
            lastMatch = i;
            searchFrom = i + 1;
            matched = true;
            break;
        }
        if (!matched)
            return false;
    }
    if (gapPenalty)
        *gapPenalty = gaps;
    return true;
}

int fuzzyScore(const QString& text, const QString& filter)
{
    const QString needle = filter.trimmed().toCaseFolded();
    if (needle.isEmpty())
        return 1;

    const QString haystack = text.toCaseFolded();
    if (haystack == needle)
        return 1000;
    if (haystack.startsWith(needle))
        return 900 - qMin(100, haystack.size() - needle.size());
    const int containsAt = haystack.indexOf(needle);
    if (containsAt >= 0)
        return 800 - qMin(200, containsAt);

    int gapPenalty = 0;
    if (fuzzySubsequenceMatch(haystack, needle, &gapPenalty))
        return 600 - qMin(300, gapPenalty);

    return 0;
}

int moduleRecordLastLine(const QList<SemanticSymbolRecord>& modules,
                         int moduleIndex,
                         const SemanticIndexSnapshot& snapshot)
{
    if (moduleIndex < 0 || moduleIndex >= modules.size())
        return -1;

    const SemanticSymbolRecord& module = modules.at(moduleIndex);
    if (module.location.endLine > module.location.startLine)
        return module.location.endLine - 1;
    if (module.location.endLine == module.location.startLine)
        return module.location.endLine;
    if (moduleIndex + 1 < modules.size())
        return modules.at(moduleIndex + 1).location.startLine - 1;

    const QString content =
        snapshot.getCachedFileContent(module.location.fileName);
    if (!content.isEmpty())
        return content.count(QLatin1Char('\n')) + 1;

    return module.location.startLine;
}

bool moduleContainsLine(const SemanticSymbolRecord& module,
                        int line,
                        int lastLine)
{
    return module.location.startLine > 0
        && line >= module.location.startLine
        && (lastLine <= 0 || line <= lastLine);
}

bool isWorkspaceScopedRecord(const SemanticSymbolRecord& record,
                             const QSet<QString>& scopedFiles,
                             bool useScope)
{
    if (!useScope)
        return true;
    const QString normalizedFile =
        normalizedComFileName(record.location.fileName);
    return scopedFiles.contains(normalizedFile);
}

QString kindLabelForDeclaration(SymbolTaxonomy::DeclarationKind kind)
{
    switch (kind) {
    case SymbolTaxonomy::DeclarationKind::Module:
        return QStringLiteral("module");
    case SymbolTaxonomy::DeclarationKind::Package:
        return QStringLiteral("package");
    case SymbolTaxonomy::DeclarationKind::Parameter:
        return QStringLiteral("parameter");
    case SymbolTaxonomy::DeclarationKind::Localparam:
        return QStringLiteral("localparam");
    default:
        return QStringLiteral("symbol");
    }
}

ComModePickerItemKind pickerKindForDeclaration(
    SymbolTaxonomy::DeclarationKind kind)
{
    switch (kind) {
    case SymbolTaxonomy::DeclarationKind::Package:
        return ComModePickerItemKind::Package;
    case SymbolTaxonomy::DeclarationKind::Parameter:
        return ComModePickerItemKind::Parameter;
    case SymbolTaxonomy::DeclarationKind::Localparam:
        return ComModePickerItemKind::Localparam;
    case SymbolTaxonomy::DeclarationKind::Module:
    default:
        return ComModePickerItemKind::Module;
    }
}

ComModePickerItem pickerItemForRecord(const SemanticSymbolRecord& record,
                                      const ProjectSnapshot& project,
                                      int score)
{
    ComModePickerItem item;
    item.kind = pickerKindForDeclaration(record.declarationKind);
    item.name = record.name;
    item.kindLabel = kindLabelForDeclaration(record.declarationKind);
    item.scopeName = record.owner.name;
    item.filePath = record.location.fileName;
    item.displayPath = displayPathForFile(record.location.fileName,
                                          project.workspaceRoot);
    item.line = record.location.startLine;
    item.column = record.location.startColumn;
    item.score = score;
    item.symbolRecord = record;
    return item;
}

QString lineTextFromContent(const QString& content, int line)
{
    if (content.isEmpty() || line <= 0)
        return QString();

    int currentLine = 1;
    int start = 0;
    for (int i = 0; i <= content.size(); ++i) {
        if (i < content.size() && content.at(i) != QLatin1Char('\n'))
            continue;
        if (currentLine == line)
            return content.mid(start, i - start);
        ++currentLine;
        start = i + 1;
    }
    return QString();
}

QString firstPackedRangeText(const QString& text)
{
    int open = -1;
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('[')) {
            if (depth == 0)
                open = i;
            ++depth;
            continue;
        }
        if (ch != QLatin1Char(']') || depth <= 0)
            continue;
        --depth;
        if (depth == 0 && open >= 0)
            return text.mid(open, i - open + 1).simplified();
    }
    return QString();
}

QString signalWidthLabel(const SemanticSymbolRecord& record,
                         const SemanticIndexSnapshot& snapshot)
{
    QString width = firstPackedRangeText(record.type.rawTypeText);
    if (!width.isEmpty())
        return width;

    const QString content =
        snapshot.getCachedFileContent(record.location.fileName);
    const QString line = lineTextFromContent(content, record.location.startLine);
    if (!line.isEmpty()) {
        const int nameAt = line.indexOf(record.name);
        width = firstPackedRangeText(nameAt >= 0 ? line.left(nameAt) : line);
        if (!width.isEmpty())
            return width;
    }

    return QStringLiteral("scalar");
}

QString signalKindLabel(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    const QString label = SymbolTaxonomy::symbolTypeLabel(metadata);
    return label.isEmpty() ? QStringLiteral("signal") : label;
}

ComModePickerItem signalPickerItemForRecord(
    const SemanticSymbolRecord& record,
    const ProjectSnapshot& project,
    const SemanticIndexSnapshot& snapshot,
    int score)
{
    ComModePickerItem item = pickerItemForRecord(record, project, score);
    item.kind = ComModePickerItemKind::Signal;
    item.kindLabel = signalKindLabel(record);
    item.detailLabel = signalWidthLabel(record, snapshot);
    return item;
}

QList<ComModePickerItem> sortedLimitedPickerItems(
    QList<ComModePickerItem> items,
    int maxResults)
{
    std::stable_sort(items.begin(), items.end(),
                     [](const ComModePickerItem& left,
                        const ComModePickerItem& right) {
        if (left.score != right.score)
            return left.score > right.score;
        if (left.name != right.name)
            return left.name < right.name;
        if (left.scopeName != right.scopeName)
            return left.scopeName < right.scopeName;
        if (left.displayPath != right.displayPath)
            return left.displayPath < right.displayPath;
        return left.line < right.line;
    });

    if (maxResults >= 0 && items.size() > maxResults)
        items = items.mid(0, maxResults);
    return items;
}

int pickerRecordScore(const SemanticSymbolRecord& record,
                      const QString& displayPath,
                      const QString& filter)
{
    const int nameScore = fuzzyScore(record.name, filter);
    const int pathScore = fuzzyScore(displayPath, filter);
    const int scopeScore = fuzzyScore(record.owner.name, filter);
    return qMax(nameScore, qMax(pathScore, scopeScore));
}

QList<ComModePickerItem> declarationPickerItems(
    const ComModePickerQuery& query,
    SymbolTaxonomy::DeclarationKind declarationKind)
{
    QList<ComModePickerItem> result;
    if (!query.snapshot)
        return result;

    const QSet<QString> scopedFiles =
        query.project.isOpen()
            ? normalizedFileSet(query.project.systemVerilogFiles)
            : QSet<QString>();
    const bool useScope = query.project.isOpen() && !scopedFiles.isEmpty();

    for (const SemanticSymbolRecord& record :
         query.snapshot->getSymbolRecordsByDeclarationKind(declarationKind)) {
        if (record.name.isEmpty() || record.location.fileName.isEmpty())
            continue;
        if (!isWorkspaceScopedRecord(record, scopedFiles, useScope))
            continue;

        const QString displayPath =
            displayPathForFile(record.location.fileName,
                               query.project.workspaceRoot);
        const int score = pickerRecordScore(record, displayPath, query.filter);
        if (score <= 0)
            continue;
        result.append(pickerItemForRecord(record, query.project, score));
    }
    return sortedLimitedPickerItems(std::move(result), query.maxResults);
}

bool sameComFile(const QString& left, const QString& right)
{
    return normalizedComFileName(left) == normalizedComFileName(right);
}

int topScopeRecordLastLine(const QList<SemanticSymbolRecord>& scopes,
                           int scopeIndex,
                           const SemanticIndexSnapshot& snapshot)
{
    if (scopeIndex < 0 || scopeIndex >= scopes.size())
        return -1;

    const SemanticSymbolRecord& scope = scopes.at(scopeIndex);
    if (scope.location.endLine > scope.location.startLine)
        return scope.location.endLine - 1;
    if (scope.location.endLine == scope.location.startLine)
        return scope.location.endLine;
    if (scopeIndex + 1 < scopes.size())
        return scopes.at(scopeIndex + 1).location.startLine - 1;

    const QString content =
        snapshot.getCachedFileContent(scope.location.fileName);
    if (!content.isEmpty())
        return content.count(QLatin1Char('\n')) + 1;

    return scope.location.startLine;
}

bool isModuleOrPackageScope(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Package;
}

SemanticSymbolRecord currentParameterScopeRecord(
    const ComModeScopedPickerQuery& query)
{
    if (!query.snapshot || query.fileName.isEmpty() || query.currentLine <= 0)
        return {};

    QList<SemanticSymbolRecord> scopes;
    for (const SemanticSymbolRecord& record :
         query.snapshot->getSymbolRecords(query.fileName)) {
        if (isModuleOrPackageScope(record))
            scopes.append(record);
    }
    std::stable_sort(scopes.begin(), scopes.end(),
                     [](const SemanticSymbolRecord& left,
                        const SemanticSymbolRecord& right) {
        return left.location.startLine < right.location.startLine;
    });

    for (int i = 0; i < scopes.size(); ++i) {
        const SemanticSymbolRecord& scope = scopes.at(i);
        const int lastLine = topScopeRecordLastLine(scopes, i, *query.snapshot);
        if (!moduleContainsLine(scope, query.currentLine, lastLine))
            continue;
        if (scope.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            && !query.currentModuleName.isEmpty()
            && scope.name != query.currentModuleName) {
            continue;
        }
        return scope;
    }

    return {};
}

bool isParameterDeclaration(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam;
}

bool isCurrentModuleSignalDeclaration(const SemanticSymbolRecord& record)
{
    if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Signal)
        return false;
    if (record.collectorKind != SymbolTaxonomy::CollectorKind::Wire
        && record.collectorKind != SymbolTaxonomy::CollectorKind::Reg
        && record.collectorKind != SymbolTaxonomy::CollectorKind::Logic) {
        return false;
    }
    return SymbolTaxonomy::isSignalDeclaration(
        semanticMetadataForSymbolRecord(record));
}

SemanticSymbolRecord currentModuleScopeRecord(
    const ComModeScopedPickerQuery& query)
{
    if (!query.snapshot || query.fileName.isEmpty() || query.currentLine <= 0)
        return {};

    QList<SemanticSymbolRecord> modules;
    for (const SemanticSymbolRecord& record :
         query.snapshot->getSymbolRecords(query.fileName)) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module)
            modules.append(record);
    }
    std::stable_sort(modules.begin(), modules.end(),
                     [](const SemanticSymbolRecord& left,
                        const SemanticSymbolRecord& right) {
        return left.location.startLine < right.location.startLine;
    });

    for (int i = 0; i < modules.size(); ++i) {
        const SemanticSymbolRecord& module = modules.at(i);
        const int lastLine = topScopeRecordLastLine(modules,
                                                    i,
                                                    *query.snapshot);
        if (!moduleContainsLine(module, query.currentLine, lastLine))
            continue;
        if (!query.currentModuleName.isEmpty()
            && module.name != query.currentModuleName) {
            continue;
        }
        return module;
    }
    return {};
}
} // namespace

QList<ComModePickerItem> ComModeService::moduleItems(
    const ComModePickerQuery& query) const
{
    return declarationPickerItems(query,
                                  SymbolTaxonomy::DeclarationKind::Module);
}

QList<ComModePickerItem> ComModeService::packageItems(
    const ComModePickerQuery& query) const
{
    return declarationPickerItems(query,
                                  SymbolTaxonomy::DeclarationKind::Package);
}

ComModeScopedPickerResult ComModeService::parameterItems(
    const ComModeScopedPickerQuery& query) const
{
    ComModeScopedPickerResult result;
    const SemanticSymbolRecord scope = currentParameterScopeRecord(query);
    if (!scope.isValid() || scope.name.isEmpty()) {
        result.message = QStringLiteral("No current parameter scope");
        return result;
    }

    result.hasScope = true;
    result.scopeName = scope.name;
    if (!query.snapshot)
        return result;

    QList<ComModePickerItem> items;
    for (const SemanticSymbolRecord& record : query.snapshot->getSymbolRecords()) {
        if (!isParameterDeclaration(record))
            continue;
        if (record.name.isEmpty() || record.location.fileName.isEmpty())
            continue;
        if (record.owner.name != scope.name)
            continue;
        if (!sameComFile(record.location.fileName, scope.location.fileName))
            continue;

        const QString displayPath =
            displayPathForFile(record.location.fileName,
                               query.project.workspaceRoot);
        const int score = pickerRecordScore(record, displayPath, query.filter);
        if (score <= 0)
            continue;
        items.append(pickerItemForRecord(record, query.project, score));
    }
    result.items = sortedLimitedPickerItems(std::move(items), query.maxResults);
    return result;
}

ComModeScopedPickerResult ComModeService::signalItems(
    const ComModeScopedPickerQuery& query) const
{
    ComModeScopedPickerResult result;
    const SemanticSymbolRecord scope = currentModuleScopeRecord(query);
    if (!scope.isValid() || scope.name.isEmpty()) {
        result.message = QStringLiteral("No current module");
        return result;
    }

    result.hasScope = true;
    result.scopeName = scope.name;
    if (!query.snapshot)
        return result;

    QList<ComModePickerItem> items;
    for (const SemanticSymbolRecord& record :
         query.snapshot->getSymbolRecords()) {
        if (!isCurrentModuleSignalDeclaration(record))
            continue;
        if (record.name.isEmpty() || record.location.fileName.isEmpty())
            continue;
        if (record.owner.name != scope.name)
            continue;
        if (!sameComFile(record.location.fileName, scope.location.fileName))
            continue;

        const QString displayPath =
            displayPathForFile(record.location.fileName,
                               query.project.workspaceRoot);
        const int score = pickerRecordScore(record, displayPath, query.filter);
        if (score <= 0)
            continue;
        items.append(signalPickerItemForRecord(record,
                                               query.project,
                                               *query.snapshot,
                                               score));
    }
    result.items = sortedLimitedPickerItems(std::move(items), query.maxResults);
    return result;
}

ComModeRelativeLineResult ComModeService::relativeLineTarget(
    const ComModeRelativeLineQuery& query) const
{
    ComModeRelativeLineResult result;
    if (query.requestedModuleLine <= 0) {
        result.message = QStringLiteral("Line number must be >= 1");
        return result;
    }
    if (!query.snapshot || query.fileName.isEmpty()
        || query.currentModuleName.isEmpty()) {
        result.message = QStringLiteral("No current module");
        return result;
    }

    QList<SemanticSymbolRecord> modules;
    for (const SemanticSymbolRecord& record :
         query.snapshot->getSymbolRecords(query.fileName)) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module)
            modules.append(record);
    }
    std::stable_sort(modules.begin(), modules.end(),
                     [](const SemanticSymbolRecord& left,
                        const SemanticSymbolRecord& right) {
        return left.location.startLine < right.location.startLine;
    });

    int moduleIndex = -1;
    for (int i = 0; i < modules.size(); ++i) {
        const SemanticSymbolRecord& module = modules.at(i);
        if (module.name != query.currentModuleName)
            continue;
        const int lastLine = moduleRecordLastLine(modules, i, *query.snapshot);
        if (moduleContainsLine(module, query.currentLine, lastLine)) {
            moduleIndex = i;
            break;
        }
        if (module.location.startLine <= query.currentLine)
            moduleIndex = i;
    }

    if (moduleIndex < 0) {
        result.message = QStringLiteral("No current module");
        return result;
    }

    const SemanticSymbolRecord module = modules.at(moduleIndex);
    const int lastLine = moduleRecordLastLine(modules, moduleIndex, *query.snapshot);
    const int moduleLineCount = qMax(1, lastLine - module.location.startLine + 1);
    if (query.requestedModuleLine > moduleLineCount) {
        result.message =
            QStringLiteral("Module has only %1 lines").arg(moduleLineCount);
        result.moduleLineCount = moduleLineCount;
        result.moduleRecord = module;
        return result;
    }

    result.ok = true;
    result.filePath = module.location.fileName;
    result.line = module.location.startLine + query.requestedModuleLine - 1;
    result.column = query.requestedModuleLine == 1
        ? module.location.startColumn
        : 1;
    result.moduleLineCount = moduleLineCount;
    result.moduleRecord = module;
    return result;
}
