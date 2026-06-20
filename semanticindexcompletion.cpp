#include "semanticindex.h"

#include "completionservice.h"
#include "symboltaxonomy.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>

namespace {
int endModulePositionInContent(const QString& fileContent,
                               const SemanticSymbolRecord& moduleRecord)
{
    int searchStart = moduleRecord.location.position;
    int moduleDepth = 0;
    bool foundModule = false;

    static const QRegularExpression moduleStartPattern(QStringLiteral("\\bmodule\\s+"));
    static const QRegularExpression moduleEndPattern(QStringLiteral("\\bendmodule\\b"));

    int pos = searchStart;
    while (pos < fileContent.length()) {
        const QRegularExpressionMatch startMatch = moduleStartPattern.match(fileContent, pos);
        const QRegularExpressionMatch endMatch = moduleEndPattern.match(fileContent, pos);
        const int nextModuleStart = startMatch.hasMatch() ? startMatch.capturedStart(0) : -1;
        const int nextModuleEnd = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;

        if (nextModuleStart != -1
            && (nextModuleEnd == -1 || nextModuleStart < nextModuleEnd)) {
            if (foundModule || nextModuleStart == moduleRecord.location.position) {
                ++moduleDepth;
                foundModule = true;
            }
            pos = nextModuleStart + startMatch.capturedLength(0);
        } else if (nextModuleEnd != -1) {
            if (foundModule) {
                --moduleDepth;
                if (moduleDepth == 0)
                    return nextModuleEnd + endMatch.capturedLength(0);
            }
            pos = nextModuleEnd + endMatch.capturedLength(0);
        } else {
            break;
        }
    }

    return -1;
}

QString moduleNameAtPositionInContent(const QList<SemanticSymbolRecord>& modules,
                                      int cursorPosition,
                                      const QString& fileContent,
                                      const SemanticIndex& semanticIndex)
{
    if (fileContent.isEmpty())
        return QString();

    int cursorLine = 0;
    int pos = 0;
    while (pos < cursorPosition && pos < fileContent.length()) {
        if (fileContent.at(pos) == QLatin1Char('\n'))
            ++cursorLine;
        ++pos;
    }

    for (const SemanticSymbolRecord& module : modules) {
        if (cursorPosition < module.location.position)
            continue;
        if (!semanticIndex.isValidModuleName(module.name))
            continue;

        if (module.location.endLine > 0) {
            if (cursorLine >= module.location.startLine
                && cursorLine <= module.location.endLine)
                return module.name;
            continue;
        }

        const int moduleEndPosition = endModulePositionInContent(fileContent, module);
        if (moduleEndPosition >= 0 && cursorPosition < moduleEndPosition)
            return module.name;
    }

    return QString();
}
}

QStringList SemanticIndex::findCompletions(const SemanticQueryContext& context) const
{
    CompletionQuery query;
    query.prefix = context.prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;

    CompletionService completions(const_cast<SemanticIndex*>(this));
    if (!context.fileName.isEmpty() && context.cursorLine > 0) {
        return completions.findScopeCompletions(query);
    }
    return completions.findCompletions(query);
}

QString SemanticIndex::currentModuleAt(const QString& fileName, int cursorPosition) const
{
    if (fileName.isEmpty() || cursorPosition < 0)
        return QString();

    QList<SemanticSymbolRecord> modules;
    const QList<SemanticSymbolRecord> fileRecords = getSymbolRecords(fileName);
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module)
            modules.append(record);
    }

    if (modules.isEmpty())
        return QString();

    std::sort(modules.begin(), modules.end(),
              [](const SemanticSymbolRecord& left,
                 const SemanticSymbolRecord& right) {
                  return left.location.position < right.location.position;
              });

    QString content = getCachedFileContent(fileName);
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }

    return moduleNameAtPositionInContent(modules, cursorPosition, content, *this);
}
