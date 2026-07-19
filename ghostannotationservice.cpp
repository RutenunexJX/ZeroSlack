#include "ghostannotationservice.h"

#include "effectivevalueservice.h"
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
    const QList<SemanticSymbolRecord>& records,
    const QString& moduleName,
    const QString& formalName)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (!isPortRecord(record) || record.name != formalName)
            continue;
        if (!moduleName.isEmpty() && record.owner.name != moduleName)
            continue;
        result.append(record);
    }
    return result;
}

void appendFormalPortAnnotations(
    const QList<LineInfo>& lines,
    const QList<SemanticSymbolRecord>& fileRecords,
    const QList<SemanticSymbolRecord>& allRecords,
    QList<GhostAnnotation>* output)
{
    if (!output)
        return;

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
            allRecords, moduleName, formalName);
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
        output->append(makeAnnotation(
            GhostAnnotationKind::FormalPort,
            GhostAnnotationPlacement::LeftOfAnchor,
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

QString widthGhostText(const EffectiveValueResult& value,
                       const SemanticSymbolRecord& record)
{
    if (!value.current() || value.bitWidthText.isEmpty()
        || value.bitWidthText == QStringLiteral("not applicable")
        || value.bitWidthText == QStringLiteral("not statically known")) {
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
            const QString text = effectiveValueGhostText(value);
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
            const QString text = effectiveValueGhostText(value);
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
            if (!fact.valueText.isEmpty()) {
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
    return annotations;
}

int lineCommentStart(const QString& line)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i + 1 < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('/')
            && line.at(i + 1) == QLatin1Char('/')) {
            return i;
        }
    }
    return -1;
}

bool stringRangeAt(const QString& line,
                   int column,
                   int* start,
                   int* end)
{
    bool inString = false;
    bool escaped = false;
    int open = -1;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!inString) {
            if (ch == QLatin1Char('"')) {
                inString = true;
                open = i;
            }
            continue;
        }
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch != QLatin1Char('"'))
            continue;
        if (column >= open && column <= i) {
            if (start)
                *start = open;
            if (end)
                *end = i + 1;
            return true;
        }
        inString = false;
        open = -1;
    }
    return false;
}

bool literalTokenRangeAt(const QString& line,
                         int column,
                         int limit,
                         int* start,
                         int* end)
{
    if (column < 0 || column > limit)
        return false;
    auto literalChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_')
            || ch == QLatin1Char('\'');
    };

    int probe = qMin(column, limit - 1);
    if (probe < 0)
        return false;
    if (!literalChar(line.at(probe)) && probe > 0
        && literalChar(line.at(probe - 1))) {
        --probe;
    }
    if (!literalChar(line.at(probe)))
        return false;
    int left = probe;
    while (left > 0 && literalChar(line.at(left - 1)))
        --left;
    int right = probe + 1;
    while (right < limit && literalChar(line.at(right)))
        ++right;
    const QString token = line.mid(left, right - left);
    if (token.isEmpty()
        || (!token.at(0).isDigit()
            && token.at(0) != QLatin1Char('\''))) {
        return false;
    }
    if (start)
        *start = left;
    if (end)
        *end = right;
    return true;
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
    QList<SemanticSymbolRecord> allRecords = activeIndex->getSymbolRecords();
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
                                allRecords,
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
    if (query.documentText.isEmpty() || query.cursorPosition < 0)
        return report;

    const QList<LineInfo> lines = documentLines(query.documentText);
    int lineIndex = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const int start = lines.at(i).startPosition;
        const int end = start + lines.at(i).text.size();
        if (query.cursorPosition >= start
            && query.cursorPosition <= end) {
            lineIndex = i;
            break;
        }
    }
    if (lineIndex < 0)
        return report;

    const LineInfo& line = lines.at(lineIndex);
    const int column = qBound(0,
                              query.cursorPosition - line.startPosition,
                              line.text.size());
    const int comment = lineCommentStart(line.text);
    const int limit = comment >= 0 ? comment : line.text.size();
    if (column >= limit)
        return report;

    int start = -1;
    int end = -1;
    QString expression;
    bool stringLiteral = false;
    if (stringRangeAt(line.text, column, &start, &end)) {
        const QString before = line.text.left(start).trimmed();
        if (before.startsWith(QStringLiteral("`include")))
            return report;
        expression = line.text.mid(start, end - start);
        stringLiteral = true;
    } else {
        if (!literalTokenRangeAt(line.text,
                                 column,
                                 limit,
                                 &start,
                                 &end)) {
            return report;
        }
        expression = line.text.mid(start, end - start);
    }

    const EffectiveLiteralResult literal =
        EffectiveValueService::evaluateLiteral(expression, stringLiteral);
    if (!literal.available || literal.valueText.isEmpty())
        return report;
    report.available = true;
    report.displayText = literal.valueText;
    report.startPosition = line.startPosition + start;
    report.endPosition = line.startPosition + end;
    return report;
}
