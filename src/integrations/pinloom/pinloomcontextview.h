#ifndef PINLOOMCONTEXTVIEW_H
#define PINLOOMCONTEXTVIEW_H

#include "pinloomhostclient.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class PinloomPreviewImageLabel;
struct PinloomSourceLinkResult;

class ZEROSLACK_API PinloomContextView final : public QWidget
{
    Q_OBJECT

public:
    using LinkHandler = std::function<bool(
        const QVariantMap&,
        const PinloomHostEntry&,
        QString*)>;
    using CreateLinkReply = std::function<void(const PinloomSourceLinkResult&)>;
    using CreateLinkHandler = std::function<void(const QVariantMap&, const QString&, CreateLinkReply)>;

    explicit PinloomContextView(PinloomHostClient* client,
                                QWidget* parent = nullptr);
    ~PinloomContextView() override;

    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);
    PinloomHostEntry currentEntry() const;

    QLineEdit* searchField() const;
    QListWidget* resultList() const;
    QPlainTextEdit* previewEditor() const;
    QLabel* imagePreviewLabel() const;
    QToolButton* technicalDetailsToggle() const;
    QToolButton* openButton() const;
    QToolButton* copyLinkButton() const;
    QString statusText() const;
    void setLinkHandler(LinkHandler handler);
    void setCreateLinkHandler(CreateLinkHandler handler);
    void setLinkSource(const QVariantMap& source);
    bool linkModeActive() const;
    bool boundModeActive() const;

signals:
    void currentEntryChanged(const PinloomHostEntry& entry);

protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QPointer<PinloomHostClient> clientValue;
    QLineEdit* searchEdit = nullptr;
    QListWidget* results = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* detailsLabel = nullptr;
    QToolButton* technicalDetailsButton = nullptr;
    QLabel* technicalDetailsLabel = nullptr;
    QStackedWidget* previewStack = nullptr;
    QPlainTextEdit* contentPreview = nullptr;
    PinloomPreviewImageLabel* imagePreview = nullptr;
    QLabel* statusLabel = nullptr;
    QToolButton* reloadButton = nullptr;
    QToolButton* openTargetButton = nullptr;
    QToolButton* copyUriButton = nullptr;
    QFrame* linkPanel = nullptr;
    QLabel* linkSourceLabel = nullptr;
    QLineEdit* linkTitleEdit = nullptr;
    QToolButton* attachEntryButton = nullptr;
    QToolButton* createAnchorButton = nullptr;
    PinloomHostEntry selectedEntry;
    PinloomHostIdentity preferredIdentity;
    quint64 searchGeneration = 0;
    quint64 resolveGeneration = 0;
    QVariantMap activeLinkSource;
    QVariantList activeBoundEntries;
    bool boundMode = false;
    bool resumeReadRequests = false;
    LinkHandler linkHandler;
    CreateLinkHandler createLinkHandler;
    quint64 linkSourceGeneration = 0;

    void buildUi();
    void startSearch();
    void applyBoundEntries(const QVariantList& entries,
                           const PinloomHostIdentity& preferred = {});
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
    void showTextPreview(const QString& text);
    void showImageFailure(const QString& reason);
    void openImagePreview();
    void openCurrentEntry();
    void copyCurrentUri();
    void attachCurrentEntry();
    void createSourceAnchor();
    void finishLink(const PinloomHostEntry& entry);
    void setStatus(const QString& status);
};

#endif // PINLOOMCONTEXTVIEW_H
