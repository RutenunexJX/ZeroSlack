#include "semanticindex.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <Qt>
#include <algorithm>

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

namespace {
QString normalizedStableKeyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();

    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(fileName));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString normalizedAnalysisBandReportFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

SemanticAnalysisBandMetadata normalizedAnalysisBandMetadata(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (metadata.isValid())
        return metadata;

    SemanticAnalysisBandMetadata unbanded;
    unbanded.label = QStringLiteral("unbanded");
    unbanded.displayName = QStringLiteral("unbanded");
    return unbanded;
}

int analysisBandReportItemSortPriority(
    const SemanticAnalysisBandReportItem& item)
{
    SemanticAnalysisBandMetadata metadata;
    metadata.label = item.label;
    metadata.displayName = item.displayName;
    metadata.priority = item.priority;
    metadata.publicationCheckpoint = item.publicationCheckpoint;
    return semanticAnalysisBandSortPriority(metadata);
}
}

bool SymbolStableKey::isValid() const
{
    return !symbolName.isEmpty();
}

QString SymbolStableKey::toString() const
{
    return symbolStableKeyText(*this);
}

bool SymbolStableKey::operator==(const SymbolStableKey& other) const
{
    return normalizedStableKeyFileName(fileName)
            == normalizedStableKeyFileName(other.fileName)
        && symbolName == other.symbolName
        && declarationKind == other.declarationKind
        && ownerScope == other.ownerScope
        && sourcePosition == other.sourcePosition
        && sourceLength == other.sourceLength;
}

bool SemanticSymbolLocation::isValid() const
{
    return !fileName.isEmpty() && startLine > 0;
}

bool SemanticSymbolOwner::isValid() const
{
    return kind != SymbolTaxonomy::SymbolOwnerScope::Unknown
        || !name.isEmpty()
        || stableKey.isValid();
}

bool SemanticSymbolTypeReference::isValid() const
{
    return !rawTypeText.isEmpty()
        || !resolvedTypeName.isEmpty()
        || resolvedTypeKind != SymbolTaxonomy::DeclarationKind::Unknown
        || !modportName.isEmpty()
        || stableKey.isValid();
}

bool SemanticAnalysisBandMetadata::isValid() const
{
    return !label.isEmpty();
}

bool SemanticAnalysisBandReportItem::isValid() const
{
    return !label.isEmpty();
}

QString SemanticAnalysisBandReport::summaryText() const
{
    if (bands.isEmpty())
        return QStringLiteral("bands none");

    QStringList parts;
    for (const SemanticAnalysisBandReportItem& item : bands) {
        const QString displayName =
            item.displayName.isEmpty() ? item.label : item.displayName;
        const QString symbolWord =
            item.symbolCount == 1
                ? QStringLiteral("symbol")
                : QStringLiteral("symbols");
        const QString fileWord =
            item.fileCount == 1
                ? QStringLiteral("file")
                : QStringLiteral("files");
        parts.append(QStringLiteral("%1 %2 %3/%4 %5")
                         .arg(displayName)
                         .arg(item.symbolCount)
                         .arg(symbolWord)
                         .arg(item.fileCount)
                         .arg(fileWord));
    }
    return QStringLiteral("bands %1").arg(parts.join(QStringLiteral(", ")));
}

bool SemanticSymbolRecord::isValid() const
{
    return stableKey.isValid() || !name.isEmpty() || localHandle >= 0;
}

SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.collectorKind = record.collectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString semanticAnalysisBandDisplayName(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (!metadata.displayName.isEmpty())
        return metadata.displayName;
    return metadata.label;
}

SemanticAnalysisBandReport semanticAnalysisBandReportForRecords(
    const QList<SemanticSymbolRecord>& records)
{
    SemanticAnalysisBandReport report;
    report.totalSymbolCount = records.size();

    QHash<QString, SemanticAnalysisBandReportItem> itemsByLabel;
    QHash<QString, QSet<QString>> fileKeysByLabel;
    QSet<QString> totalFileKeys;

    for (const SemanticSymbolRecord& record : records) {
        const SemanticAnalysisBandMetadata metadata =
            normalizedAnalysisBandMetadata(record.analysisBand);
        const QString label = metadata.label;
        if (label.isEmpty())
            continue;

        if (!itemsByLabel.contains(label)) {
            SemanticAnalysisBandReportItem item;
            item.label = label;
            item.displayName = semanticAnalysisBandDisplayName(metadata);
            item.priority = metadata.priority;
            item.publicationCheckpoint = metadata.publicationCheckpoint;
            itemsByLabel.insert(label, item);
        }

        SemanticAnalysisBandReportItem item = itemsByLabel.value(label);
        item.symbolCount += 1;
        if (metadata.priority)
            item.priority = true;
        if (item.displayName.isEmpty())
            item.displayName = semanticAnalysisBandDisplayName(metadata);
        if (item.publicationCheckpoint <= 0
            || (metadata.publicationCheckpoint > 0
                && metadata.publicationCheckpoint
                    < item.publicationCheckpoint)) {
            item.publicationCheckpoint = metadata.publicationCheckpoint;
        }

        const QString normalizedFile =
            normalizedAnalysisBandReportFileName(record.location.fileName);
        if (!normalizedFile.isEmpty()) {
            QSet<QString> fileKeys = fileKeysByLabel.value(label);
            if (!fileKeys.contains(normalizedFile)) {
                fileKeys.insert(normalizedFile);
                item.files.append(record.location.fileName);
            }
            fileKeysByLabel.insert(label, fileKeys);
            totalFileKeys.insert(normalizedFile);
        }

        itemsByLabel.insert(label, item);
    }

    report.totalFileCount = totalFileKeys.size();
    report.bands = itemsByLabel.values();
    for (SemanticAnalysisBandReportItem& item : report.bands) {
        item.fileCount = fileKeysByLabel.value(item.label).size();
        item.files.sort(Qt::CaseInsensitive);
    }
    std::sort(report.bands.begin(),
              report.bands.end(),
              [](const SemanticAnalysisBandReportItem& left,
                 const SemanticAnalysisBandReportItem& right) {
        const int leftPriority = analysisBandReportItemSortPriority(left);
        const int rightPriority = analysisBandReportItemSortPriority(right);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;
        if (left.publicationCheckpoint != right.publicationCheckpoint)
            return left.publicationCheckpoint < right.publicationCheckpoint;
        return QString::compare(left.label,
                                right.label,
                                Qt::CaseInsensitive) < 0;
    });

    return report;
}

int semanticAnalysisBandSortPriority(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (metadata.label == QStringLiteral("current"))
        return 0;
    if (metadata.label == QStringLiteral("dirty-open"))
        return 1;
    if (metadata.label == QStringLiteral("open"))
        return 2;
    if (metadata.label == QStringLiteral("background"))
        return 3;
    if (metadata.priority)
        return 2;
    if (metadata.isValid())
        return 3;
    return 4;
}

int semanticSymbolAnalysisBandSortPriority(
    const SemanticSymbolRecord& record)
{
    return semanticAnalysisBandSortPriority(record.analysisBand);
}

QString symbolStableKeyText(const SymbolStableKey& key)
{
    if (!key.isValid())
        return QString();

    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(normalizedStableKeyFileName(key.fileName),
             QString::number(static_cast<int>(key.declarationKind)),
             key.ownerScope,
             key.symbolName,
             QString::number(key.sourcePosition),
             QString::number(key.sourceLength));
}

QString semanticRelationshipStableKeyText(
    const SemanticRelationship& relationship)
{
    const QString fromKey = symbolStableKeyText(relationship.fromStableKey);
    const QString toKey = symbolStableKeyText(relationship.toStableKey);
    if (fromKey.isEmpty() || toKey.isEmpty())
        return QString();

    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(QString::number(static_cast<int>(relationship.type)),
              fromKey,
              toKey,
              relationship.fromAccessPath,
              relationship.toAccessPath);
}

SemanticIndex* SemanticIndex::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticIndex>();
    return instance.get();
}

SemanticIndex::SemanticIndex()
{
}

SemanticIndex::~SemanticIndex() = default;
