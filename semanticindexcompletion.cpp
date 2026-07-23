#include "semanticindex.h"

#include "symboltaxonomy.h"

#include <QFile>
#include <QTextBlock>
#include <QTextDocument>

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

    QString content = getCachedFileContent(fileName);
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }
    if (content.isEmpty())
        return QString();

    QTextDocument document(content);
    const int boundedPosition = qBound(
        0,
        cursorPosition,
        qMax(0, document.characterCount() - 1));
    const QTextBlock block = document.findBlock(boundedPosition);
    if (!block.isValid())
        return QString();
    const int cursorLine = block.blockNumber() + 1;

    const SemanticSymbolRecord* containingModule = nullptr;
    for (const SemanticSymbolRecord& module : modules) {
        if (!isValidModuleName(module.name)
            || module.location.startLine <= 0
            || module.location.endLine < module.location.startLine
            || cursorLine < module.location.startLine
            || cursorLine > module.location.endLine) {
            continue;
        }
        if (!containingModule
            || module.location.startLine
                > containingModule->location.startLine) {
            containingModule = &module;
        }
    }
    return containingModule ? containingModule->name : QString();
}
