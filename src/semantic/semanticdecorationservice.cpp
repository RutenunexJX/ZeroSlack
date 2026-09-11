#include "semanticdecorationservice.h"

#include <QChar>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <utility>

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
    case CollectorKind::MacroReference:
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
    case CollectorKind::MacroReference:
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

void appendInactiveBranchDecoration(const SemanticSymbolRecord& record,
                                    QList<SemanticDecoration>* out)
{
    if (!out
        || record.collectorKind
            != SymbolTaxonomy::CollectorKind::InactivePreprocessorBranch
        || !record.location.isValid()
        || record.location.position < 0
        || record.location.length <= 0) {
        return;
    }

    SemanticDecoration decoration;
    decoration.role = SemanticDecorationRole::InactivePreprocessorBranch;
    decoration.text = record.name;
    decoration.startPosition = record.location.position;
    decoration.length = record.location.length;
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

QString moduleNameForDecorationLine(
    const QList<SemanticSymbolRecord>& localRecords,
    int oneBasedLine)
{
    const SemanticSymbolRecord* containing = nullptr;
    for (const SemanticSymbolRecord& record : localRecords) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Module
            || record.location.startLine <= 0
            || record.location.endLine < record.location.startLine
            || oneBasedLine < record.location.startLine
            || oneBasedLine > record.location.endLine) {
            continue;
        }
        if (!containing
            || record.location.startLine
                > containing->location.startLine) {
            containing = &record;
        }
    }
    return containing ? containing->name : QString();
}

QString qualifiedPackageBeforeIdentifier(const QString& line,
                                         int identifierStart)
{
    int pos = identifierStart - 1;
    while (pos >= 0 && line.at(pos).isSpace())
        --pos;
    if (pos < 1 || line.at(pos) != QLatin1Char(':')
        || line.at(pos - 1) != QLatin1Char(':')) {
        return QString();
    }
    pos -= 2;
    while (pos >= 0 && line.at(pos).isSpace())
        --pos;
    const int end = pos + 1;
    while (pos >= 0 && isIdentifierPart(line.at(pos)))
        --pos;
    const int start = pos + 1;
    if (start >= end || !isIdentifierStart(line.at(start)))
        return QString();
    return line.mid(start, end - start);
}

const QList<SemanticSymbolRecord>& cachedRecordsByName(
    SemanticIndex* index,
    const QString& name,
    QHash<QString, QList<SemanticSymbolRecord>>* cache)
{
    auto it = cache->find(name);
    if (it == cache->end()) {
        it = cache->insert(
            name,
            index ? index->getSymbolRecordsByName(name)
                  : QList<SemanticSymbolRecord>());
    }
    return it.value();
}

void effectiveDecorationOwners(
    SemanticIndex* index,
    const SemanticSymbolRecord& record,
    QHash<QString, QList<SemanticSymbolRecord>>* cache,
    QSet<QString>* moduleOwners,
    QSet<QString>* packageOwners)
{
    if (!moduleOwners || !packageOwners)
        return;
    auto addOwner = [moduleOwners, packageOwners](
                        const SemanticSymbolRecord& candidate) {
        if (candidate.owner.name.isEmpty())
            return;
        if (candidate.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Package
            || candidate.visibility
                == SymbolTaxonomy::SymbolVisibility::PackageVisible) {
            packageOwners->insert(candidate.owner.name);
        } else if (candidate.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Module
                   || candidate.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Interface
                   || candidate.visibility
                       == SymbolTaxonomy::SymbolVisibility::ScopeLocal) {
            moduleOwners->insert(candidate.owner.name);
        }
    };

    addOwner(record);
    if (record.owner.name.isEmpty())
        return;
    for (const SemanticSymbolRecord& ownerRecord :
         cachedRecordsByName(index, record.owner.name, cache)) {
        if (ownerRecord.declarationKind
                != SymbolTaxonomy::DeclarationKind::Typedef
            && ownerRecord.declarationKind
                != SymbolTaxonomy::DeclarationKind::Enum
            && ownerRecord.declarationKind
                != SymbolTaxonomy::DeclarationKind::Struct) {
            continue;
        }
        addOwner(ownerRecord);
    }
}

struct VisibleUsageCandidate {
    UsageDecorationCandidate decoration;
    int priority = -1;
    QString packageOwner;
};

VisibleUsageCandidate visibleUsageCandidate(
    SemanticIndex* index,
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context,
    const QString& qualifiedPackage,
    QHash<QString, QList<SemanticSymbolRecord>>* recordsByName)
{
    VisibleUsageCandidate result;
    if (!usageDecorationRoleForRecord(record, &result.decoration.role))
        return result;
    result.decoration.record = record;

    QSet<QString> moduleOwners;
    QSet<QString> packageOwners;
    effectiveDecorationOwners(index,
                              record,
                              recordsByName,
                              &moduleOwners,
                              &packageOwners);
    const int rolePriority = usageRolePriority(result.decoration.role);
    if (!context.moduleName.isEmpty()
        && moduleOwners.contains(context.moduleName)) {
        result.priority = 400 + rolePriority;
        return result;
    }

    for (const QString& packageOwner : std::as_const(packageOwners)) {
        if (!qualifiedPackage.isEmpty()
            && qualifiedPackage == packageOwner) {
            result.priority = 350 + rolePriority;
            result.packageOwner = packageOwner;
            return result;
        }
        SemanticSymbolRecord packageRecord = record;
        packageRecord.owner.kind =
            SymbolTaxonomy::SymbolOwnerScope::Package;
        packageRecord.owner.name = packageOwner;
        packageRecord.visibility =
            SymbolTaxonomy::SymbolVisibility::PackageVisible;
        if (index
            && index->packageVisibleRecordImported(packageRecord, context)) {
            result.priority = 300 + rolePriority;
            result.packageOwner = packageOwner;
            return result;
        }
    }

    if (record.visibility == SymbolTaxonomy::SymbolVisibility::Global
        || record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Global
        || record.owner.name.isEmpty()) {
        result.priority = 100 + rolePriority;
    }
    return result;
}

void appendUsageDecorations(
    SemanticIndex* index,
    const SemanticDecorationQuery& query,
    const QList<SemanticSymbolRecord>& localRecords,
    const QTextDocument& document,
    QList<SemanticDecoration>* out)
{
    if (!index || !out)
        return;

    QHash<QString, QList<SemanticSymbolRecord>> recordsByName;
    for (QTextBlock block = document.firstBlock();
         block.isValid();
         block = block.next()) {
        if (query.isCancelled && query.isCancelled())
            return;
        const QString line = block.text();
        const QList<QPair<int, int>> spans = identifierSpansInCodeLine(line);
        for (const QPair<int, int>& span : spans) {
            const QString token = line.mid(span.first, span.second);
            const int startPosition = block.position() + span.first;
            if (decorationRangeOccupied(*out, startPosition, span.second))
                continue;

            SemanticQueryContext context;
            context.fileName = query.fileName;
            context.moduleName = moduleNameForDecorationLine(
                localRecords, block.blockNumber() + 1);
            context.cursorLine = block.blockNumber() + 1;
            const QString qualifiedPackage =
                qualifiedPackageBeforeIdentifier(line, span.first);

            VisibleUsageCandidate best;
            bool ambiguousImportedPackages = false;
            const QList<SemanticSymbolRecord> tokenRecords =
                cachedRecordsByName(index, token, &recordsByName);
            for (const SemanticSymbolRecord& record : tokenRecords) {
                const VisibleUsageCandidate candidate =
                    visibleUsageCandidate(index,
                                          record,
                                          context,
                                          qualifiedPackage,
                                          &recordsByName);
                if (candidate.priority < 0)
                    continue;
                if (candidate.priority > best.priority) {
                    best = candidate;
                    ambiguousImportedPackages = false;
                } else if (candidate.priority == best.priority
                           && !candidate.packageOwner.isEmpty()
                           && !best.packageOwner.isEmpty()
                           && candidate.packageOwner
                               != best.packageOwner) {
                    ambiguousImportedPackages = true;
                }
            }
            if (best.priority < 0 || ambiguousImportedPackages)
                continue;

            SemanticDecoration decoration;
            decoration.role = best.decoration.role;
            decoration.text = token;
            decoration.startPosition = startPosition;
            decoration.length = span.second;
            decoration.symbolRecord = best.decoration.record;
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
                                 const std::function<bool()>& isCancelled,
                                 QList<SemanticDecoration>* out)
{
    if (!out)
        return;

    for (QTextBlock block = document.firstBlock();
         block.isValid();
         block = block.next()) {
        if (isCancelled && isCancelled())
            return;
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
    const bool leftInactive =
        lhs.role == SemanticDecorationRole::InactivePreprocessorBranch;
    const bool rightInactive =
        rhs.role == SemanticDecorationRole::InactivePreprocessorBranch;
    if (leftInactive != rightInactive)
        return !leftInactive;
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
        if (query.isCancelled && query.isCancelled())
            return report;
        appendInactiveBranchDecoration(record, &report.decorations);
        appendRecordDecoration(record, document, &report.decorations);
        appendActualSignalDecoration(record, document, &report.decorations);
    }
    appendUsageDecorations(semanticIndex(),
                           query,
                           records,
                           document,
                           &report.decorations);
    appendSystemTaskDecorations(document,
                                query.isCancelled,
                                &report.decorations);

    if (query.isCancelled && query.isCancelled()) {
        report.decorations.clear();
        return report;
    }

    std::stable_sort(report.decorations.begin(),
                     report.decorations.end(),
                     startsBefore);
    return report;
}
