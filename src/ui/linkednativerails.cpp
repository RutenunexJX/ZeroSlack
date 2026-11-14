#include "linkednativerails.h"
#include "linkedraillayout.h"
#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include <QEvent>
#include <QCryptographicHash>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QKeyEvent>
#include <QPointer>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaScrollBar.h"
#endif

namespace {
class ReferenceGroup final : public QFrame {
public:
    using QFrame::QFrame;
    bool highlighted = false;
    void setHighlighted(bool value) { if (highlighted != value) { highlighted = value;update(); } }
protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        if (!highlighted) return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        auto tint = palette().color(QPalette::Highlight);tint.setAlpha(22);
        painter.setBrush(tint);painter.setPen(palette().color(QPalette::Highlight));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),4,4);
    }
};
class ReferenceCard final : public QFrame {
public:
    using QFrame::QFrame;
    int outlines = 0;
    bool highlighted = false;
    void setHighlighted(bool value) { if (highlighted != value) { highlighted = value;update(); } }
    void setOutlines(int count) {
        if (outlines == count) return;
        outlines = count;
        if (layout()) layout()->setContentsMargins(8, 8, 8 + 4 * outlines, 8 + 4 * outlines);
        updateGeometry(); update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(palette().color(highlighted ? QPalette::Highlight : QPalette::Mid));
        painter.setBrush(palette().brush(QPalette::Base));
        const QRectF front = QRectF(rect()).adjusted(.5, .5, -.5 - 4 * outlines, -.5 - 4 * outlines);
        for (int layer = outlines; layer >= 0; --layer)
            painter.drawRoundedRect(front.translated(4 * layer, 4 * layer), 4, 4);
    }
};
}

struct LinkedNativeRails::Private {
    struct Card {
        Reference reference;
        ReferenceCard* frame = nullptr;
        QLabel* title = nullptr;
        QLabel* location = nullptr;
        QStackedWidget* body = nullptr;
        QTextBrowser* text = nullptr;
        PinloomPreviewImageLabel* image = nullptr;
        QString state;
        Preview preview;
        QString displayedTitle;
        bool displayed = false;
    };
    struct GroupView {
        Group value;
        ReferenceGroup* frame = nullptr;
        int side = 0;
        QLabel* title = nullptr;
        QPushButton* toggle = nullptr;
        bool expanded = false;
        qint64 preferredHeight = 0;
    };
    LinkedNativeRails* q;
    QScrollArea* rails[2]{};
    QWidget* hosts[2]{};
    QPushButton* followButtons[2]{};
    QWidget* contents[2]{};
    struct Track {
        bool follow = true;
        QString reason = QStringLiteral("session"), selected, target;
        QMap<QString,qint64> frozen;
        QStringList structure;
        QSize viewport;
        int structuralCorrection = 0;
        bool clamped = false;
    } tracks[2];
    using Readings = std::array<QMap<QString,int>,2>;
    LinkedRailLayout::Result placements[2];
    QMap<QString,qint64> appliedIdeals[2];
    QList<GroupView> groups;
    QList<Group> logicalGroups;
    QMap<QString,bool> expansion;
    QMap<QString, Card> cards;
    QMap<QString, Preview> previews;
    QSet<QString> activeAnchors;
    QStringList placedOrder;
    QStringList visible;
    QTimer visibilityTimer;
    QList<int> requestedSizes;
    double requestedRatio = .5;
    bool initialSizesPending = false;
    bool rebuilding = false;
    bool visibilityInvalidated = false;
    qint64 appliedContentRevision = -1;
    qint64 pendingContentRevision = -1;
    quint64 reconciliationSerial = 0;
    quint64 layoutSerial = 0;
    quint64 presentationSerial = 0;
    bool layingOut = false;
    bool layoutPending = false;
    bool alignmentPending = false;

    explicit Private(LinkedNativeRails* owner) : q(owner) {
        visibilityTimer.setSingleShot(true);
        QObject::connect(&visibilityTimer, &QTimer::timeout, q, [this] { reportVisible(); });
    }
    void applyHighlights() {
        for (auto& group : groups) {
            bool active = false;
            for (const auto& member : group.value.members) active |= activeAnchors.contains(member);
            group.frame->setHighlighted(active);
            for (const auto& reference : group.value.references) {
                const auto card = cards.find(reference.id);
                if (card != cards.end()) card->frame->setHighlighted(active);
            }
        }
    }
    void scheduleVisible() { if (!rebuilding) visibilityTimer.start(0); }
    void reportVisible() {
        QStringList next;
        for (const auto& group : groups) {
            auto* viewport = rails[group.side]->viewport();
            if (!viewport->isVisible()) continue;
            for (const auto& reference : group.value.references) {
                const auto card = cards.constFind(reference.id);
                if (card == cards.cend() || !card->frame->isVisible()) continue;
                const QRect area(card->frame->mapTo(viewport, QPoint()), card->frame->size());
                if (viewport->rect().intersects(area)) next.append(reference.id);
            }
        }
        next.sort();
        if (next == visible && !visibilityInvalidated) return;
        visibilityInvalidated = false;
        visible = next;
        emit q->visibleReferencesChanged(visible);
    }
    void applyInitialSizes() {
        if (!initialSizesPending || q->width() <= 0) return;
        initialSizesPending = false;
        const int available = qMax(0, q->width() - 2 * q->handleWidth());
        QList<int> values;
        const int sum = requestedSizes.size() == 3
            ? requestedSizes[0] + requestedSizes[1] + requestedSizes[2] : 0;
        if (sum > 0 && std::all_of(requestedSizes.cbegin(), requestedSizes.cend(), [](int v) { return v >= 0; })) {
            const int left = qBound(0, qRound(double(available) * requestedSizes[0] / sum), available);
            const int center = qBound(0, qRound(double(available) * requestedSizes[1] / sum), available - left);
            values = {left, center, available - left - center};
        } else {
            const int center = qBound(0, qRound(available * requestedRatio), available);
            const int left = (available - center) / 2;
            values = {left, center, available - center - left};
        }
        q->setSizes(values);
        scheduleVisible();
    }
    void orderGroups() {
        std::stable_sort(groups.begin(), groups.end(), [](const GroupView& a, const GroupView& b) {
            if(a.value.positioned!=b.value.positioned)return a.value.positioned;
            return a.value.contentY != b.value.contentY ? a.value.contentY < b.value.contentY : a.value.anchor < b.value.anchor;
        });
        QStringList after;
        for (const auto& group : groups) after.append(group.value.anchor);
        if (placedOrder == after) return;
        placedOrder = after;
        for (int i = 0; i < groups.size(); ++i) {
            auto& group = groups[i]; group.side = i % 2;
            if (group.frame->parentWidget() != contents[group.side]) group.frame->setParent(contents[group.side]);
            group.frame->setProperty("rail", group.side == 0 ? "left" : "right");
            group.frame->show();
        }
    }
    QList<Group> presentationGroups() const {
        QMap<int,QList<Group>> folded;
        for(const auto& group:logicalGroups)if(group.foldHead>=0)folded[group.foldHead].append(group);
        QList<Group> result;
        for(auto group:logicalGroups) {
            if(group.foldHead>=0)continue;
            if(folded.contains(group.sourceLine))folded[group.sourceLine].append(group);
            else {group.members={group.anchor};result.append(group);}
        }
        for(auto it=folded.cbegin();it!=folded.cend();++it) {
            Group group=it.value().first();group.members.clear();group.references.clear();
            QMap<QString,Reference> references;
            for(const auto& member:it.value()) {
                group.members.append(member.anchor);
                for(const auto& ref:member.references)references.insert(ref.id,ref);
            }
            group.members.sort();group.references=references.values();
            group.anchor=QStringLiteral("fold-")+QString::fromLatin1(QCryptographicHash::hash(
                QJsonDocument(QJsonArray::fromStringList(group.members)).toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256).toHex());
            result.append(group);
        }
        return result;
    }
    Readings readingPositions() const {
        Readings result;
        for (const auto& group : groups) for (const auto& reference : group.value.references) {
            const auto card = cards.constFind(reference.id);
            if (card != cards.cend()) result[group.side].insert(reference.id,
                card->frame->mapTo(rails[group.side]->viewport(),QPoint()).y());
        }
        return result;
    }
    void detach(int side, const QString& reason) {
        auto& track = tracks[side];
        if (track.follow) {
            track.frozen = appliedIdeals[side];
        }
        track.follow = false; track.reason = reason; followButtons[side]->show();
    }
    void resume(int side, const QString& anchor, const QString& reason, bool selection) {
#ifdef ZEROSLACK_ENABLE_ELA
        if(auto* bar=qobject_cast<ElaScrollBar*>(rails[side]->verticalScrollBar()))bar->stopSmoothWheel();
#endif
        auto& track = tracks[side];
        track.follow = true; track.reason = reason; track.frozen.clear();
        if (selection) track.selected = anchor;
        if (!anchor.isEmpty()) track.target = anchor;
        followButtons[side]->hide(); layoutGroups(true,side);
    }
    void layoutGroups(bool alignCode = false, int forcedSide = -1, const Readings* savedReadings = nullptr) {
        alignmentPending |= alignCode;
        if (layingOut || rebuilding) { layoutPending = true; return; }
        if (!rails[0] || !rails[1] || !contents[0] || !contents[1]) return;
        alignCode = alignmentPending; alignmentPending = false; layoutPending = false;
        const Readings before = savedReadings ? *savedReadings : readingPositions();
        layingOut = true;
        for (int side = 0; side < 2; ++side) {
            auto* area = rails[side]; auto* bar = area->verticalScrollBar();
            auto& track = tracks[side];
            const int oldScroll = bar->value();
            const int width = qMax(0, area->viewport()->width() - 12);
            QList<LinkedRailLayout::Input> input;
            QList<GroupView*> ordered;
            QStringList structure;
            QSet<QString> remaining;
            for (auto& group : groups) if (group.side == side) {
                group.frame->resize(width, group.frame->height());
                group.frame->layout()->activate();
                group.preferredHeight = qMax(0, group.frame->sizeHint().height());
                remaining.insert(group.value.anchor);
                if (!track.follow && !track.frozen.contains(group.value.anchor)) track.frozen.insert(group.value.anchor,group.value.contentY);
                const qint64 ideal = track.follow ? group.value.contentY : track.frozen.value(group.value.anchor);
                const qint64 located = group.value.positioned ? ideal
                    : input.isEmpty()?6:input.last().idealY+input.last().preferredHeight+8;
                input.append({group.value.anchor, located, int(group.value.references.size()),
                              group.preferredHeight, group.expanded});
                ordered.append(&group);
                structure << group.value.anchor << QString::number(group.preferredHeight) << QString::number(group.expanded)
                          << QString::number(group.value.references.size());
                for (const auto& reference : group.value.references) structure << reference.id;
            }
            for (auto it = track.frozen.begin(); it != track.frozen.end();) {
                if (remaining.contains(it.key())) ++it; else it = track.frozen.erase(it);
            }
            const bool structural = structure != track.structure || track.viewport != area->viewport()->size();
            auto result = LinkedRailLayout::arrange(input, area->viewport()->height(), oldScroll, 8, 6);
            if (!result.valid) continue;
            contents[side]->resize(area->viewport()->width(),
                int(qMin<qint64>(QWIDGETSIZE_MAX, qMax<qint64>(area->viewport()->height(), result.totalHeight + 6))));
            for (qsizetype i = 0; i < result.groups.size(); ++i) {
                const auto& placed = result.groups[i];
                ordered[i]->frame->setGeometry(6, int(qMin<qint64>(QWIDGETSIZE_MAX, placed.contentY)), width,
                                              int(qMin<qint64>(QWIDGETSIZE_MAX, placed.height)));
            }
            int target = oldScroll;
            if (track.follow && alignCode && (forcedSide < 0 || forcedSide == side) && area->viewport()->height() > 0) {
                const GroupView* nearest = nullptr;
                for (const auto& group : groups) if (group.side == side && group.value.positioned
                    && (!nearest || qAbs(group.value.viewportY) < qAbs(nearest->value.viewportY))) nearest = &group;
                for (const auto* group : ordered) {
                    if(!group->value.positioned)continue;
                    if (forcedSide == side && group->value.anchor == track.target) { nearest = group; break; }
                    if (group->value.anchor == track.selected && group->value.viewportY >= 0
                        && group->value.viewportY < area->viewport()->height()) { nearest = group; break; }
                }
                if (nearest) {
                    const int y = qBound(0,nearest->value.viewportY,qMax(0,area->viewport()->height()-qMin(nearest->frame->height(),area->viewport()->height())));
                    target = nearest->frame->y() - y; track.target = nearest->value.anchor;
                }
            } else if (!track.follow && structural) {
                QString baseline; int distance = INT_MAX;
                for (const auto* group : ordered) for (const auto& reference : group->value.references) {
                    if (!cards.contains(reference.id) || !before[side].contains(reference.id)) continue;
                    const int offset = before[side].value(reference.id);
                    if (qAbs(offset) < distance) { baseline = reference.id; distance = qAbs(offset); }
                }
                if (!baseline.isEmpty()) target = bar->value()
                    + cards[baseline].frame->mapTo(area->viewport(),QPoint()).y() - before[side].value(baseline);
            }
            bar->setValue(target);
            if (structural) {
                track.structuralCorrection = bar->value() - oldScroll; track.clamped = bar->value() != target;
            }
            track.structure = structure; track.viewport = area->viewport()->size();
            placements[side] = LinkedRailLayout::arrange(input, area->viewport()->height(), bar->value(), 8, 6);
            appliedIdeals[side].clear();
            for (const auto& item : input) appliedIdeals[side].insert(item.id,item.idealY);
        }
        appliedContentRevision = pendingContentRevision;
        ++layoutSerial; layingOut = false; scheduleVisible();
        ++presentationSerial;
        emit q->layoutApplied(layoutSerial,appliedContentRevision);
        if (layoutPending) QTimer::singleShot(0,q,[this]{layoutGroups();});
    }
    Card makeCard(const Reference& reference, QWidget* parent) {
        Card card; card.reference = reference;
        card.frame = new ReferenceCard(parent); card.frame->setObjectName("linkedNativeCard");
        card.frame->setProperty("referenceId", reference.id); card.frame->setFrameShape(QFrame::StyledPanel);
        card.frame->setMinimumWidth(0); card.frame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto* layout = new QVBoxLayout(card.frame); layout->setContentsMargins(8, 8, 8, 8);
        card.title = new QLabel(reference.title, card.frame); card.title->setTextFormat(Qt::PlainText);
        card.title->setMinimumWidth(0); card.title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        card.title->setToolTip(reference.title); layout->addWidget(card.title);
        card.location = new QLabel(card.frame); card.location->setTextFormat(Qt::PlainText);
        card.location->setMinimumWidth(0); card.location->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        card.location->setFixedHeight(card.location->fontMetrics().height()); layout->addWidget(card.location);
        card.body = new QStackedWidget(card.frame);
        card.body->setFixedHeight(qMax(144, card.title->fontMetrics().height() * 8));
        card.body->setMinimumWidth(0); card.body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        card.text = new QTextBrowser(card.body); card.text->setObjectName("linkedNativePreviewText");
        card.text->setReadOnly(true); card.text->setOpenLinks(false); card.text->setOpenExternalLinks(false);
        card.text->setFrameShape(QFrame::NoFrame); card.text->setMinimumWidth(0);
        card.image = new PinloomPreviewImageLabel(card.body); card.image->setObjectName("linkedNativePreviewImage");
        card.body->addWidget(card.text); card.body->addWidget(card.image); layout->addWidget(card.body);
        auto* actions = new QHBoxLayout;
        auto* locate = UiControls::pushButton(QStringLiteral("Go to code"), card.frame);
        locate->setObjectName("linkedNativeLocate"); actions->addWidget(locate);
        auto* original = UiControls::pushButton(QStringLiteral("Open original"), card.frame);
        original->setObjectName("linkedNativeOpen"); actions->addWidget(original); layout->addLayout(actions);
        QObject::connect(locate, &QPushButton::clicked, q, [this, id = reference.id] { emit q->locateCodeRequested(id); });
        QObject::connect(original, &QPushButton::clicked, q, [this, id = reference.id] { emit q->openOriginalRequested(id); });
        if(reference.local){
            card.title->hide();card.location->hide();locate->hide();original->hide();
            card.body->setFixedHeight(qBound(80,reference.height,900));layout->setContentsMargins(0,0,0,0);
        }
        return card;
    }
    void display(Card& card, const Preview& preview) {
        if(card.reference.local)return;
        const QString title = preview.title.isEmpty() ? card.reference.title : preview.title;
        if (card.displayed && card.displayedTitle == title
            && card.preview.text == preview.text && card.preview.location == preview.location
            && card.preview.error == preview.error && card.preview.complete == preview.complete
            && card.preview.image.cacheKey() == preview.image.cacheKey()) return;
        card.preview = preview; card.displayedTitle = title; card.displayed = true;
        card.title->setText(title); card.title->setToolTip(title);
        card.location->setText(preview.location); card.location->setToolTip(preview.location);
        if (!preview.complete) card.state = "loading";
        else if (!preview.error.isEmpty()) card.state = "error";
        else if (!preview.image.isNull()) card.state = "image";
        else card.state = preview.text.isEmpty() ? "empty" : "text";
        if (card.state == "image") {
            card.image->setSourcePixmap(QPixmap::fromImage(preview.image), title);
            card.text->clear(); card.body->setCurrentWidget(card.image);
        } else {
            card.image->clearSourcePixmap(); card.body->setCurrentWidget(card.text);
            card.text->setPlainText(card.state == "loading" ? QStringLiteral("Loading reference…")
                : card.state == "error" ? preview.error
                : card.state == "empty" ? QStringLiteral("No content") : preview.text.left(12000));
        }
        card.frame->setProperty("previewState", card.state);
    }
};

LinkedNativeRails::LinkedNativeRails(QWidget* editorPage, QWidget* parent)
    : QSplitter(Qt::Horizontal, parent), d(std::make_unique<Private>(this)) {
    setObjectName("linkedNativeRails"); setChildrenCollapsible(false);
    for (int side = 0; side < 2; ++side) {
        auto* host = new QWidget(this); d->hosts[side] = host;
        host->setMinimumWidth(0); host->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
        auto* column = new QVBoxLayout(host); column->setContentsMargins(0,0,0,0); column->setSpacing(0);
        column->setSizeConstraint(QLayout::SetNoConstraint);
        auto* header = new QWidget(host); auto* row = new QHBoxLayout(header); row->setContentsMargins(6,3,6,3);
        auto* follow = UiControls::pushButton(QStringLiteral("Follow code"),header); d->followButtons[side] = follow;
        follow->setObjectName(side == 0 ? "linkedNativeFollowLeft" : "linkedNativeFollowRight");
        row->addWidget(follow); header->setFixedHeight(follow->sizeHint().height()+6); follow->hide(); column->addWidget(header);
        connect(follow,&QPushButton::clicked,this,[this,side]{d->resume(side,d->tracks[side].target,QStringLiteral("follow-button"),false);});
        auto* area = UiControls::scrollArea(host); d->rails[side] = area; column->addWidget(area,1);
        area->setObjectName(side == 0 ? "linkedNativeLeftRail" : "linkedNativeRightRail");
        area->setWidgetResizable(false); area->setFrameShape(QFrame::NoFrame); area->setMinimumWidth(0);
        area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body = new QWidget(area); d->contents[side] = body;
        body->setObjectName(side == 0 ? "linkedNativeLeftGroups" : "linkedNativeRightGroups");
        body->setMinimumWidth(0); body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        area->setWidget(body); area->viewport()->installEventFilter(this);
        area->verticalScrollBar()->installEventFilter(this);
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            d->scheduleVisible();
            if (!d->layingOut && !d->rebuilding) { ++d->presentationSerial;emit presentationChanged(); }
        });
    }
    addWidget(d->hosts[0]); addWidget(editorPage); addWidget(d->hosts[1]);
    setStretchFactor(0, 0); setStretchFactor(1, 1); setStretchFactor(2, 0);
    connect(this, &QSplitter::splitterMoved, this, [this] {
        const auto values = sizes(); const int sum = values[0] + values[1] + values[2];
        if (sum > 0) emit codeRatioChanged(double(values[1]) / sum);
        d->layoutGroups();
    });
}
LinkedNativeRails::~LinkedNativeRails() { resetSession(); }

void LinkedNativeRails::resetSession() {
    d->rebuilding = true; d->visibilityTimer.stop();
    for(auto it=d->cards.cbegin();it!=d->cards.cend();++it)if(it->reference.local)emit releasingBlockWidget(it.key());
    for (const auto& group : d->groups) delete group.frame;
    d->groups.clear(); d->cards.clear(); d->previews.clear(); d->placedOrder.clear(); d->visible.clear();
    d->logicalGroups.clear(); d->expansion.clear();
    d->activeAnchors.clear();
    d->appliedContentRevision = -1; d->reconciliationSerial = 0; d->layoutSerial = 0; d->visibilityInvalidated = true;
    d->pendingContentRevision = -1;
    d->alignmentPending = false; d->layoutPending = false;
    d->placements[0] = {}; d->placements[1] = {};
    d->appliedIdeals[0].clear(); d->appliedIdeals[1].clear();
    for (int side=0;side<2;++side) {
        d->tracks[side] = {}; d->followButtons[side]->hide(); d->rails[side]->verticalScrollBar()->setValue(0);
    }
    d->rebuilding = false;
    ++d->presentationSerial;emit presentationChanged();
}
void LinkedNativeRails::setGroups(const QList<Group>& logicalGroups, qint64 contentRevision) {
    d->rebuilding = true; d->visibilityTimer.stop();
    const auto readings = d->readingPositions();
    d->logicalGroups=logicalGroups;
    std::stable_sort(d->logicalGroups.begin(),d->logicalGroups.end(),[](const Group& a,const Group& b){
        return a.contentY!=b.contentY?a.contentY<b.contentY:a.anchor<b.anchor;
    });
    QSet<QString> logicalIds;
    for(const auto& group:logicalGroups)logicalIds.insert(group.anchor);
    for(auto it=d->expansion.begin();it!=d->expansion.end();)
        if(logicalIds.contains(it.key()))++it;else it=d->expansion.erase(it);
    const auto groups=d->presentationGroups();
    const bool initial = d->groups.isEmpty();
    bool geometryChanged = groups.size() != d->groups.size();
    QMap<QString,Position> previousPositions;
    for(const auto& group:d->groups)
        previousPositions.insert(group.value.anchor,{group.value.y,group.value.contentY,group.value.viewportY});
    for(const auto& group:groups) {
        const auto previous=previousPositions.constFind(group.anchor);
        geometryChanged |= previous==previousPositions.cend()
            || previous->contentY!=group.contentY || previous->viewportY!=group.viewportY;
    }
    QMap<QString, Private::GroupView> old;
    for (const auto& group : d->groups) old.insert(group.value.anchor, group);
    QList<Private::GroupView> next;
    QSet<QString> seen, retainedCards;
    for (const auto& value : groups) {
        if (value.references.isEmpty() || seen.contains(value.anchor)) continue;
        seen.insert(value.anchor);
        Private::GroupView group = old.take(value.anchor);
        if (!group.frame) {
            group.frame = new ReferenceGroup(d->contents[0]); group.frame->setObjectName("linkedNativeGroup");
            group.frame->setProperty("anchorId", value.anchor); group.frame->setMinimumWidth(0);
            group.frame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            auto* layout = new QVBoxLayout(group.frame); layout->setContentsMargins(0, 0, 0, 0);
            layout->setSizeConstraint(QLayout::SetNoConstraint);
            auto* header = new QWidget(group.frame); auto* row = new QHBoxLayout(header); row->setContentsMargins(0, 0, 0, 0);
            group.title = new QLabel(group.frame); group.title->setTextFormat(Qt::PlainText);
            group.title->setMinimumWidth(0); group.title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            row->addWidget(group.title, 1);
            group.toggle = UiControls::pushButton(QStringLiteral("Expand references"), header);
            group.toggle->setObjectName("linkedNativeExpand"); group.toggle->setProperty("anchorId", value.anchor);
            row->addWidget(group.toggle); layout->addWidget(header);
            QObject::connect(group.toggle, &QPushButton::clicked, this, [this, id = value.anchor] {
                const auto found = std::find_if(d->groups.cbegin(), d->groups.cend(), [&](const auto& current) { return current.value.anchor == id; });
                if (found != d->groups.cend()) setGroupExpanded(id, !found->expanded);
            });
        }
        group.value = value;
        group.expanded=value.foldHead<0&&d->expansion.value(value.anchor);
        std::sort(group.value.references.begin(), group.value.references.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        group.title->setText((value.foldHead>=0?QStringLiteral("%1 references (folded)")
            :!value.positioned?QStringLiteral("%1 references (unlocated)"):QStringLiteral("%1 references")).arg(group.value.references.size()));
        group.title->setToolTip(group.title->text());
        group.toggle->setVisible(value.foldHead>=0||group.value.references.size()>1);
        group.toggle->setText(group.expanded ? QStringLiteral("Collapse references") : QStringLiteral("Expand references"));
        auto* layout = qobject_cast<QVBoxLayout*>(group.frame->layout());
        int index = 1;
        for (const auto& reference : group.value.references) {
            if (!group.expanded && index > 1) break;
            if (retainedCards.contains(reference.id)) continue;
            retainedCards.insert(reference.id);
            auto card = d->cards.find(reference.id);
            if (card == d->cards.end()) card = d->cards.insert(reference.id, d->makeCard(reference, group.frame));
            if (card->reference.source != reference.source) d->visibilityInvalidated = true;
            card->reference = reference;
            if (card->frame->parentWidget() != group.frame) card->frame->setParent(group.frame);
            if (layout->indexOf(card->frame) != index) layout->insertWidget(index, card->frame);
            ++index;
            d->display(*card, d->previews.value(reference.source));
            card->frame->setOutlines(group.expanded ? 0 : qMin(2, int(group.value.references.size()) - 1));
            card->frame->show();
        }
        layout->activate();
        next.append(group);
    }
    for (auto it = d->cards.begin(); it != d->cards.end();) {
        if (retainedCards.contains(it.key())) { ++it; continue; }
        if(it->reference.local)emit releasingBlockWidget(it.key());
        delete it->frame; it = d->cards.erase(it);
    }
    for (const auto& group : old) delete group.frame;
    d->groups = next;
    d->applyHighlights();
    d->orderGroups();
    d->pendingContentRevision = contentRevision;
    d->rebuilding = false;
    d->layoutGroups(initial||geometryChanged,-1,&readings);
    ++d->reconciliationSerial;
    d->rebuilding = false; d->scheduleVisible();
}
void LinkedNativeRails::updateAnchorPositions(const QMap<QString, Position>& positions) {
    bool changed = false;
    for(auto& group:d->logicalGroups) {
        const auto found=positions.constFind(group.anchor);if(found==positions.cend())continue;
        group.y=found->y;group.contentY=found->contentY;group.viewportY=found->viewportY;
    }
    for (auto& group : d->groups) {
        const auto found = positions.constFind(group.value.members.value(0,group.value.anchor)); if (found == positions.cend()) continue;
        changed |= group.value.contentY != found->contentY || group.value.viewportY != found->viewportY;
        group.value.y = found->y; group.value.contentY = found->contentY; group.value.viewportY = found->viewportY;
    }
    d->layoutGroups(changed);
}
int LinkedNativeRails::railViewportTop() const { return d->rails[0]->viewport()->mapTo(this, QPoint()).y(); }
bool LinkedNativeRails::setBlockWidget(const QString& id,QWidget* widget,int height) {
    auto found=d->cards.find(id);if(found==d->cards.end() || !found->reference.local || !widget)return false;
    const int extent=qBound(80,height,900);
    const bool changed=found->body->height()!=extent || found->body->currentWidget()!=widget;
    if(found->body->indexOf(widget)<0)found->body->addWidget(widget);
    found->body->setCurrentWidget(widget);found->body->setFixedHeight(extent);widget->show();
    if(changed)d->layoutGroups();return true;
}
void LinkedNativeRails::setRailsVisible(bool visible) {
    if (d->hosts[0]->isHidden() == !visible && d->hosts[1]->isHidden() == !visible)
        return;
    if (!visible) d->requestedSizes = sizes();
    for(auto* host:d->hosts)host->setVisible(visible);
    if(visible){d->initialSizesPending=true;d->applyInitialSizes();d->scheduleVisible();}
}
void LinkedNativeRails::setActiveAnchors(const QSet<QString>& ids) {
    if (d->activeAnchors == ids) return;
    d->activeAnchors = ids;d->applyHighlights();
}
LinkedNativeRails::ConnectorSnapshot LinkedNativeRails::connectorSnapshot(const QWidget* coordinates) const {
    ConnectorSnapshot result;
    result.layoutSerial = d->layoutSerial;result.presentationSerial = d->presentationSerial;
    result.contentRevision = d->appliedContentRevision;
    const auto rectIn = [coordinates](const QWidget* widget) {
        return QRectF(coordinates->mapFromGlobal(widget->mapToGlobal(QPoint())),widget->size());
    };
    for (int side = 0; side < 2; ++side) {
        const auto* viewport = d->rails[side]->viewport();
        result.viewports[side] = rectIn(viewport);
        const auto* bar = d->rails[side]->verticalScrollBar();
        if (bar->isVisible()) result.occlusions.append(rectIn(bar));
    }
    for (const auto& group : d->groups) {
        const QRectF frame = rectIn(group.frame);
        const QRectF title = rectIn(group.title);
        const QPointF point(group.side == 0 ? frame.right() : frame.left(), title.center().y());
        result.endpoints.append({group.value.anchor,group.value.members,group.side,
            group.value.foldHead >= 0 ? group.value.foldHead : group.value.sourceLine,
            group.value.positioned,
            !frame.isEmpty() && !title.isEmpty() && group.frame->isVisible() && group.title->isVisible()
                && result.viewports[group.side].contains(point),
            point,frame});
    }
    return result;
}
void LinkedNativeRails::setGroupExpanded(const QString& anchor, bool expanded) {
    for(const auto& group:d->groups)if(group.value.anchor==anchor) {
        if(group.value.foldHead>=0) {
            const auto members=group.value.members;
            if(expanded)emit expandFoldedRequested(members,d->appliedContentRevision);
            return;
        }
        if(group.expanded==expanded)return;
        d->expansion.insert(anchor,expanded);
        setGroups(d->logicalGroups,d->appliedContentRevision);emit groupExpansionChanged();return;
    }
}
QStringList LinkedNativeRails::expandedAnchors() const {
    QStringList result;
    for(auto it=d->expansion.cbegin();it!=d->expansion.cend();++it)if(it.value())result.append(it.key());
    return result;
}
void LinkedNativeRails::restoreExpandedAnchors(const QStringList& anchors) {
    QMap<QString,bool> restored;
    for(const auto& group:d->logicalGroups)if(anchors.contains(group.anchor))restored.insert(group.anchor,true);
    if(restored==d->expansion)return;
    d->expansion=restored;setGroups(d->logicalGroups,d->appliedContentRevision);
}
void LinkedNativeRails::followAnchor(const QString& anchor, const QString& reason) {
    for (const auto& group : d->groups) if (group.value.members.contains(anchor)) {
        d->resume(group.side,group.value.anchor,reason,true); return;
    }
}
void LinkedNativeRails::setPreview(const QString& source, const Preview& preview) {
    d->previews.insert(source, preview);
    for (auto& card : d->cards) if (card.reference.source == source) d->display(card, preview);
    d->layoutGroups();
}
void LinkedNativeRails::clearPreview(const QString& source) { setPreview(source, {}); d->previews.remove(source); }
void LinkedNativeRails::setInitialSizes(const QList<int>& saved, double codeRatio) {
    d->requestedSizes = saved; d->requestedRatio = qBound(.3, codeRatio, .7);
    d->initialSizesPending = true; d->applyInitialSizes();
}
QJsonObject LinkedNativeRails::snapshot() const {
    QJsonArray groups, cards, columns, presentation, tracks;
    QSet<QString> displayed;
    for (int size : sizes()) columns.append(size);
    for(const auto& logical:d->logicalGroups) {
        QJsonArray ids;for(const auto& reference:logical.references)ids.append(reference.id);
        int side=0;for(const auto& group:d->groups)if(group.value.members.contains(logical.anchor)){side=group.side;break;}
        groups.append(QJsonObject{{"anchor",logical.anchor},{"y",logical.y},{"side",side},{"references",ids}});
    }
    for (const auto& group : d->groups) {
        if (group.frame->highlighted) for (const auto& member : group.value.members) displayed.insert(member);
        QJsonArray ids; for (const auto& reference : group.value.references) ids.append(reference.id);
        QJsonObject memberExpansion;
        for(const auto& member:group.value.members)memberExpansion[member]=d->expansion.value(member);
        const auto* viewport = d->rails[group.side]->viewport();
        presentation.append(QJsonObject{{"id",group.value.anchor},{"references",ids},{"side",group.side},
            {"members",QJsonArray::fromStringList(group.value.members)},{"folded",group.value.foldHead>=0},
            {"memberExpansion",memberExpansion},
            {"foldHeadSourceLine",group.value.foldHead},{"foldHeadVisibleRow",group.value.visibleRow},
            {"positioned",group.value.positioned},{"highlighted",group.frame->highlighted},
            {"expanded",group.expanded},{"idealContentY",group.value.contentY},{"anchorViewportY",group.value.viewportY},
            {"effectiveIdealY",d->tracks[group.side].follow?group.value.contentY:d->tracks[group.side].frozen.value(group.value.anchor,group.value.contentY)},
            {"contentY",group.frame->y()},{"viewportY",group.frame->mapTo(viewport,QPoint()).y()},
            {"height",group.frame->height()},{"preferredHeight",group.preferredHeight},
            {"outlines",group.expanded?0:qMin(2,int(group.value.references.size())-1)}});
    }
    for (const auto& card : d->cards) cards.append(QJsonObject{{"id",card.reference.id},{"source",card.reference.source},{"state",card.state},{"highlighted",card.frame->highlighted}});
    for (int side=0;side<2;++side) {
        const auto& track=d->tracks[side];const auto* area=d->rails[side];
        tracks.append(QJsonObject{{"side",side},{"state",track.follow?"Follow":"Detached"},{"reason",track.reason},
            {"target",track.target},{"selectedTarget",track.selected},{"scroll",area->verticalScrollBar()->value()},
            {"maximum",area->verticalScrollBar()->maximum()},{"contentHeight",area->widget()->height()},
            {"viewportHeight",area->viewport()->height()},{"structuralCorrection",track.structuralCorrection},{"clamped",track.clamped}});
    }
    auto activeIds=d->activeAnchors.values();activeIds.sort();auto displayedIds=displayed.values();displayedIds.sort();
    return {{"groups",groups},{"cards",cards},{"visible",QJsonArray::fromStringList(d->visible)},{"sizes",columns},
        {"activeAnchorIds",QJsonArray::fromStringList(activeIds)},{"displayHighlightedMembers",QJsonArray::fromStringList(displayedIds)},
        {"appliedContentRevision",d->appliedContentRevision},{"reconciliationSerial",double(d->reconciliationSerial)},
        {"presentation",presentation},{"layoutSerial",double(d->layoutSerial)},
        {"tracks",tracks},
        {"coordinateUnit","logical pixels; content relative to document row 0; scroll only projects content"}};
}
void LinkedNativeRails::resizeEvent(QResizeEvent* event) {
    QSplitter::resizeEvent(event); d->applyInitialSizes(); d->layoutGroups();
}
void LinkedNativeRails::showEvent(QShowEvent* event) {
    QSplitter::showEvent(event); d->applyInitialSizes(); d->layoutGroups();
}
bool LinkedNativeRails::eventFilter(QObject* watched, QEvent* event) {
    for (int side=0;side<2;++side) {
        if (!d->rails[side]) continue;
        auto* bar=d->rails[side]->verticalScrollBar();
        const bool outer=watched==d->rails[side]->viewport()||watched==bar;
        if (outer&&event->type()==QEvent::Wheel&&bar->maximum()>bar->minimum()) {
            auto* wheel=static_cast<QWheelEvent*>(event);
            if (!wheel->angleDelta().isNull()||!wheel->pixelDelta().isNull()) d->detach(side,QStringLiteral("outer-wheel"));
        }
        if (watched==bar&&event->type()==QEvent::MouseButtonPress) d->detach(side,QStringLiteral("scrollbar-mouse"));
        if (watched==bar&&event->type()==QEvent::KeyPress) {
            const int key=static_cast<QKeyEvent*>(event)->key();
            if (key==Qt::Key_Up||key==Qt::Key_Down||key==Qt::Key_Left||key==Qt::Key_Right
                ||key==Qt::Key_PageUp||key==Qt::Key_PageDown||key==Qt::Key_Home||key==Qt::Key_End)
                d->detach(side,QStringLiteral("scrollbar-key"));
        }
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide) d->layoutGroups();
    if (event->type() == QEvent::Move) { ++d->presentationSerial;emit presentationChanged(); }
    return QSplitter::eventFilter(watched, event);
}
