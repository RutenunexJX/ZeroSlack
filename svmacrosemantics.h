#ifndef SVMACROSEMANTICS_H
#define SVMACROSEMANTICS_H

#include "semanticdecorationservice.h"
#include "semanticindex.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace SvMacroSemantics {

struct MacroDefinition {
    QString fileName;
    QString name;
    QStringList parameters;
    QString body;
    int line = 0;
    int column = 0;
    int position = -1;
    int length = 0;
    bool functionLike = false;

    bool isValid() const { return !name.isEmpty() && line > 0; }
};

struct MacroReference {
    QString fileName;
    QString name;
    int line = 0;
    int column = 0;
    int position = -1;
    int length = 0;

    bool isValid() const { return !name.isEmpty() && line > 0; }
};

struct InactiveBranchRange {
    int startPosition = -1;
    int length = 0;
    int startLine = 0;
    int endLine = 0;
    QString reason;

    bool isValid() const { return startPosition >= 0 && length > 0; }
};

QList<MacroDefinition> collectMacroDefinitions(const QString& fileName,
                                               const QString& content);
QList<MacroReference> collectMacroReferences(const QString& fileName,
                                             const QString& content);
QList<SemanticSymbolRecord> collectMacroDefinitionRecords(
    const QString& fileName,
    const QString& content);

MacroDefinition macroDefinitionFromRecord(
    const SemanticSymbolRecord& record,
    const QString& content = QString());

QString macroSignatureText(const MacroDefinition& definition);
QString truncatedMacroBody(const MacroDefinition& definition,
                           int maxCharacters = 180);

QSet<QString> configuredDefineNames(const QHash<QString, QString>& defines);
QSet<QString> macroDefinitionNames(const QList<SemanticSymbolRecord>& records);

QList<SemanticDiagnostic> undefinedMacroDiagnostics(
    const QString& fileName,
    const QString& content,
    const QSet<QString>& visibleMacroNames);

QList<InactiveBranchRange> inactiveBranchRanges(
    const QString& content,
    const QHash<QString, QString>& configuredDefines);

QList<SemanticDecoration> inactiveBranchDecorations(
    const QString& fileName,
    const QString& content,
    const QHash<QString, QString>& configuredDefines);

} // namespace SvMacroSemantics

#endif // SVMACROSEMANTICS_H
