#include "linkednativerails.h"
#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

struct LinkedNativeRails::Private {
    struct Card {
        Reference reference;
        QFrame* frame = nullptr;
        QLabel* title = nullptr;
        QLabel* location = nullptr;
        QStackedWidget* body = nullptr;
        QTextBrowser* text = nullptr;
        PinloomPreviewImageLabel* image = nullptr;
        QString state;
    };
    struct GroupView { Group value; QFrame* frame = nullptr; int side = 0; };
    LinkedNativeRails* q;
    QScrollArea* rails[2]{};
    QWidget* contents[2]{};
    QVBoxLayout* layouts[2]{};
    QList<GroupView> groups;
    QMap<QString, Card> cards;
    QStringList visible;
    QTimer visibilityTimer;
    QList<int> requestedSizes;
    double requestedRatio = .5;
    bool initialSizesPending = false;
    bool rebuilding = false;
    bool visibilityInvalidated = false;

    explicit Private(LinkedNativeRails* owner) : q(owner) {
        visibilityTimer.setSingleShot(true);
        QObject::connect(&visibilityTimer, &QTimer::timeout, q, [this] { reportVisible(); });
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
        QStringList before;
        for (const auto& group : groups) before.append(group.value.anchor);
        std::stable_sort(groups.begin(), groups.end(), [](const GroupView& a, const GroupView& b) {
            return a.value.y != b.value.y ? a.value.y < b.value.y : a.value.anchor < b.value.anchor;
        });
        QStringList after;
        for (const auto& group : groups) after.append(group.value.anchor);
        if (before == after && !rebuilding) return;
        const int scrolls[] = {rails[0]->verticalScrollBar()->value(), rails[1]->verticalScrollBar()->value()};
        for (auto& group : groups) layouts[group.side]->removeWidget(group.frame);
        for (int i = 0; i < groups.size(); ++i) {
            auto& group = groups[i]; group.side = i % 2;
            layouts[group.side]->addWidget(group.frame);
            group.frame->setProperty("rail", group.side == 0 ? "left" : "right");
            group.frame->show();
        }
        for (int side = 0; side < 2; ++side) {
            layouts[side]->activate();
            rails[side]->verticalScrollBar()->setValue(scrolls[side]);
        }
    }
    Card makeCard(const Reference& reference, QWidget* parent) {
        Card card; card.reference = reference;
        card.frame = new QFrame(parent); card.frame->setObjectName("linkedNativeCard");
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
        return card;
    }
    void display(Card& card, const Preview& preview) {
        const QString title = preview.title.isEmpty() ? card.reference.title : preview.title;
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
        auto* area = UiControls::scrollArea(this); d->rails[side] = area;
        area->setObjectName(side == 0 ? "linkedNativeLeftRail" : "linkedNativeRightRail");
        area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame); area->setMinimumWidth(0);
        area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body = new QWidget(area); d->contents[side] = body;
        body->setObjectName(side == 0 ? "linkedNativeLeftGroups" : "linkedNativeRightGroups");
        body->setMinimumWidth(0); body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* layout = new QVBoxLayout(body); d->layouts[side] = layout;
        layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(8); layout->setAlignment(Qt::AlignTop);
        area->setWidget(body); area->viewport()->installEventFilter(this);
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { d->scheduleVisible(); });
    }
    addWidget(d->rails[0]); addWidget(editorPage); addWidget(d->rails[1]);
    setStretchFactor(0, 0); setStretchFactor(1, 1); setStretchFactor(2, 0);
    connect(this, &QSplitter::splitterMoved, this, [this] {
        const auto values = sizes(); const int sum = values[0] + values[1] + values[2];
        if (sum > 0) emit codeRatioChanged(double(values[1]) / sum);
        d->scheduleVisible();
    });
}
LinkedNativeRails::~LinkedNativeRails() = default;

void LinkedNativeRails::setGroups(const QList<Group>& groups) {
    d->rebuilding = true; d->visibilityInvalidated = true; d->visibilityTimer.stop();
    const int scrolls[] = {d->rails[0]->verticalScrollBar()->value(), d->rails[1]->verticalScrollBar()->value()};
    for (const auto& group : d->groups) delete group.frame;
    d->groups.clear(); d->cards.clear();
    QSet<QString> seen;
    for (const auto& value : groups) {
        if (value.references.isEmpty() || seen.contains(value.anchor)) continue;
        seen.insert(value.anchor);
        auto* frame = new QFrame(d->contents[0]); frame->setObjectName("linkedNativeGroup");
        frame->setProperty("anchorId", value.anchor); frame->setMinimumWidth(0);
        frame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(0, 0, 0, 0);
        auto* title = new QLabel(value.title.isEmpty() ? QStringLiteral("References") : value.title, frame);
        title->setTextFormat(Qt::PlainText); title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); title->setToolTip(title->text());
        layout->addWidget(title);
        for (const auto& reference : value.references) {
            auto card = d->makeCard(reference, frame); d->display(card, {});
            layout->addWidget(card.frame); d->cards.insert(reference.id, card);
        }
        d->groups.append({value, frame, 0});
    }
    d->orderGroups();
    for (int side = 0; side < 2; ++side) d->rails[side]->verticalScrollBar()->setValue(scrolls[side]);
    d->rebuilding = false; d->scheduleVisible();
}
void LinkedNativeRails::updateAnchorPositions(const QMap<QString, int>& positions) {
    for (auto& group : d->groups) group.value.y = positions.value(group.value.anchor, group.value.y);
    d->orderGroups(); d->scheduleVisible();
}
void LinkedNativeRails::setPreview(const QString& source, const Preview& preview) {
    for (auto& card : d->cards) if (card.reference.source == source) d->display(card, preview);
    d->scheduleVisible();
}
void LinkedNativeRails::clearPreview(const QString& source) { setPreview(source, {}); }
void LinkedNativeRails::setInitialSizes(const QList<int>& saved, double codeRatio) {
    d->requestedSizes = saved; d->requestedRatio = qBound(.3, codeRatio, .7);
    d->initialSizesPending = true; d->applyInitialSizes();
}
QJsonObject LinkedNativeRails::snapshot() const {
    QJsonArray groups, cards, columns;
    for (int size : sizes()) columns.append(size);
    for (const auto& group : d->groups) {
        QJsonArray ids; for (const auto& reference : group.value.references) ids.append(reference.id);
        groups.append(QJsonObject{{"anchor",group.value.anchor},{"y",group.value.y},{"side",group.side},{"references",ids}});
    }
    for (const auto& card : d->cards) cards.append(QJsonObject{{"id",card.reference.id},{"source",card.reference.source},{"state",card.state}});
    return {{"groups",groups},{"cards",cards},{"visible",QJsonArray::fromStringList(d->visible)},{"sizes",columns}};
}
void LinkedNativeRails::resizeEvent(QResizeEvent* event) {
    QSplitter::resizeEvent(event); d->applyInitialSizes(); d->scheduleVisible();
}
void LinkedNativeRails::showEvent(QShowEvent* event) {
    QSplitter::showEvent(event); d->applyInitialSizes(); d->scheduleVisible();
}
bool LinkedNativeRails::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide) d->scheduleVisible();
    return QSplitter::eventFilter(watched, event);
}
