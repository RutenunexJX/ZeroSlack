#ifndef SHAREDDOCUMENT_H
#define SHAREDDOCUMENT_H

#include "zeroslackexport.h"
#include "documentbuffer.h"

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

class ZEROSLACK_API SharedDocument : public DocumentBuffer
{
    Q_OBJECT

public:
    explicit SharedDocument(const QString& documentId,
                            const QString& fileName,
                            const QString& initialText,
                            QObject* parent = nullptr);
    ~SharedDocument() override;

    bool reloadCleanText(const QString& text);
    // Applies a user-confirmed external generation as one undoable
    // replacement. Unlike automatic clean reload, this entry point may
    // resolve a dirty conflict and deliberately keeps the rejected local
    // text reachable through the shared undo stack.
    bool acceptExternalText(const QString& text);
    bool restoreUnsavedText(
        const QString& text,
        std::uint64_t recoveredRevision);
    void setReadOnly(bool readOnly) override;

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
    void viewAttached(const QString& viewId);
    void viewDetached(const QString& viewId);

private:
    friend class SharedDocumentRegistry;

    struct ViewBinding {
        QPointer<MyCodeEditor> editor;
        SharedDocumentViewState state;
        QList<QMetaObject::Connection> connections;
    };

    QHash<MyCodeEditor*, ViewBinding> viewBindings;

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
