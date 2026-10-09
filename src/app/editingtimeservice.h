#pragma once

#include "editingtimestore.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>

class MyCodeEditor;
class TabManager;
class QTextDocument;

class ZEROSLACK_API EditingTimeService final : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<qint64()>;
    explicit EditingTimeService(const QString& storagePath, QObject* parent = nullptr,
                                Clock clock = {});
    ~EditingTimeService() override;
    static QString defaultStoragePath();
    static qint64 monotonicNow();
    void attachEditor(MyCodeEditor* editor);
    void attachTabManager(TabManager* manager);
    qint64 totalNanoseconds() const;
    bool isActive() const { return !heldKeys.isEmpty(); }
    bool hasPendingTime() const { return isActive() || !pending.isEmpty(); }
    QString persistenceError() const { return lastError; }
    bool synchronize();
    bool reset();
    void stopAll();

signals:
    void changed();
    void persistenceFailed(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Input {
        qint64 start = 0;
        qint64 stoppedAt = -1;
        int key = 0;
        bool repeat = false;
        quint64 revision = 0;
        QPointer<QTextDocument> document;
        bool changedBeforeStop = false;
        QString generation;
    };
    void beginInput(MyCodeEditor* editor, int key, bool repeat);
    void finishInput(MyCodeEditor* editor, bool changed);
    void stopEditor(MyCodeEditor* editor);
    void stopInput(MyCodeEditor* editor, qint64 now);
    void releaseKey(int key);
    void checkpoint(qint64 now);
    void addInterval(qint64 start, qint64 end);
    bool acceptResult(const EditingTimeSaveResult& result);

    EditingTimeStore store;
    Clock clock;
    QTimer saveTimer;
    EditingTimeSnapshot saved;
    QList<EditingTimeInterval> pending;
    QSet<MyCodeEditor*> editors;
    QHash<MyCodeEditor*, QSet<int>> heldKeys;
    QHash<MyCodeEditor*, Input> inputs;
    qint64 activeSince = 0;
    QString lastError;
    QPointer<MyCodeEditor> shortcutEditor;
    Input shortcutInput;
    quint64 shortcutGeneration = 0;
};
