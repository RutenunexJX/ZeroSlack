#ifndef PINLOOMCODELINKSTORE_H
#define PINLOOMCODELINKSTORE_H

#include "zeroslackexport.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <functional>

class TSDocument;
struct SemanticSymbolRecord;
struct TSBindableCodeAnchor;

enum class PinloomCodeLinkResolution {
    Exact,
    Moved,
    Ambiguous,
    Missing
};

enum class PinloomCodeAnchorKind {
    LegacySelection,
    Symbol,
    AlwaysBlock,
    ContinuousAssign
};

struct ZEROSLACK_API PinloomSourceSelection {
    QString anchorId;
    PinloomCodeAnchorKind anchorKind =
        PinloomCodeAnchorKind::LegacySelection;
    QString workspaceRoot;
    QString relativeFilePath;
    QString absoluteFilePath;
    QString moduleName;
    QString selectedText;
    QString selectedTextHash;
    QString prefixContext;
    QString suffixContext;
    QString logicalKey;
    QString structuralFingerprint;
    QString syntaxKind;
    QString ownerScope;
    QString symbolName;
    QStringList semanticTokens;
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
    static PinloomSourceSelection fromSemanticSymbol(
        const QString& workspaceRoot,
        const QString& documentText,
        const SemanticSymbolRecord& symbol);
    static PinloomSourceSelection fromSyntaxAnchor(
        const QString& workspaceRoot,
        const QString& filePath,
        const QString& documentText,
        const TSBindableCodeAnchor& anchor);
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

struct ZEROSLACK_API PinloomCodeLinkAnchorRecord {
    QString id;
    PinloomSourceSelection source;
    QList<PinloomCodeLinkRecord> links;
    QString createdAtUtc;

    bool isValid() const;
};

struct ZEROSLACK_API ResolvedPinloomCodeLink {
    PinloomCodeLinkAnchorRecord anchor;
    // First target retained as a compatibility convenience. New consumers
    // should use anchor.links so one source anchor can expose every target.
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

    int linkCount() const { return anchor.links.size(); }
};

class ZEROSLACK_API PinloomCodeLinkStore
{
public:
    static constexpr int kVersion = 2;

    void setWorkspaceRoot(const QString& workspaceRoot);
    QString workspaceRoot() const;
    QString storagePath() const;
    QString loadFailureReason() const;

    bool addLink(const PinloomSourceSelection& source,
                 const QUrl& uri,
                 const QString& title,
                 const QVariantMap& identity,
                 QString* failureReason = nullptr);
    QList<ResolvedPinloomCodeLink> linksForDocument(
        const QString& filePath,
        const QString& documentText,
        const TSDocument* syntaxDocument = nullptr,
        const QList<SemanticSymbolRecord>* semanticSymbols = nullptr) const;
    QList<ResolvedPinloomCodeLink> linksAtPosition(
        const QString& filePath,
        const QString& documentText,
        int position,
        const TSDocument* syntaxDocument = nullptr,
        const QList<SemanticSymbolRecord>* semanticSymbols = nullptr) const;
    ResolvedPinloomCodeLink anchorByIdForDocument(
        const QString& anchorId,
        const QString& filePath,
        const QString& documentText,
        const TSDocument* syntaxDocument = nullptr,
        const QList<SemanticSymbolRecord>* semanticSymbols = nullptr) const;
    QList<PinloomCodeLinkAnchorRecord> anchors() const;
    QList<PinloomCodeLinkRecord> records() const;
    void setChangedHandler(std::function<void()> handler);

private:
    QString root;
    QList<PinloomCodeLinkAnchorRecord> anchorRecords;
    std::function<void()> changedHandler;
    QString loadFailureValue;

    bool load(QString* failureReason = nullptr);
    bool save(QString* failureReason = nullptr) const;
};

#endif // PINLOOMCODELINKSTORE_H
