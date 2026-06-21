#include "semanticdecorationservice.h"

#include <QChar>
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

SemanticDecorationRole roleForRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    switch (record.collectorKind) {
    case CollectorKind::Module:
    case CollectorKind::Interface:
        return SemanticDecorationRole::ModuleInterface;
    case CollectorKind::Package:
    case CollectorKind::Typedef:
    case CollectorKind::PackedStruct:
    case CollectorKind::UnpackedStruct:
    case CollectorKind::Enum:
    case CollectorKind::InterfaceModport:
        return SemanticDecorationRole::PackageClassType;
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
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
    case CollectorKind::PackedStructVariable:
    case CollectorKind::UnpackedStructVariable:
    case CollectorKind::EnumVariable:
        return SemanticDecorationRole::ActualSignal;
    default:
        break;
    }

    switch (record.declarationKind) {
    case DeclarationKind::Module:
    case DeclarationKind::Interface:
        return SemanticDecorationRole::ModuleInterface;
    case DeclarationKind::Package:
    case DeclarationKind::Typedef:
    case DeclarationKind::Struct:
    case DeclarationKind::Enum:
    case DeclarationKind::Modport:
        return SemanticDecorationRole::PackageClassType;
    case DeclarationKind::Instance:
        return SemanticDecorationRole::InstanceName;
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

int positionForRecord(const SemanticSymbolRecord& record,
                      const QTextDocument& document)
{
    if (record.location.position > 0)
        return record.location.position;

    const QTextBlock block =
        document.findBlockByNumber(record.location.startLine - 1);
    if (!block.isValid() || record.location.startColumn <= 0)
        return -1;
    return block.position() + record.location.startColumn - 1;
}

int lengthForRecord(const SemanticSymbolRecord& record)
{
    if (record.location.length > 0)
        return record.location.length;
    if (record.location.endLine == record.location.startLine
        && record.location.endColumn > record.location.startColumn) {
        return record.location.endColumn - record.location.startColumn;
    }
    return record.name.size();
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
    decoration.startPosition = positionForRecord(record, document);
    decoration.length = lengthForRecord(record);
    decoration.symbolRecord = record;
    if (decoration.isValid())
        out->append(decoration);
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
    appendSystemTaskDecorations(document, &report.decorations);

    std::stable_sort(report.decorations.begin(),
                     report.decorations.end(),
                     startsBefore);
    return report;
}
