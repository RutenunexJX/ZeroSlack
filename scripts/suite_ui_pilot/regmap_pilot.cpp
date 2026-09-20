#include "regmap_pilot.h"
#include "pilot_style.h"
#include "pilot_tokens.h"
#include <QPointer>
#include <QAbstractItemView>
#include <QEvent>
#include <QStyleFactory>

namespace RegMapPilot {
namespace {
class ViewBoundaries final : public QObject {
public:
    explicit ViewBoundaries(QApplication& application) : QObject(&application) {
        fusion_ = QStyleFactory::create("Fusion");
        fusion_->setParent(this);
        application.installEventFilter(this);
    }
private:
    QStyle* fusion_ = nullptr;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::Polish && event->type() != QEvent::ParentChange) return false;
        auto* widget = qobject_cast<QWidget*>(object);
        if (!widget) return false;
        bool protect = false;
        for (auto* parent = widget; parent; parent = parent->parentWidget()) {
            const auto name = QByteArray(parent->metaObject()->className());
            if (qobject_cast<QAbstractItemView*>(parent) || name == "BitfieldView" || name == "AddressSpaceView") {
                protect = true;
                break;
            }
        }
        if (protect && !widget->property("pilotClassicView").toBool()) {
            widget->setProperty("pilotClassicView", true);
            widget->setStyle(fusion_);
        } else if (!protect && widget->property("pilotClassicView").toBool()) {
            widget->setProperty("pilotClassicView", false);
            widget->setStyle(nullptr);
        }
        return false;
    }
};
class Style final : public Pilot::Style {
    bool primary(const QWidget* widget) const override {
        if (!widget) return false;
        const auto name = widget->objectName();
        return name == "saveSyncButton" || name == "synchronizeButton" || Pilot::Style::primary(widget);
    }
};
}
bool enabled() { return qEnvironmentVariable("REGMAP_PILOT_STYLE") == "qlementine"; }
QStyle* install(QApplication& application) {
    // Application's QStyleSheetStyle wrapper is not the owned backend.
    static QPointer<Style> backend;
    if (!backend) {
        backend = new Style;
        backend->setObjectName("RegMapQlementinePilot");
        backend->setAutoIconColor(oclero::qlementine::AutoIconColor::None);
        application.setStyle(backend);
        new ViewBoundaries(application);
    }
    return backend;
}
void update(QStyle* style, WorkbenchTheme::Mode mode, const QFont& font, const QPalette& palette) {
    using oclero::qlementine::Theme;
    auto q = mode == WorkbenchTheme::Mode::dark ? Theme::makeDark() : Theme::makeLight();
    const auto& t = WorkbenchTheme::tokens(mode);
    q.meta.name = WorkbenchTheme::modeName(mode);
    q.backgroundColorMain1 = t.panel; q.backgroundColorMain2 = t.raisedSurface;
    q.backgroundColorMain3 = t.application; q.backgroundColorMain4 = t.panel;
    q.backgroundColorWorkspace = t.canvas;
    Pilot::applyControls(q, {
        {t.panel, t.selection, t.divider, t.panel},
        {t.text, t.text, t.text, t.mutedText},
        {t.accent, t.accent.lighter(110), t.accent.darker(110), t.panel},
        {t.onAccent, t.onAccent, t.onAccent, t.mutedText},
        {t.border, t.focus, t.accent, t.divider}, t.focus});
    q.neutralColorTransparent = t.panel;
    q.primaryAlternativeColor = t.selection; q.primaryAlternativeColorHovered = t.selection;
    q.primaryAlternativeColorPressed = t.selectionStrong; q.primaryAlternativeColorDisabled = t.panel;
    q.secondaryAlternativeColor = t.mutedText; q.secondaryAlternativeColorDisabled = t.mutedText;
    q.fontRegular = font; q.fontBold = font; q.fontBold.setWeight(QFont::DemiBold);
    q.fontCaption = font; q.palette = palette;
    auto* backend = static_cast<Style*>(style);
    backend->setTheme(q);
    backend->setAnimationsEnabled(!WorkbenchTheme::reducedMotionEnabled());
}
}
