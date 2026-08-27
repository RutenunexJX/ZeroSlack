#ifndef LIVEINSIGHTSCONTEXTVIEW_H
#define LIVEINSIGHTSCONTEXTVIEW_H

#include "liveinsighttypes.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QWidget>

#include <array>
#include <functional>

class LiveInsightSession;
class QCheckBox;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QStackedWidget;

class ZEROSLACK_API LiveInsightsContextView final : public QWidget
{
    Q_OBJECT

public:
    using FullViewHandler =
        std::function<void(LiveInsightKind kind)>;

    explicit LiveInsightsContextView(
        LiveInsightSession* session,
        QWidget* parent = nullptr);
    LiveInsightsContextView(
        LiveInsightSession* session,
        LiveInsightKind fixedKind,
        QWidget* parent = nullptr);
    ~LiveInsightsContextView() override;

    LiveInsightSession* session() const;
    LiveInsightKind selectedKind() const;
    void setSelectedKind(LiveInsightKind kind);

    bool followEditor() const;
    void setFollowEditor(bool follow);
    bool pinned() const;
    void setPinned(bool pinned);

    QString workspaceId() const;
    void setWorkspaceId(const QString& workspaceId);
    void setFullViewHandler(FullViewHandler handler);

    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);

    QPushButton* kindButton(LiveInsightKind kind) const;
    QLabel* kindStatusLabel(LiveInsightKind kind) const;
    QLabel* kindSummaryLabel(LiveInsightKind kind) const;
    QCheckBox* followEditorCheckBox() const;
    QPushButton* pinButton() const;
    QPushButton* openFullViewButton() const;
    bool hasFixedKind() const;

signals:
    void selectedKindChanged(LiveInsightKind kind);
    void followEditorChanged(bool follow);
    void pinnedChanged(bool pinned);
    void pinStateChangeRequested(bool pinned);
    void openFullViewRequested(LiveInsightKind kind);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct CardWidgets {
        QPushButton* button = nullptr;
        QLabel* status = nullptr;
        QLabel* summary = nullptr;
    };

    QPointer<LiveInsightSession> sessionValue;
    std::array<CardWidgets, 4> cards;
    std::array<LiveInsightSnapshot, 4> renderedSnapshots;
    std::array<bool, 4> hasRenderedSnapshot{};
    QStackedWidget* contentStack = nullptr;
    QCheckBox* followCheck = nullptr;
    QPushButton* pinToggle = nullptr;
    QPushButton* fullViewButton = nullptr;
    LiveInsightKind selected = LiveInsightKind::Kernel;
    bool fixedKindValue = false;
    QString workspaceIdValue;
    FullViewHandler fullViewHandler;

    static int indexForKind(LiveInsightKind kind);
    void initialize();
    void buildUi();
    void refreshSnapshot(LiveInsightKind kind,
                         const LiveInsightSnapshot& snapshot);
    void renderSnapshot(LiveInsightKind kind,
                        const LiveInsightSnapshot& snapshot);
    void refreshAllFromSession();
    void refreshTheme();
    void updateSessionVisibility(LiveInsightKind previousKind,
                                 LiveInsightKind nextKind);
};

#endif // LIVEINSIGHTSCONTEXTVIEW_H
