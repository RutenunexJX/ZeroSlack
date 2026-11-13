#pragma once
#include "zeroslackexport.h"
#include <QObject>
#include <QList>
#include <QMap>
#include <QHash>
#include <QJsonObject>
#include <QTextCursor>
#include <QVariantMap>
#include <QPointer>
#include <QSharedPointer>
#include <functional>

struct EditorAssociatedBlock {
    QString id, type, text, uri, asset, caption;
    int start = 0, end = 0, returnPosition = 0, returnAnchor = 0;
    int order = 0, width = 480, height = 160;
    bool missing = false, collapsed = false;
    bool autoHeight = false, detached = false;
    QVariantMap appearance;
    quint64 revision = 1;
};

class FormatterTriviaPositionMapper;

// The QTextDocument owns one association model, shared by every native view.
// Text remains in Qt's undo stack. Association commands have explicit text
// boundaries; they never introduce synthetic source characters.
class ZEROSLACK_API DocumentBlockModel final : public QObject {
    Q_OBJECT
public:
    static DocumentBlockModel* get(QTextDocument* document);
    const QList<EditorAssociatedBlock>& items() const { return blocks; }
    const EditorAssociatedBlock* find(const QString& id) const;
    QString insert(const QString& type, const QTextCursor& source, const QString& text = {});
    QString insert(EditorAssociatedBlock block, const QTextCursor& source, std::function<bool(const QString&)> prepare = {});
    QStringList insertMany(QList<EditorAssociatedBlock> values, const QTextCursor& source, const QString& beforeId = {});
    bool update(EditorAssociatedBlock value);
    bool remove(const QString& id);
    bool reorder(const QString& id, int direction);
    bool reattach(const QString& id, const QTextCursor& source);
    bool move(const QString& id, const QTextCursor& source, const QString& beforeId = {});
    QString transferTo(DocumentBlockModel* target, const QString& id, const QTextCursor& destination,
        bool copy, const QString& beforeId = {}, std::function<bool(QString*)> replayGuard = {});
    void beginTextSession(const QString& id);
    void editText(const QString& id, const QString& text);
    void endTextSession();
    bool flushContents(QString* error = nullptr);
    bool undoAssociation(bool redo);
    void beginTextReplay();
    void endTextReplay();
    void beginFormatting();
    void endFormatting(const FormatterTriviaPositionMapper& mapper, bool succeeded);
    bool canUndo() const;
    bool canRedo() const;
    bool hasAssociationHistory() const { return !blocks.isEmpty() || !commands.isEmpty(); }
    bool takeTextBarrier() { if (replaying) return false; const bool value = textBarrier; textBarrier = false; return value; }
    void requireTextBarrier() { textBarrier = true; }
    bool dirty() const { return stamp != savedStamp; }
    quint64 revision() const { return generation; }
    void markSaved();
    void markUnsaved();
    void setDocumentId(const QString& value) { identity = value; }
    void discard();
    void resetHistory(const QString& reason);
    QJsonObject serialize() const;
    bool restore(const QJsonObject& value, QString* error);
    QString documentId() const { return identity; }
    QString error() const { return failure; }
    void setError(const QString& message);
    void cloneIdentity();
signals:
    void contentsFlushRequested(QString* error);
    void changed();
    void dirtyChanged(bool dirty);
    void historyChanged();
    void errorChanged(const QString& message);
private:
    explicit DocumentBlockModel(QTextDocument* document);
    QTextDocument* document;
    QList<EditorAssociatedBlock> blocks, savedBlocks;
    QString identity, failure;
    quint64 generation = 0, nextStamp = 0, stamp = 0, savedStamp = 0;
    struct PairedCommand {
        QPointer<DocumentBlockModel> source, target;
        std::function<bool(QString*)> guard;
    };
    struct Command {
        QList<EditorAssociatedBlock> before, after;
        int textStep;
        quint64 beforeStamp, afterStamp;
        QSharedPointer<PairedCommand> pair;
    };
    QList<Command> commands;
    int applied = 0;
    struct AnchorState { QHash<QString, EditorAssociatedBlock> entries; quint64 stamp = 0; };
    QMap<int, AnchorState> textStates;
    bool replaying = false;
    bool formatting = false;
    bool textBarrier = false;
    QString editing;
    int editingCommand = -1;
    void mutate(const QList<EditorAssociatedBlock>& next);
    void appendCommand(const QList<EditorAssociatedBlock>& next, const QSharedPointer<PairedCommand>& pair = {});
    bool replayPair(const QSharedPointer<PairedCommand>& pair, bool redo);
    void captureAnchors();
    void sourceChanged(int position, int removed, int added);
    void publish(bool previouslyDirty, bool advance = true);
};
