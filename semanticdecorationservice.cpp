#include "semanticdecorationservice.h"

#include <QChar>
#include <QHash>
#include <QPair>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

std::unique_ptr<SemanticDecorationService>
    SemanticDecorationService::instance = nullptr;

namespace {
bool isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
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

        if (ch == QLatin1Char('/') && line.at(i + 1) == QLatin1Char('/'))
            return i;
    }
    return -1;
}

bool hasIdentifierBoundary(const QString& line, int start, int length)
{
    const int end = start + length;
    const bool leftOk =
        start <= 0 || !isIdentifierPart(line.at(start - 1));
    const bool rightOk =
        end >= line.size() || !isIdentifierPart(line.at(end));
    return leftOk && rightOk;
}

QList<QPair<int, int>> identifierSpansInCodeLine(const QString& line)
{
    QList<QPair<int, int>> spans;
    bool inString = false;
    bool escaped = false;
    int pos = 0;
    while (pos < line.size()) {
        const QChar ch = line.at(pos);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            ++pos;
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
            ++pos;
            continue;
        }
        if (ch == QLatin1Char('/')
            && pos + 1 < line.size()
            && line.at(pos + 1) == QLatin1Char('/')) {
            break;
        }

        if (!isIdentifierStart(ch)) {
            ++pos;
            continue;
        }

        const int start = pos;
        ++pos;
        while (pos < line.size() && isIdentifierPart(line.at(pos)))
            ++pos;
        spans.append(qMakePair(start, pos - start));
    }
    return spans;
}

int findNameOnLine(const QString& line,
                   const QString& name,
                   int preferredStartColumn)
{
    if (name.isEmpty())
        return -1;

    const int commentStart = lineCommentStart(line);
    const int searchEnd = commentStart >= 0 ? commentStart : line.size();
    const int preferredStart = qBound(0, preferredStartColumn - 1, searchEnd);

    auto findFrom = [&](int start) -> int {
        int pos = start;
        while (pos >= 0 && pos < searchEnd) {
            pos = line.indexOf(name, pos, Qt::CaseSensitive);
            if (pos < 0 || pos + name.size() > searchEnd)
                return -1;
            if (hasIdentifierBoundary(line, pos, name.size()))
                return pos;
            ++pos;
        }
        return -1;
    };

    const int afterPreferred = findFrom(preferredStart);
    if (afterPreferred >= 0)
        return afterPreferred;
    return findFrom(0);
}

SemanticDecorationRole roleForRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    switch (record.collectorKind) {
    case CollectorKind::EnumValue:
        return SemanticDecorationRole::EnumValue;
    case CollectorKind::Module:
    case CollectorKind::Interface:
        return SemanticDecorationRole::ModuleInterface;
    case CollectorKind::Package:
    case CollectorKind::Enum:
    case CollectorKind::InterfaceModport:
        return SemanticDecorationRole::PackageClassType;
    case CollectorKind::Typedef:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
        return SemanticDecorationRole::TypeAlias;
    case CollectorKind::Inst:
        return SemanticDecorationRole::InstanceName;
    case CollectorKind::InstPin:
        return SemanticDecorationRole::FormalPort;
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
        return SemanticDecorationRole::Parameter;
    case CollectorKind::DefDefine:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
        return SemanticDecorationRole::Macro;
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::EnumVariable:
        return SemanticDecorationRole::ActualSignal;
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return SemanticDecorationRole::ModulePort;
    default:
        break;
    }

    switch (record.declarationKind) {
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
        return SemanticDecorationRole::ModuleInterface;
    case DeclarationKind::Package:
    case DeclarationKind::Enum:
    case DeclarationKind::Modport:
        return SemanticDecorationRole::PackageClassType;
    case DeclarationKind::Typedef:
    case DeclarationKind::Struct:
        return SemanticDecorationRole::TypeAlias;
    case DeclarationKind::Instance:
        return SemanticDecorationRole::InstanceName;
    case DeclarationKind::Port:
        return SemanticDecorationRole::ModulePort;
    case DeclarationKind::Parameter:
    case DeclarationKind::Localparam:
        return SemanticDecorationRole::Parameter;
    case DeclarationKind::Macro:
        return SemanticDecorationRole::Macro;
    default:
        return SemanticDecorationRole::ActualSignal;
    }
}

bool shouldDecorateRecord(const SemanticSymbolRecord& record)
{
    if (record.name.isEmpty() || !record.location.isValid())
        return false;

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::EnumValue:
    case CollectorKind::Module:
    case CollectorKind::Interface:
    case CollectorKind::Package:
    case CollectorKind::Typedef:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
    case CollectorKind::Enum:
    case CollectorKind::InterfaceModport:
    case CollectorKind::Inst:
    case CollectorKind::InstPin:
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
    case CollectorKind::DefDefine:
    case CollectorKind::DefIfdef:
    case CollectorKind::DefIfndef:
    case CollectorKind::DefElse:
    case CollectorKind::DefElsif:
    case CollectorKind::DefEndif:
    case CollectorKind::Reg:
    case CollectorKind::Wire:
    case CollectorKind::Logic:
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::EnumVariable:
        return true;
    default:
        return false;
    }
}

int usageRolePriority(SemanticDecorationRole role)
{
    switch (role) {
    case SemanticDecorationRole::EnumValue:
        return 50;
    case SemanticDecorationRole::TypeAlias:
        return 40;
    case SemanticDecorationRole::Parameter:
        return 30;
    case SemanticDecorationRole::ModulePort:
        return 20;
    default:
        return 0;
    }
}

bool usageDecorationRoleForRecord(const SemanticSymbolRecord& record,
                                  SemanticDecorationRole* role)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    if (record.collectorKind == CollectorKind::InstPin)
        return false;

    SemanticDecorationRole candidate = SemanticDecorationRole::ActualSignal;
    bool eligible = false;
    switch (record.collectorKind) {
    case CollectorKind::EnumValue:
        candidate = SemanticDecorationRole::EnumValue;
        eligible = true;
        break;
    case CollectorKind::Typedef:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
        candidate = SemanticDecorationRole::TypeAlias;
        eligible = true;
        break;
    case CollectorKind::Parameter:
    case CollectorKind::Localparam:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
        candidate = SemanticDecorationRole::Parameter;
        eligible = true;
        break;
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        candidate = SemanticDecorationRole::ModulePort;
        eligible = true;
        break;
    default:
        break;
    }

    if (!eligible) {
        switch (record.declarationKind) {
        case DeclarationKind::Typedef:
        case DeclarationKind::Struct:
            candidate = SemanticDecorationRole::TypeAlias;
            eligible = true;
            break;
        case DeclarationKind::Parameter:
        case DeclarationKind::Localparam:
            candidate = SemanticDecorationRole::Parameter;
            eligible = true;
            break;
        case DeclarationKind::Port:
            candidate = SemanticDecorationRole::ModulePort;
            eligible = true;
            break;
        default:
            break;
        }
    }

    if (eligible && role)
        *role = candidate;
    return eligible;
}

QPair<int, int> rangeForRecord(const SemanticSymbolRecord& record,
                               const QTextDocument& document)
{
    const QTextBlock block =
        document.findBlockByNumber(record.location.startLine - 1);
    if (block.isValid()) {
        const int nameStart =
            findNameOnLine(block.text(),
                           record.name,
                           record.location.startColumn);
        if (nameStart >= 0)
            return qMakePair(block.position() + nameStart, record.name.size());
    }

    if (record.location.position > 0 && record.location.length > 0)
        return qMakePair(record.location.position, record.location.length);

    int length = record.name.size();
    if (record.location.endLine == record.location.startLine
        && record.location.endColumn > record.location.startColumn) {
        length = record.location.endColumn - record.location.startColumn;
    }
    if (block.isValid() && record.location.startColumn > 0)
        return qMakePair(block.position() + record.location.startColumn - 1,
                         length);
    return qMakePair(-1, 0);
}

void appendRecordDecoration(const SemanticSymbolRecord& record,
                            const QTextDocument& document,
                            QList<SemanticDecoration>* out)
{
    if (!shouldDecorateRecord(record) || !out)
        return;

    SemanticDecoration decoration;
    decoration.role = roleForRecord(record);
    decoration.text = record.name;
    const QPair<int, int> range = rangeForRecord(record, document);
    decoration.startPosition = range.first;
    decoration.length = range.second;
    decoration.symbolRecord = record;
    if (decoration.isValid())
        out->append(decoration);
}

bool decorationRangeOccupied(const QList<SemanticDecoration>& decorations,
                             int startPosition,
                             int length)
{
    for (const SemanticDecoration& decoration : decorations) {
        if (decoration.startPosition == startPosition
            && decoration.length == length) {
            return true;
        }
    }
    return false;
}

struct UsageDecorationCandidate {
    SemanticDecorationRole role = SemanticDecorationRole::ActualSignal;
    SemanticSymbolRecord record;
};

void appendUsageDecorations(const QList<SemanticSymbolRecord>& records,
                            const QTextDocument& document,
                            QList<SemanticDecoration>* out)
{
    if (!out)
        return;

    QHash<QString, UsageDecorationCandidate> candidates;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name.isEmpty())
            continue;
        SemanticDecorationRole role = SemanticDecorationRole::ActualSignal;
        if (!usageDecorationRoleForRecord(record, &role))
            continue;
        const int priority = usageRolePriority(role);
        const auto existing = candidates.constFind(record.name);
        if (existing != candidates.constEnd()
            && usageRolePriority(existing.value().role) >= priority) {
            continue;
        }

        UsageDecorationCandidate candidate;
        candidate.role = role;
        candidate.record = record;
        candidates.insert(record.name, candidate);
    }
    if (candidates.isEmpty())
        return;

    for (QTextBlock block = document.firstBlock();
         block.isValid();
         block = block.next()) {
        const QString line = block.text();
        const QList<QPair<int, int>> spans = identifierSpansInCodeLine(line);
        for (const QPair<int, int>& span : spans) {
            const QString token = line.mid(span.first, span.second);
            const auto candidate = candidates.constFind(token);
            if (candidate == candidates.constEnd())
                continue;

            const int startPosition = block.position() + span.first;
            if (decorationRangeOccupied(*out, startPosition, span.second))
                continue;

            SemanticDecoration decoration;
            decoration.role = candidate.value().role;
            decoration.text = token;
            decoration.startPosition = startPosition;
            decoration.length = span.second;
            decoration.symbolRecord = candidate.value().record;
            if (decoration.isValid())
                out->append(decoration);
        }
    }
}

int findActualSignalStart(const QString& line, int zeroBasedAfterFormal)
{
    const int openParen = line.indexOf(QLatin1Char('('), zeroBasedAfterFormal);
    if (openParen < 0)
        return -1;

    int pos = openParen + 1;
    while (pos < line.size() && line.at(pos).isSpace())
        ++pos;
    if (pos >= line.size() || !isIdentifierStart(line.at(pos)))
        return -1;
    return pos;
}

void appendActualSignalDecoration(const SemanticSymbolRecord& formalRecord,
                                  const QTextDocument& document,
                                  QList<SemanticDecoration>* out)
{
    if (!out
        || formalRecord.collectorKind
            != SymbolTaxonomy::CollectorKind::InstPin
        || formalRecord.location.startLine <= 0) {
        return;
    }

    const QTextBlock block =
        document.findBlockByNumber(formalRecord.location.startLine - 1);
    if (!block.isValid())
        return;

    const QString line = block.text();
    const int actualStart =
        findActualSignalStart(line, formalRecord.location.startColumn);
    if (actualStart < 0)
        return;

    int actualEnd = actualStart + 1;
    while (actualEnd < line.size() && isIdentifierPart(line.at(actualEnd)))
        ++actualEnd;

    SemanticDecoration decoration;
    decoration.role = SemanticDecorationRole::ActualSignal;
    decoration.text = line.mid(actualStart, actualEnd - actualStart);
    decoration.startPosition = block.position() + actualStart;
    decoration.length = actualEnd - actualStart;
    decoration.symbolRecord = formalRecord;
    if (decoration.isValid())
        out->append(decoration);
}

void appendSystemTaskDecorations(const QTextDocument& document,
                                 QList<SemanticDecoration>* out)
{
    if (!out)
        return;

    for (QTextBlock block = document.firstBlock();
         block.isValid();
         block = block.next()) {
        const QString line = block.text();
        const int lineCommentStart = line.indexOf(QStringLiteral("//"));
        int pos = 0;
        while (pos < line.size()) {
            const int dollar = line.indexOf(QLatin1Char('$'), pos);
            if (dollar < 0 || dollar + 1 >= line.size())
                break;
            if (lineCommentStart >= 0 && dollar > lineCommentStart)
                break;
            if (!isIdentifierStart(line.at(dollar + 1))) {
                pos = dollar + 1;
                continue;
            }

            int end = dollar + 2;
            while (end < line.size() && isIdentifierPart(line.at(end)))
                ++end;

            SemanticDecoration decoration;
            decoration.role = SemanticDecorationRole::SystemTask;
            decoration.text = line.mid(dollar, end - dollar);
            decoration.startPosition = block.position() + dollar;
            decoration.length = end - dollar;
            if (decoration.isValid())
                out->append(decoration);
            pos = end;
        }
    }
}

bool startsBefore(const SemanticDecoration& lhs,
                  const SemanticDecoration& rhs)
{
    if (lhs.startPosition != rhs.startPosition)
        return lhs.startPosition < rhs.startPosition;
    return lhs.length > rhs.length;
}
}

SemanticDecorationService* SemanticDecorationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticDecorationService>();
    return instance.get();
}

SemanticDecorationService::SemanticDecorationService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SemanticDecorationService::~SemanticDecorationService() = default;

void SemanticDecorationService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SemanticIndex* SemanticDecorationService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

SemanticDecorationReport SemanticDecorationService::decorationsForDocument(
    const SemanticDecorationQuery& query) const
{
    SemanticDecorationReport report;
    if (query.fileName.isEmpty())
        return report;

    QTextDocument document(query.documentText);
    const QList<SemanticSymbolRecord> records =
        semanticIndex()->getSymbolRecords(query.fileName);
    for (const SemanticSymbolRecord& record : records) {
        appendRecordDecoration(record, document, &report.decorations);
        appendActualSignalDecoration(record, document, &report.decorations);
    }
    appendUsageDecorations(records, document, &report.decorations);
    appendSystemTaskDecorations(document, &report.decorations);

    std::stable_sort(report.decorations.begin(),
                     report.decorations.end(),
                     startsBefore);
    return report;
}
