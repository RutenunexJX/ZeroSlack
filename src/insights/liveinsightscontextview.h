#ifndef LIVEINSIGHTSCONTEXTVIEW_H
#define LIVEINSIGHTSCONTEXTVIEW_H

#include "liveinsighttoolpage.h"
#include "liveinsighttypes.h"
#include "zeroslackexport.h"

#include <QPointer>
#include <QString>
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
class QVBoxLayout;

class ZEROSLACK_API LiveInsightsContextView final : public QWidget
{
    Q_OBJECT

public:
    using FullViewHandler =
        std::function<void(LiveInsightKind kind)>;
    // Supplies the editor context a real insight surface renders from. A
    // fixed-kind view only embeds that surface once a source is installed;
    // without one it keeps the compact summary card.
    using ToolContextSource =
        std::function<LiveInsightToolContext()>;
    using WaveformLibraryPathSource =
        std::function<QString()>;
    // One target a section can render. Candidates come from this section's own
    // history, from the editor context it is not currently following, and from
    // the editor-side picker.
    struct TargetCandidate {
        QString label;
        QString moduleName;
        QString signalName;
        QString signalAccessPath;
        // Wave renders a scope, not a symbol: without these a picked scope
        // would be accepted and then quietly ignored by the surface.
        QString scopeLabel;
        int scopeStartPosition = -1;
        int scopeEndPosition = -1;
        int scopeStartLineZeroBased = 0;
        bool operator==(const TargetCandidate& other) const
        {
            return moduleName == other.moduleName
                && signalName == other.signalName
                && signalAccessPath == other.signalAccessPath
                && scopeLabel == other.scopeLabel
                && scopeStartPosition == other.scopeStartPosition
                && scopeEndPosition == other.scopeEndPosition;
        }
    };
    // Asks the host to run the editor-side picker for this kind. The section
    // that asked is the one the result lands in, so the reply is a callback
    // rather than a broadcast. Returns false when no editor can be picked in.
    using TargetPickRequest = std::function<bool(
        LiveInsightKind kind,
        std::function<void(const TargetCandidate&)> picked)>;

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
    void setToolContextSource(ToolContextSource source);
    void setWaveformLibraryPathSource(
        WaveformLibraryPathSource source);
    void setTargetPickRequest(TargetPickRequest request);
    // Invoked by the section header's scope chip through the generic property
    // channel; also reachable from the empty state.
    Q_INVOKABLE bool requestScopePick();
    LiveInsightToolPage* surfaceForTest() const;
    QWidget* emptyStateForTest() const;
    QList<TargetCandidate> candidateTargets() const;
    bool applyTargetCandidate(const TargetCandidate& candidate);

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
    std::array<CardWidgets, 5> cards;
    std::array<LiveInsightSnapshot, 5> renderedSnapshots;
    std::array<bool, 5> hasRenderedSnapshot{};
    QStackedWidget* contentStack = nullptr;
    QCheckBox* followCheck = nullptr;
    QPushButton* pinToggle = nullptr;
    QPushButton* fullViewButton = nullptr;
    LiveInsightKind selected = LiveInsightKind::Kernel;
    bool fixedKindValue = false;
    QString workspaceIdValue;
    FullViewHandler fullViewHandler;
    ToolContextSource toolContextSource;
    WaveformLibraryPathSource waveformLibraryPathSource;
    TargetPickRequest targetPickRequest;
    QPointer<LiveInsightToolPage> surfaceValue;
    QPointer<QWidget> emptyStateValue;
    QPointer<QVBoxLayout> emptyStateList;
    QList<TargetCandidate> recentTargets;
    TargetCandidate targetOverride;
    bool targetOverrideActive = false;
    bool surfaceRenderPending = false;

    static int indexForKind(LiveInsightKind kind);
    void initialize();
    void buildUi();
    void refreshSnapshot(LiveInsightKind kind,
                         const LiveInsightSnapshot& snapshot);
    void renderSnapshot(LiveInsightKind kind,
                        const LiveInsightSnapshot& snapshot);
    void refreshAllFromSession();
    void publishSectionStatus(const LiveInsightSnapshot& snapshot);
    void publishSectionScope();
    bool surfaceEnabled() const;
    void ensureSurface();
    void renderSurface();
    LiveInsightToolContext effectiveContext() const;
    bool contextHasTarget(const LiveInsightToolContext& context) const;
    void rememberTarget(const LiveInsightToolContext& context);
    void refreshEmptyState(const LiveInsightToolContext& context);
    void refreshTheme();
    void updateSessionVisibility(LiveInsightKind previousKind,
                                 LiveInsightKind nextKind);
};

#endif // LIVEINSIGHTSCONTEXTVIEW_H
