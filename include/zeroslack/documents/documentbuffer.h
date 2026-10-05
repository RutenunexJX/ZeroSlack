#pragma once
#include <zeroslack/documents/documentsapi.h>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <cstdint>
class QTextDocument;

enum class SharedDocumentExternalState {
    Current,
    ExternallyModified,
    Conflict
};

// Owns text, revisions and saved disk identity independently of editor widgets.
class ZEROSLACK_DOCUMENTS_API DocumentBuffer : public QObject {
    Q_OBJECT
public:
    DocumentBuffer(const QString& documentId, const QString& fileName, const QString& initialText,
                   QObject* parent = nullptr, QTextDocument* ownedDocument = nullptr);
    ~DocumentBuffer() override = default;
    QString documentId() const;
    QString fileName() const;
    QTextDocument* textDocument() const;
    std::uint64_t textRevision() const;
    std::uint64_t savedTextRevision() const;
    QByteArray savedBaselineSha256() const;
    QDateTime savedBaselineModifiedUtc() const;
    bool dirty() const;
    bool readOnly() const;
    SharedDocumentExternalState externalState() const;

    void resetText(const QString& text,
                   std::uint64_t revision = 0,
                   bool markClean = true);
    void restoreSavedBaseline(
        const QByteArray& sha256,
        const QDateTime& modifiedUtc);
    void markSaved();
    void markSaved(const QByteArray& sha256,
                   const QDateTime& modifiedUtc);
    virtual void setReadOnly(bool readOnly);
    void setExternalState(SharedDocumentExternalState state);
signals:
    void textRevisionChanged(std::uint64_t revision);
    void dirtyChanged(bool dirty);
    void identityChanged(const QString& previousDocumentId,
                         const QString& previousFileName,
                         const QString& documentId,
                         const QString& fileName);
    void statusChanged();

protected:
    QString id;
    QString normalizedFileName;
    QTextDocument* document = nullptr;
    std::uint64_t revision = 0;
    std::uint64_t savedRevision = 0;
    QByteArray savedBaselineDigest;
    QDateTime savedBaselineModifiedTimeUtc;
    bool loadingText = false;
    bool readOnlyState = false;
    bool lastDirtyState = false;
    SharedDocumentExternalState externalFileState =
        SharedDocumentExternalState::Current;
    void handleContentsChange();
    void publishDirtyIfChanged();
    void captureSavedBaseline();
};
