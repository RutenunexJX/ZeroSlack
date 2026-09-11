#include "commandlayerservice.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
QString normalizedCommandFileName(const QString& fileName)
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
        const QString normalized = normalizedCommandFileName(fileName);
        if (!normalized.isEmpty())
            result.insert(normalized);
    }
    return result;
}

QString displayPathForFile(const QString& fileName, const QString& root)
{
    if (fileName.isEmpty())
        return QString();

    const QString normalizedFile = normalizedCommandFileName(fileName);
    const QString normalizedRoot = normalizedCommandFileName(root);
    QString display = normalizedFile;
    if (!normalizedRoot.isEmpty()
        && (normalizedFile == normalizedRoot
            || normalizedFile.startsWith(normalizedRoot
                                         + QLatin1Char('/')))) {
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

QString kindLabelForDeclaration(SymbolTaxonomy::DeclarationKind kind)
{
    return kind == SymbolTaxonomy::DeclarationKind::Package
        ? QStringLiteral("package")
        : QStringLiteral("module");
}

CommandLayerPickerItem pickerItemForRecord(
    const SemanticSymbolRecord& record,
    const ProjectSnapshot& project,
    int score)
{
    CommandLayerPickerItem item;
    item.kind = record.declarationKind
            == SymbolTaxonomy::DeclarationKind::Package
        ? CommandLayerPickerItemKind::Package
        : CommandLayerPickerItemKind::Module;
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

QList<CommandLayerPickerItem> sortedLimitedPickerItems(
    QList<CommandLayerPickerItem> items,
    int maxResults)
{
    std::stable_sort(items.begin(), items.end(),
                     [](const CommandLayerPickerItem& left,
                        const CommandLayerPickerItem& right) {
        if (left.score != right.score)
            return left.score > right.score;
        if (left.name != right.name)
            return left.name < right.name;
        if (left.displayPath != right.displayPath)
            return left.displayPath < right.displayPath;
        return left.line < right.line;
    });
    if (maxResults >= 0 && items.size() > maxResults)
        items = items.mid(0, maxResults);
    return items;
}

QList<CommandLayerPickerItem> declarationPickerItems(
    const CommandLayerPickerQuery& query,
    SymbolTaxonomy::DeclarationKind declarationKind)
{
    QList<CommandLayerPickerItem> result;
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
        if (useScope
            && !scopedFiles.contains(
                normalizedCommandFileName(record.location.fileName))) {
            continue;
        }

        const QString displayPath =
            displayPathForFile(record.location.fileName,
                               query.project.workspaceRoot);
        const int score = qMax(fuzzyScore(record.name, query.filter),
                               qMax(fuzzyScore(displayPath, query.filter),
                                    fuzzyScore(record.owner.name,
                                               query.filter)));
        if (score <= 0)
            continue;
        result.append(pickerItemForRecord(record, query.project, score));
    }
    return sortedLimitedPickerItems(std::move(result), query.maxResults);
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
} // namespace

QList<CommandLayerPickerItem> CommandLayerService::moduleItems(
    const CommandLayerPickerQuery& query) const
{
    return declarationPickerItems(query,
                                  SymbolTaxonomy::DeclarationKind::Module);
}

QList<CommandLayerPickerItem> CommandLayerService::packageItems(
    const CommandLayerPickerQuery& query) const
{
    return declarationPickerItems(query,
                                  SymbolTaxonomy::DeclarationKind::Package);
}

CommandLayerRelativeLineResult CommandLayerService::relativeLineTarget(
    const CommandLayerRelativeLineQuery& query) const
{
    CommandLayerRelativeLineResult result;
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
    const int lastLine =
        moduleRecordLastLine(modules, moduleIndex, *query.snapshot);
    const int moduleLineCount =
        qMax(1, lastLine - module.location.startLine + 1);
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
