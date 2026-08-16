#ifndef SHAREDDOCUMENT_H
#define SHAREDDOCUMENT_H

#include "zeroslackexport.h"

#include "editorfoldviewstate.h"

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>

class MyCodeEditor;
class QTextDocument;

struct SharedDocumentViewState {
    QString viewId;
    int cursorPosition = 0;
    int anchorPosition = 0;
    int verticalScrollValue = 0;
    int horizontalScrollValue = 0;
    EditorFoldViewState folding;
};

enum class SharedDocumentExternalState {
    Current,
    ExternallyModified,
    Conflict
};

class ZEROSLACK_API SharedDocument : public QObject
{
    Q_OBJECT

public:
    explicit SharedDocument(const QString& documentId,
                            const QString& fileName,
                            const QString& initialText,
                            QObject* parent = nullptr);
    ~SharedDocument() override;

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
    bool reloadCleanText(const QString& text);
    // Applies a user-confirmed external generation as one undoable
    // replacement. Unlike automatic clean reload, this entry point may
    // resolve a dirty conflict and deliberately keeps the rejected local
    // text reachable through the shared undo stack.
    bool acceptExternalText(const QString& text);
    bool restoreUnsavedText(
        const QString& text,
        std::uint64_t recoveredRevision);
    void restoreSavedBaseline(
        const QByteArray& sha256,
        const QDateTime& modifiedUtc);
    void markSaved();
    void markSaved(const QByteArray& sha256,
                   const QDateTime& modifiedUtc);
    void setReadOnly(bool readOnly);
    void setExternalState(SharedDocumentExternalState state);

    QString attachView(
        MyCodeEditor* editor,
        const SharedDocumentViewState& restoredState = {});
    bool detachView(MyCodeEditor* editor,
                    bool preserveIndependentCopy = true);
    int viewCount() const;
    QList<MyCodeEditor*> views() const;
    SharedDocumentViewState viewState(MyCodeEditor* editor) const;
    void captureViewState(MyCodeEditor* editor);

signals:
    void textRevisionChanged(std::uint64_t revision);
    void dirtyChanged(bool dirty);
    void identityChanged(const QString& previousDocumentId,
                         const QString& previousFileName,
                         const QString& documentId,
                         const QString& fileName);
    void viewAttached(const QString& viewId);
    void viewDetached(const QString& viewId);
    void statusChanged();

private:
    friend class SharedDocumentRegistry;

    struct ViewBinding {
        QPointer<MyCodeEditor> editor;
        SharedDocumentViewState state;
        QList<QMetaObject::Connection> connections;
    };

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
    QHash<MyCodeEditor*, ViewBinding> viewBindings;

    void handleContentsChange();
    void publishDirtyIfChanged();
    void captureSavedBaseline();
    void applyViewState(MyCodeEditor* editor,
                        const SharedDocumentViewState& state) const;
    void disconnectViewBinding(ViewBinding* binding);
    void setFileIdentity(const QString& documentId,
                         const QString& fileName);
};

class ZEROSLACK_API SharedDocumentRegistry : public QObject
{
    Q_OBJECT

public:
    explicit SharedDocumentRegistry(QObject* parent = nullptr);

    SharedDocument* acquire(const QString& fileName,
                            const QString& initialText = QString());
    SharedDocument* createUntitled(
        const QString& initialText = QString());
    SharedDocument* acquireUntitled(
        const QString& documentId,
        const QString& initialText = QString());
    SharedDocument* documentForFile(const QString& fileName) const;
    SharedDocument* documentForView(MyCodeEditor* editor) const;
    SharedDocument* documentById(const QString& documentId) const;
    QList<SharedDocument*> documents() const;
    bool renameDocument(SharedDocument* document,
                        const QString& fileName);
    bool releaseIfUnused(SharedDocument* document);

private:
    QHash<QString, SharedDocument*> documentsByIdentity;
    QHash<QString, SharedDocument*> documentsById;

    SharedDocument* createDocument(const QString& documentId,
                                   const QString& fileName,
                                   const QString& initialText);
};

#endif // SHAREDDOCUMENT_H
