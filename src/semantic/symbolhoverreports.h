#ifndef SYMBOLHOVERREPORTS_H
#define SYMBOLHOVERREPORTS_H

#include <QString>
#include <QStringList>

enum class EffectiveValueStatus;

struct SymbolHoverReport {
    bool available = false;
    QString symbolName;
    QString displayKind;
    QString ownerName;
    QString sourceRole;
    QString typeText;
    QString macroSignatureText;
    QString macroBodyText;
    QString declarationText;
    QString valueText;
    QString expressionText;
    QString valueSource;
    QString instancePath;
    QString resolvedTypeText;
    QString packedDimensionsText;
    QString unpackedDimensionsText;
    QString bitWidthText;
    QString signednessText;
    QString interfaceName;
    QString modportName;
    QString enumTypeName;
    QString enumUnderlyingBitWidthText;
    QString definitionFile;
    int definitionLine = -1;
    QString unavailableReason;
    QString evaluationFailureReason;
    EffectiveValueStatus effectiveValueStatus{};
    bool parameterLike = false;
    bool enumMember = false;
    bool port = false;
    bool instanceBound = false;
    bool defaultEvaluation = false;
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
