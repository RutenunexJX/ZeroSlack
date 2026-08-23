#ifndef PINLOOMCONTEXTVIEW_H
#define PINLOOMCONTEXTVIEW_H

#include "pinloomhostclient.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QToolButton;

class ZEROSLACK_API PinloomContextView final : public QWidget
{
    Q_OBJECT

public:
    explicit PinloomContextView(PinloomHostClient* client,
                                QWidget* parent = nullptr);

    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);
    PinloomHostEntry currentEntry() const;

    QLineEdit* searchField() const;
    QListWidget* resultList() const;
    QPlainTextEdit* previewEditor() const;
    QToolButton* openButton() const;
    QToolButton* copyLinkButton() const;
    QString statusText() const;

signals:
    void currentEntryChanged(const PinloomHostEntry& entry);

private:
    QPointer<PinloomHostClient> clientValue;
    QLineEdit* searchEdit = nullptr;
    QListWidget* results = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* detailsLabel = nullptr;
    QPlainTextEdit* contentPreview = nullptr;
    QLabel* statusLabel = nullptr;
    QToolButton* reloadButton = nullptr;
    QToolButton* openTargetButton = nullptr;
    QToolButton* copyUriButton = nullptr;
    PinloomHostEntry selectedEntry;
    PinloomHostIdentity preferredIdentity;
    quint64 searchGeneration = 0;
    quint64 resolveGeneration = 0;

    void buildUi();
    void startSearch();
    void applySearchResults(const QList<PinloomHostEntry>& entries,
                            const QString& error,
                            quint64 generation,
                            const PinloomHostIdentity& preferred = {});
    void selectEntry(const PinloomHostEntry& entry,
                     bool announceChange = true);
    void resolveEntry(const PinloomHostIdentity& identity,
                      bool announceChange = true);
    void showDocument(const PinloomHostDocument& document,
                      const QString& error,
                      quint64 generation,
                      bool announceChange);
    void openCurrentEntry();
    void copyCurrentUri();
    void setStatus(const QString& status);
};

#endif // PINLOOMCONTEXTVIEW_H
