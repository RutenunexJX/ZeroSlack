#include "ghostannotationservice.h"

#include "effectivevalueservice.h"
#include "tsdocument.h"
#include <QSet>
#include <QStringList>

#include <algorithm>

std::unique_ptr<GhostAnnotationService>
    GhostAnnotationService::instance = nullptr;

namespace {
struct LineInfo {
    QString text;
    int startPosition = 0;
};

QList<LineInfo> documentLines(const QString& text)
{
    QList<LineInfo> result;
    int start = 0;
    while (start <= text.size()) {
        int end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0)
            end = text.size();
        LineInfo line;
        line.startPosition = start;
        line.text = text.mid(start, end - start);
        if (line.text.endsWith(QLatin1Char('\r')))
            line.text.chop(1);
        result.append(std::move(line));
        if (end >= text.size())
            break;
        start = end + 1;
    }
    return result;
}

bool isIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

int findNameOnLine(const QString& line,
                   const QString& name,
                   int oneBasedColumn)
{
    if (name.isEmpty())
        return -1;
    int preferred = qMax(0, oneBasedColumn - 1);
    if (preferred + name.size() <= line.size()
        && line.mid(preferred, name.size()) == name) {
        return preferred;
    }

    int position = 0;
    while ((position = line.indexOf(name, position)) >= 0) {
        const bool leftBoundary = position == 0
            || !isIdentifierPart(line.at(position - 1));
        const int after = position + name.size();
        const bool rightBoundary = after >= line.size()
            || !isIdentifierPart(line.at(after));
        if (leftBoundary && rightBoundary)
            return position;
        position = after;
    }
    return -1;
}

GhostAnnotation makeAnnotation(GhostAnnotationKind kind,
                               GhostAnnotationPlacement placement,
                               const QString& text,
                               int line,
                               int anchorPosition,
                               int anchorLength = 0)
{
    GhostAnnotation annotation;
    annotation.kind = kind;
    annotation.placement = placement;
    annotation.text = text;
    annotation.line = line;
    annotation.anchorPosition = anchorPosition;
    annotation.anchorLength = anchorLength;
    return annotation;
}

bool startsBefore(const GhostAnnotation& left,
                  const GhostAnnotation& right)
{
    if (left.line != right.line)
        return left.line < right.line;
    if (left.anchorPosition != right.anchorPosition)
        return left.anchorPosition < right.anchorPosition;
    return static_cast<int>(left.kind) < static_cast<int>(right.kind);
}

bool isPortRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return true;
    default:
        return false;
    }
}

bool isSignalRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind
               == SymbolTaxonomy::DeclarationKind::Signal
        || record.declarationKind
               == SymbolTaxonomy::DeclarationKind::Port;
}

bool isParameterRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind
               == SymbolTaxonomy::DeclarationKind::Parameter
        || record.declarationKind
               == SymbolTaxonomy::DeclarationKind::Localparam;
}

bool isEnumValueRecord(const SemanticSymbolRecord& record)
{
    return record.collectorKind
        == SymbolTaxonomy::CollectorKind::EnumValue;
}

QString formalNameFromInstPin(const QString& name)
{
    const int separator = name.lastIndexOf(QLatin1Char('.'));
    return separator < 0 ? name : name.mid(separator + 1);
}

QString stripFormalDeclarationSeparator(QString text)
{
    text = text.trimmed();
    while (text.endsWith(QLatin1Char(','))
           || text.endsWith(QLatin1Char(';'))) {
        text.chop(1);
        text = text.trimmed();
    }
    return text;
}

QList<SemanticSymbolRecord> matchingPortRecords(
    SemanticIndex* index,
    const QString& moduleName,
    const QString& formalName)
{
    QList<SemanticSymbolRecord> result;
    if (!index || formalName.isEmpty())
        return result;
    QSet<QString> seenDeclarations;
    for (const SemanticSymbolRecord& record :
         index->getSymbolRecordsByName(formalName)) {
        if (!isPortRecord(record) || record.name != formalName)
            continue;
        if (!moduleName.isEmpty() && record.owner.name != moduleName)
            continue;
        QString declarationIdentity =
            symbolStableKeyText(record.stableKey);
        if (declarationIdentity.isEmpty()) {
            declarationIdentity =
                QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                    .arg(record.location.fileName)
                    .arg(record.location.position)
                    .arg(record.location.length)
                    .arg(record.location.startLine)
                    .arg(record.location.startColumn)
                    .arg(record.owner.name, record.name);
        }
        if (seenDeclarations.contains(declarationIdentity))
            continue;
        seenDeclarations.insert(declarationIdentity);
        result.append(record);
    }
    return result;
}

QString recordSemanticIdentity(const SemanticSymbolRecord& record)
{
    const QString stableKey = symbolStableKeyText(record.stableKey);
    return stableKey.isEmpty()
        ? EffectiveValueService::stableSourceIdentity(record)
        : stableKey;
}

QString hierarchyContextIdentity(
    const HierarchyInstanceContext& context)
{
    return context.workspacePath
        + QLatin1Char('\x1f')
        + context.activeTopModule
        + QLatin1Char('\x1f')
        + context.instancePath;
}

QString formalPortAnnotationIdentity(
    const SemanticSymbolRecord& instPin,
    const SemanticSymbolRecord& formalPort,
    const HierarchyInstanceContext& context)
{
    const QString instPinIdentity =
        EffectiveValueService::stableSourceIdentity(instPin);
    const QString formalPortIdentity =
        recordSemanticIdentity(formalPort);
    if (instPinIdentity.isEmpty() || formalPortIdentity.isEmpty())
        return QString();
    return instPinIdentity
        + QLatin1Char('\x1f')
        + formalPortIdentity
        + QLatin1Char('\x1f')
        + hierarchyContextIdentity(context);
}

void appendFormalPortAnnotations(
    const QList<LineInfo>& lines,
    const QList<SemanticSymbolRecord>& fileRecords,
    SemanticIndex* index,
    const HierarchyInstanceContext& instanceContext,
    QList<GhostAnnotation>* output)
{
    if (!output)
        return;

    QSet<QString> emittedAnnotationKeys;
    for (const SemanticSymbolRecord& instPin : fileRecords) {
        if (instPin.collectorKind
                != SymbolTaxonomy::CollectorKind::InstPin
            || instPin.location.startLine <= 0
            || instPin.location.startLine > lines.size()) {
            continue;
        }

        const QString formalName = formalNameFromInstPin(instPin.name);
        const QString moduleName = !instPin.type.resolvedTypeName.isEmpty()
            ? instPin.type.resolvedTypeName
            : instPin.type.rawTypeText;
        const QList<SemanticSymbolRecord> ports = matchingPortRecords(
            index, moduleName, formalName);
        if (ports.size() != 1)
            continue;
        const SemanticSymbolRecord& port = ports.first();
        const QString text = stripFormalDeclarationSeparator(
            port.presentation.declarationText);
        if (text.isEmpty())
            continue;

        const LineInfo& line = lines.at(instPin.location.startLine - 1);
        const int nameStart = findNameOnLine(
            line.text,
            formalName,
            instPin.location.startColumn);
        if (nameStart < 0)
            continue;

        const QString annotationIdentity =
            formalPortAnnotationIdentity(instPin,
                                         port,
                                         instanceContext);
        if (!annotationIdentity.isEmpty()
            && emittedAnnotationKeys.contains(annotationIdentity)) {
            continue;
        }
        if (!annotationIdentity.isEmpty())
            emittedAnnotationKeys.insert(annotationIdentity);

        output->append(makeAnnotation(
            GhostAnnotationKind::FormalPort,
            GhostAnnotationPlacement::RightOfLine,
            text,
            instPin.location.startLine,
            line.startPosition + nameStart,
            formalName.size()));
    }
}

QString effectiveValueGhostText(const EffectiveValueResult& value)
{
    if (!value.current() || value.valueText.isEmpty())
        return QString();
    return QStringLiteral("= %1").arg(value.valueText);
}

QString enumValueGhostText(const EffectiveValueResult& value)
{
    if (!value.current())
        return QString();
    const QString display = value.displayValueText.isEmpty()
        ? value.valueText
        : value.displayValueText;
    return display.isEmpty()
        ? QString()
        : QStringLiteral("= %1").arg(display);
}

QString widthGhostText(const EffectiveValueResult& value,
                       const SemanticSymbolRecord& record)
{
    if (!value.current() || value.bitWidthText.isEmpty()
        || value.bitWidthText == QStringLiteral("not applicable")
        || value.bitWidthText == QStringLiteral("not statically known")) {
        return QString();
    }

    // The source-level packed dimensions are collected from Slang syntax.
    // A port that already spells out its width does not need the same fact
    // repeated as a trailing annotation. Typedef-resolved widths remain
    // eligible because their source presentation has no packed dimensions.
    if (isPortRecord(record)
        && !record.presentation.packedDimensionsText.trimmed().isEmpty()) {
        return QString();
    }

    bool numeric = false;
    const qulonglong width = value.bitWidthText.toULongLong(&numeric);
    if (!numeric || width <= 1)
        return QString();

    const QString sourceDimensions =
        !value.packedDimensionsText.isEmpty()
            ? value.packedDimensionsText
            : record.presentation.packedDimensionsText;
    const QString compact = sourceDimensions.simplified();
    if (width == 8
        && (compact == QStringLiteral("[7:0]")
            || compact.isEmpty())) {
        return QString();
    }
    return sourceDimensions.isEmpty()
        ? QStringLiteral("%1 bits").arg(width)
        : QStringLiteral("%1 %2 bits").arg(sourceDimensions).arg(width);
}

void appendSymbolEffectiveValueAnnotations(
    const QList<LineInfo>& lines,
    const QList<SemanticSymbolRecord>& records,
    EffectiveValueService* values,
    const GhostAnnotationQuery& query,
    QList<GhostAnnotation>* output)
{
    if (!values || !output)
        return;

    for (const SemanticSymbolRecord& record : records) {
        if (record.location.startLine <= 0
            || record.location.startLine > lines.size()) {
            continue;
        }
        if (!isParameterRecord(record)
            && !isEnumValueRecord(record)
            && !isSignalRecord(record)) {
            continue;
        }

        const LineInfo& line = lines.at(record.location.startLine - 1);
        const int nameStart = findNameOnLine(
            line.text, record.name, record.location.startColumn);
        if (nameStart < 0)
            continue;
        const int anchor = line.startPosition + nameStart
            + record.name.size();

        EffectiveValueQuery valueQuery;
        valueQuery.symbol = record;
        valueQuery.instanceContext = query.instanceContext;
        valueQuery.documentText = query.documentText;
        valueQuery.documentRevision = query.documentRevision;
        const EffectiveValueResult value = values->resolve(valueQuery);
        if (!value.current())
            continue;

        if (isParameterRecord(record)) {
            const QString text = value.sourceTextDisplaysEffectiveValue
                ? QString()
                : effectiveValueGhostText(value);
            if (!text.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::ParameterValue,
                    GhostAnnotationPlacement::RightOfLine,
                    text,
                    record.location.startLine,
                    anchor));
            }
        }
        if (isEnumValueRecord(record)) {
            const QString text = enumValueGhostText(value);
            if (!text.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::EnumValue,
                    GhostAnnotationPlacement::RightOfAnchor,
                    text,
                    record.location.startLine,
                    anchor));
            }
        }
        if (isSignalRecord(record)) {
            const QString text = widthGhostText(value, record);
            if (!text.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::SignalWidth,
                    GhostAnnotationPlacement::RightOfLine,
                    text,
                    record.location.startLine,
                    anchor));
            }
            if (!value.unpackedElementCountText.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::ArraySummary,
                    GhostAnnotationPlacement::RightOfLine,
                    QStringLiteral("%1 entries")
                        .arg(value.unpackedElementCountText),
                    record.location.startLine,
                    anchor));
            }
        }
    }
}

QString bracketText(const QString& expression)
{
    const int open = expression.lastIndexOf(QLatin1Char('['));
    const int close = expression.indexOf(QLatin1Char(']'), open + 1);
    if (open < 0 || close < open)
        return QString();
    return expression.mid(open, close - open + 1).simplified();
}

void appendExpressionFactAnnotations(
    const QList<EffectiveValueFact>& facts,
    QList<GhostAnnotation>* output)
{
    if (!output)
        return;
    for (const EffectiveValueFact& fact : facts) {
        if (!fact.isValid()
            || fact.status == EffectiveValueStatus::Error
            || fact.status == EffectiveValueStatus::Stale) {
            continue;
        }
        switch (fact.kind) {
        case EffectiveValueFactKind::ParameterOverride:
            if (!fact.valueText.isEmpty()
                && !fact.sourceTextDisplaysEffectiveValue) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::ParameterOverride,
                    GhostAnnotationPlacement::RightOfAnchor,
                    QStringLiteral("= %1").arg(fact.valueText),
                    fact.line,
                    fact.endPosition));
            }
            break;
        case EffectiveValueFactKind::PartSelectWidth:
            if (!fact.bitWidthText.isEmpty()) {
                const QString selection = bracketText(fact.expressionText);
                const QString text = selection.isEmpty()
                    ? QStringLiteral("%1 bits").arg(fact.bitWidthText)
                    : QStringLiteral("%1 %2 bits")
                          .arg(selection, fact.bitWidthText);
                output->append(makeAnnotation(
                    GhostAnnotationKind::PartSelect,
                    GhostAnnotationPlacement::RightOfLine,
                    text,
                    fact.line,
                    fact.endPosition));
            }
            break;
        case EffectiveValueFactKind::GenerateCount:
            if (!fact.valueText.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::GenerateLoop,
                    GhostAnnotationPlacement::RightOfLine,
                    QStringLiteral("instances=%1").arg(fact.valueText),
                    fact.line,
                    fact.endPosition));
            }
            break;
        case EffectiveValueFactKind::ConcatenationWidth:
            if (!fact.bitWidthText.isEmpty()) {
                output->append(makeAnnotation(
                    GhostAnnotationKind::ConcatenationWidth,
                    GhostAnnotationPlacement::RightOfLine,
                    QStringLiteral("%1 bits").arg(fact.bitWidthText),
                    fact.line,
                    fact.endPosition));
            }
            break;
        }
    }
}

QList<GhostAnnotation> mergeLineTailAnnotations(
    QList<GhostAnnotation> annotations)
{
    std::stable_sort(annotations.begin(), annotations.end(), startsBefore);

    QList<GhostAnnotation> merged;
    merged.reserve(annotations.size());
    QSet<QString> emittedGenerateLines;
    for (GhostAnnotation& annotation : annotations) {
        if (annotation.kind == GhostAnnotationKind::GenerateLoop
            && annotation.placement
                   == GhostAnnotationPlacement::RightOfLine) {
            const QString key = QString::number(annotation.line)
                + QLatin1Char('\x1f') + annotation.text;
            if (emittedGenerateLines.contains(key))
                continue;
            emittedGenerateLines.insert(key);
        }
        merged.append(std::move(annotation));
    }
    return merged;
}

}

GhostAnnotationService* GhostAnnotationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<GhostAnnotationService>();
    return instance.get();
}

GhostAnnotationService::GhostAnnotationService(
    SemanticIndex* semanticIndex,
    EffectiveValueService* effectiveValueService)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
    , values(effectiveValueService)
{
}

GhostAnnotationService::~GhostAnnotationService() = default;

void GhostAnnotationService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SemanticIndex* GhostAnnotationService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

GhostAnnotationReport GhostAnnotationService::annotationsForDocument(
    const GhostAnnotationQuery& query) const
{
    GhostAnnotationReport report;
    if (query.fileName.isEmpty() || query.documentText.isEmpty())
        return report;

    const QList<LineInfo> lines = documentLines(query.documentText);
    SemanticIndex* activeIndex = semanticIndex();
    QList<SemanticSymbolRecord> fileRecords =
        activeIndex->getSymbolRecords(query.fileName);
    std::unique_ptr<EffectiveValueService> indexedValues;
    EffectiveValueService* valueReader = values;
    if (!valueReader && activeIndex == SemanticIndex::getInstance()) {
        valueReader = EffectiveValueService::getInstance();
    } else if (!valueReader) {
        indexedValues =
            std::make_unique<EffectiveValueService>(activeIndex);
        valueReader = indexedValues.get();
    }
    const QList<EffectiveValueFact> facts = valueReader->factsForDocument(
        query.fileName,
        query.documentText,
        query.instanceContext,
        query.documentRevision);

    appendFormalPortAnnotations(lines,
                                fileRecords,
                                activeIndex,
                                query.instanceContext,
                                &report.annotations);
    appendSymbolEffectiveValueAnnotations(lines,
                                          fileRecords,
                                          valueReader,
                                          query,
                                          &report.annotations);
    appendExpressionFactAnnotations(facts, &report.annotations);

    report.annotations.erase(
        std::remove_if(report.annotations.begin(),
                       report.annotations.end(),
                       [](const GhostAnnotation& annotation) {
                           return !annotation.isValid();
                       }),
        report.annotations.end());
    report.annotations = mergeLineTailAnnotations(
        std::move(report.annotations));
    return report;
}

GhostNumericLiteralReport GhostAnnotationService::numericLiteralAt(
    const GhostNumericLiteralQuery& query) const
{
    GhostNumericLiteralReport report;
    if (!query.syntaxDocument || query.cursorPosition < 0) {
        return report;
    }

    const TSNumericLiteralTarget target =
        query.syntaxDocument->numericLiteralAt(query.cursorPosition);
    if (!target.ok())
        return report;

    const EffectiveLiteralResult literal =
        EffectiveValueService::evaluateLiteral(
            target.evaluationText.isEmpty()
                ? target.text
                : target.evaluationText,
            target.stringLiteral);
    if (!literal.available
        || literal.valueText.isEmpty()
        || literal.radixRepresentations.isEmpty()) {
        return report;
    }
    report.available = true;
    report.valueText = literal.valueText;
    report.radixRepresentations = literal.radixRepresentations;
    QStringList displayLines = literal.radixRepresentations;
    if (target.stringLiteral)
        displayLines.prepend(literal.valueText);
    report.displayText = displayLines.join(QLatin1Char('\n'));
    report.startPosition = target.startChar;
    report.endPosition = target.endChar;
    return report;
}
