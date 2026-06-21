#ifndef SYMBOLHOVERREPORTS_H
#define SYMBOLHOVERREPORTS_H

#include <QString>
#include <QStringList>

struct SymbolHoverReport {
    bool available = false;
    QString symbolName;
    QString displayKind;
    QString ownerName;
    QString sourceRole;
    QString typeText;
    QString definitionFile;
    int definitionLine = -1;
    QString unavailableReason;
};

struct DefinitionPreviewReport {
    bool available = false;
    bool targetResolved = false;
    QString symbolName;
    QString displayKind;
    QString targetFile;
    int targetLine = -1;
    int targetColumn = -1;
    int firstLineNumber = -1;
    int highlightedLine = -1;
    QStringList codeLines;
    QString unavailableReason;
};

#endif // SYMBOLHOVERREPORTS_H
