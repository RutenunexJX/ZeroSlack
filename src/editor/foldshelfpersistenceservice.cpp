#include "foldshelfpersistenceservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>

#include <memory>

std::unique_ptr<FoldShelfPersistenceService>
    FoldShelfPersistenceService::instance = nullptr;

namespace {
constexpr const char* kFoldShelfGroup = "foldShelf";
constexpr const char* kFoldShelfVersion = "v1";
constexpr const char* kFoldShelfWorkspaces = "workspaces";
constexpr const char* kFoldShelfWorkspaceRoot = "workspaceRoot";
constexpr const char* kFoldShelfItems = "items";
constexpr const char* kFoldShelfId = "id";
constexpr const char* kFoldShelfAlias = "alias";
constexpr const char* kFoldShelfText = "text";
constexpr const char* kFoldShelfSourceFile = "sourceFile";
constexpr const char* kFoldShelfSourceModule = "sourceModule";
constexpr const char* kFoldShelfSourceStartLine = "sourceStartLine";
constexpr const char* kFoldShelfSourceEndLine = "sourceEndLine";
constexpr const char* kFoldShelfLineCount = "lineCount";
constexpr const char* kFoldShelfOriginKind = "originKind";
constexpr const char* kFoldShelfConsumed = "consumed";
constexpr const char* kFoldShelfStale = "stale";

std::unique_ptr<QSettings> makeSettings(const QString& settingsFilePath)
{
    if (!settingsFilePath.isEmpty())
        return std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("ZeroSlack"),
                                       QStringLiteral("ZeroSlack"));
}
QString originToString(FoldShelfOriginKind origin)
{
    return origin == FoldShelfOriginKind::Moved
        ? QStringLiteral("moved")
        : QStringLiteral("copied");
}

FoldShelfOriginKind originFromString(const QString& origin)
{
    return origin == QStringLiteral("moved")
        ? FoldShelfOriginKind::Moved
        : FoldShelfOriginKind::Copied;
}

QString scopeKeyForWorkspaceRoot(const QString& workspaceRoot)
{
    const QString normalized =
        FoldShelfPersistenceService::normalizedWorkspaceRoot(workspaceRoot);
    if (normalized.isEmpty())
        return QStringLiteral("global");

    const QByteArray hash = QCryptographicHash::hash(
        normalized.toUtf8(),
        QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

int lineCountForText(const QString& text)
{
    if (text.isEmpty())
        return 0;
    int count = text.count(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n')))
        ++count;
    return count;
}

FoldShelfItem normalizedItem(FoldShelfItem item)
{
    item.id = item.id.trimmed();
    item.alias = item.alias.trimmed();
    item.sourceModule = item.sourceModule.trimmed();
    item.sourceFile =
        FoldShelfPersistenceService::normalizedSourceFile(item.sourceFile);
    item.lineCount = lineCountForText(item.text);
    if (item.alias.isEmpty())
        item.alias = item.id;
    return item;
}

void beginWorkspaceGroup(QSettings* settings, const QString& workspaceRoot)
{
    settings->beginGroup(QString::fromLatin1(kFoldShelfGroup));
    settings->beginGroup(QString::fromLatin1(kFoldShelfVersion));
    settings->beginGroup(QString::fromLatin1(kFoldShelfWorkspaces));
    settings->beginGroup(scopeKeyForWorkspaceRoot(workspaceRoot));
}

void endWorkspaceGroup(QSettings* settings)
{
    settings->endGroup();
    settings->endGroup();
    settings->endGroup();
    settings->endGroup();
}
}

FoldShelfPersistenceService* FoldShelfPersistenceService::getInstance()
{
    if (!instance)
        instance = std::make_unique<FoldShelfPersistenceService>();
    return instance.get();
}

FoldShelfPersistenceService::FoldShelfPersistenceService(const QString& path)
    : settingsFilePath(path)
{
}

FoldShelfPersistenceService::~FoldShelfPersistenceService() = default;

QString FoldShelfPersistenceService::storageLocation() const
{
    if (!settingsFilePath.isEmpty())
        return settingsFilePath;
    return QStringLiteral("QSettings:ZeroSlack/ZeroSlack/foldShelf/v1");
}

QString FoldShelfPersistenceService::normalizedWorkspaceRoot(
    const QString& workspaceRoot)
{
    const QString trimmed = workspaceRoot.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(trimmed).absoluteFilePath()));
}

QString FoldShelfPersistenceService::normalizedSourceFile(
    const QString& sourceFile)
{
    const QString trimmed = sourceFile.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(trimmed).absoluteFilePath()));
}

QList<FoldShelfItem> FoldShelfPersistenceService::loadItems(
    const QString& workspaceRoot) const
{
    QList<FoldShelfItem> result;
    QSet<QString> seenIds;

    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    beginWorkspaceGroup(settings.get(), workspaceRoot);
    const int count =
        settings->beginReadArray(QString::fromLatin1(kFoldShelfItems));
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings->setArrayIndex(i);
        FoldShelfItem item;
        item.id =
            settings->value(QString::fromLatin1(kFoldShelfId)).toString();
        item.alias =
            settings->value(QString::fromLatin1(kFoldShelfAlias)).toString();
        item.text =
            settings->value(QString::fromLatin1(kFoldShelfText)).toString();
        item.sourceFile =
            settings->value(QString::fromLatin1(kFoldShelfSourceFile)).toString();
        item.sourceModule =
            settings->value(QString::fromLatin1(kFoldShelfSourceModule)).toString();
        item.sourceStartLine =
            settings->value(QString::fromLatin1(kFoldShelfSourceStartLine), -1).toInt();
        item.sourceEndLine =
            settings->value(QString::fromLatin1(kFoldShelfSourceEndLine), -1).toInt();
        item.lineCount =
            settings->value(QString::fromLatin1(kFoldShelfLineCount), 0).toInt();
        item.originKind = originFromString(
            settings->value(QString::fromLatin1(kFoldShelfOriginKind)).toString());
        item.consumed =
            settings->value(QString::fromLatin1(kFoldShelfConsumed), false).toBool();
        item.stale =
            settings->value(QString::fromLatin1(kFoldShelfStale), false).toBool();
        item = normalizedItem(item);
        const QString lowerId = item.id.toLower();
        if (item.id.isEmpty()
            || item.text.isEmpty()
            || seenIds.contains(lowerId)) {
            continue;
        }
        seenIds.insert(lowerId);
        result.append(item);
    }
    settings->endArray();
    endWorkspaceGroup(settings.get());
    return result;
}

bool FoldShelfPersistenceService::saveItems(
    const QString& workspaceRoot,
    const QList<FoldShelfItem>& items) const
{
    std::unique_ptr<QSettings> settings = makeSettings(settingsFilePath);
    beginWorkspaceGroup(settings.get(), workspaceRoot);
    settings->remove(QString());
    settings->setValue(QString::fromLatin1(kFoldShelfWorkspaceRoot),
                       normalizedWorkspaceRoot(workspaceRoot));
    settings->beginWriteArray(QString::fromLatin1(kFoldShelfItems));

    int outputIndex = 0;
    QSet<QString> seenIds;
    for (FoldShelfItem item : items) {
        item = normalizedItem(item);
        const QString lowerId = item.id.toLower();
        if (item.id.isEmpty()
            || item.text.isEmpty()
            || seenIds.contains(lowerId)) {
            continue;
        }
        seenIds.insert(lowerId);
        settings->setArrayIndex(outputIndex++);
        settings->setValue(QString::fromLatin1(kFoldShelfId), item.id);
        settings->setValue(QString::fromLatin1(kFoldShelfAlias), item.alias);
        settings->setValue(QString::fromLatin1(kFoldShelfText), item.text);
        settings->setValue(QString::fromLatin1(kFoldShelfSourceFile),
                           item.sourceFile);
        settings->setValue(QString::fromLatin1(kFoldShelfSourceModule),
                           item.sourceModule);
        settings->setValue(QString::fromLatin1(kFoldShelfSourceStartLine),
                           item.sourceStartLine);
        settings->setValue(QString::fromLatin1(kFoldShelfSourceEndLine),
                           item.sourceEndLine);
        settings->setValue(QString::fromLatin1(kFoldShelfLineCount),
                           item.lineCount);
        settings->setValue(QString::fromLatin1(kFoldShelfOriginKind),
                           originToString(item.originKind));
        settings->setValue(QString::fromLatin1(kFoldShelfConsumed),
                           item.consumed);
        settings->setValue(QString::fromLatin1(kFoldShelfStale), item.stale);
    }

    settings->endArray();
    endWorkspaceGroup(settings.get());
    settings->sync();
    return settings->status() == QSettings::NoError;
}
