#ifndef PINLOOMCODELINKSTORE_H
#define PINLOOMCODELINKSTORE_H

#include "zeroslackexport.h"

#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantMap>

enum class PinloomCodeLinkResolution {
    Exact,
    Moved,
    Ambiguous,
    Missing
};

struct ZEROSLACK_API PinloomSourceSelection {
    QString workspaceRoot;
    QString relativeFilePath;
    QString absoluteFilePath;
    QString moduleName;
    QString selectedText;
    QString selectedTextHash;
    QString prefixContext;
    QString suffixContext;
    int startPosition = -1;
    int endPosition = -1;
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;

    bool isValid() const;
    QString suggestedTitle() const;
    QVariantMap toVariantMap() const;

    static PinloomSourceSelection fromVariantMap(const QVariantMap& map);
    static PinloomSourceSelection fromDocumentSelection(
        const QString& workspaceRoot,
        const QString& filePath,
        const QString& moduleName,
        const QString& documentText,
        int startPosition,
        int endPosition);
};

struct ZEROSLACK_API PinloomCodeLinkRecord {
    QString id;
    QString title;
    QUrl uri;
    QVariantMap identity;
    PinloomSourceSelection source;
    QString createdAtUtc;

    bool isValid() const;
};

struct ZEROSLACK_API ResolvedPinloomCodeLink {
    PinloomCodeLinkRecord record;
    PinloomCodeLinkResolution resolution =
        PinloomCodeLinkResolution::Missing;
    int startPosition = -1;
    int endPosition = -1;
    int firstLine = -1;
    int lastLine = -1;

    bool available() const
    {
        return resolution == PinloomCodeLinkResolution::Exact
            || resolution == PinloomCodeLinkResolution::Moved;
    }
};

class ZEROSLACK_API PinloomCodeLinkStore
{
public:
    static constexpr int kVersion = 1;

    void setWorkspaceRoot(const QString& workspaceRoot);
    QString workspaceRoot() const;
    QString storagePath() const;

    bool addLink(const PinloomSourceSelection& source,
                 const QUrl& uri,
                 const QString& title,
                 const QVariantMap& identity,
                 QString* failureReason = nullptr);
    QList<ResolvedPinloomCodeLink> linksForDocument(
        const QString& filePath,
        const QString& documentText) const;
    QList<ResolvedPinloomCodeLink> linksAtPosition(
        const QString& filePath,
        const QString& documentText,
        int position) const;
    QList<PinloomCodeLinkRecord> records() const;

private:
    QString root;
    QList<PinloomCodeLinkRecord> linkRecords;

    bool load(QString* failureReason = nullptr);
    bool save(QString* failureReason = nullptr) const;
};

#endif // PINLOOMCODELINKSTORE_H
