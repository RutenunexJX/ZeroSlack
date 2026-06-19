#include "syminfo.h"

#include <QFile>
#include <QReadLocker>
#include <QRegularExpression>

bool sym_list::isValidModuleName(const QString& name)
{
    if (name.isEmpty())
        return false;
    static const QRegularExpression svIdentifier(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    return svIdentifier.match(name).hasMatch();
}

QString sym_list::getCurrentModuleScope(const QString& fileName, int lineNumber)
{
    const QList<SymbolInfo> symbols = getAllSymbols();
    for (const SymbolInfo& moduleSymbol : symbols) {
        if (moduleSymbol.symbolType != sym_module)
            continue;
        if (moduleSymbol.fileName != fileName)
            continue;
        if (!isValidModuleName(moduleSymbol.symbolName))
            continue;
        int moduleEndLine = findEndModuleLine(fileName, moduleSymbol);
        if (moduleEndLine < 0)
            continue;
        if (lineNumber > moduleSymbol.startLine && lineNumber < moduleEndLine)
            return moduleSymbol.symbolName;
    }
    return QString();
}

QString sym_list::getCachedFileContent(const QString& fileName) const
{
    QReadLocker lock(&symbolDbLock);
    return previousFileContents.value(fileName, QString());
}

int sym_list::findEndModuleLine(const QString &fileName, const SymbolInfo &moduleSymbol)
{
    if (moduleSymbol.symbolType != sym_module) {
        return -1;
    }

    QString content;
    if (previousFileContents.contains(fileName)) {
        content = previousFileContents[fileName];
    } else {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
            return -1;
        }
        content = file.readAll();
        file.close();
        previousFileContents[fileName] = content;
    }

    QStringList lines = content.split('\n');
    int moduleDepth = 0;
    int scanStart = moduleSymbol.startLine - 1;
    if (scanStart < 0)
        scanStart = 0;
    int lineStartPos = 0;
    for (int j = 0; j < scanStart && j < lines.size(); ++j)
        lineStartPos += lines[j].length() + 1;

    for (int i = scanStart; i < lines.size(); ++i) {
        const QString &line = lines[i];
        static const QRegularExpression moduleWord("\\bmodule\\b");
        static const QRegularExpression endmoduleWord("\\bendmodule\\b");
        if (line.contains(moduleWord) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth++;
        }
        if (line.contains(endmoduleWord) && !isMatchInComment(lineStartPos, line.length())) {
            moduleDepth--;
            if (moduleDepth == 0) {
                return i;
            }
        }
        lineStartPos += line.length() + 1;
    }

    return -1;
}
