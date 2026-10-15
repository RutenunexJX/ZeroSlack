#include "mycodeeditor.h"
#include "documenttemplatemodel.h"
#include "editorruntime.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
int mainPosition(const TemplateInstance& item) {
    for (const auto& fragment : item.fragments)
        if (!fragment.auxiliary) return fragment.start;
    return item.fragments.isEmpty() ? 0 : item.fragments.first().start;
}
QString inputLabel(const CodeTemplateSlot& input) {
    QString label = input.input.value("label", input.name).toString();
    label.replace('_', ' ');
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}
}

QList<MyCodeEditor::TemplateRegionTarget> MyCodeEditor::templateRegions() const {
    QList<TemplateRegionTarget> result;
    for (const auto& item : templateModel()->instances()) {
        QSet<QString> names;
        for (const auto& input : item.inputs) {
            if (input.input.value("kind").toString() != "Code" || names.contains(input.name)) continue;
            names.insert(input.name);
            result << TemplateRegionTarget{item.id, input.name, inputLabel(input),
                input.input.value("content").toString(), input.start, input.length,
                templateModel()->depth(item.id)};
        }
    }
    return result;
}

QString MyCodeEditor::selectedTemplateRegion() const {
    if (templateInputEditing()) {
        if (!state->templateSlots.active()) return activeTemplateRegion;
        const auto name = state->templateSlots.activeInputName();
        const auto* item = templateModel()->instance(activeTemplate);
        if (item) for (const auto& input : item->inputs)
            if (input.name == name && input.input.value("kind").toString() == "Code") return name;
        return {};
    }
    return activeTemplateRegion;
}

QList<MyCodeEditor::TemplateNavigationTarget> MyCodeEditor::templateChildren(
    const QString& owner, const QString& input) const {
    QList<TemplateNavigationTarget> result;
    if (!owner.isEmpty() && input.isEmpty()) {
        const auto* item = templateModel()->instance(owner);
        if (!item) return result;
        for (const auto& slot : item->inputs)
            if (slot.input.value("kind").toString() != "Code") {
                result << TemplateNavigationTarget{owner, {}, TemplateTargetKind::Inputs};
                break;
            }
        for (const auto& region : templateRegions())
            if (region.owner == owner)
                result << TemplateNavigationTarget{owner, region.name, TemplateTargetKind::Region};
        return result;
    }
    if (!owner.isEmpty())
        result << TemplateNavigationTarget{owner, input, TemplateTargetKind::Handwriting};
    auto entries = templateModel()->instances();
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return mainPosition(a) < mainPosition(b);
    });
    for (const auto& item : entries)
        if (item.parentId == owner && (owner.isEmpty() || item.parentSlot == input))
            result << TemplateNavigationTarget{item.id, {}, TemplateTargetKind::Instance};
    return result;
}

int MyCodeEditor::templateRegionEntry(const TemplateRegionTarget& region) const {
    const auto* item = templateModel()->instance(region.owner);
    if (!item) return region.start;
    int index = -1;
    for (int i = 0; i < item->inputs.size(); ++i)
        if (item->inputs[i].name == region.name) { index = i; break; }
    const int end = region.start + region.length;
    // Prefer the final blank line, rather than the first fixed token after it.
    int position = end;
    if (position > region.start && (document()->characterAt(position - 1) == '\n' ||
        document()->characterAt(position - 1) == QChar::ParagraphSeparator)) --position;
    if (templateModel()->allows(region.owner, index, position, position)) return position;
    return end;
}

void MyCodeEditor::selectTemplateTarget(const TemplateNavigationTarget& target) {
    clearTemplatePreedit();
    state->templateSlots.clear(this);
    templateEditing = false;
    activeTemplate = target.owner;
    activeTemplateRegion = target.input;
    templateTargetKind = target.kind;
    int position = 0;
    const auto* item = templateModel()->instance(activeTemplate);
    if (item) position = mainPosition(*item);
    if (!target.input.isEmpty()) for (const auto& region : templateRegions())
        if (region.owner == target.owner && region.name == target.input) {
            position = templateRegionEntry(region);
            break;
        }
    QTextCursor cursor(document());
    cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
    setTextCursor(cursor);
    ensureCursorVisible();
    updateTemplatePresentation();
    viewport()->update();
}

void MyCodeEditor::templateNavigate(int direction) {
    QString owner, input;
    if (templateTargetKind == TemplateTargetKind::Instance) {
        if (const auto* item = templateModel()->instance(activeTemplate)) {
            owner = item->parentId;
            input = item->parentSlot;
        }
    } else {
        owner = activeTemplate;
        if (templateTargetKind == TemplateTargetKind::Handwriting) input = activeTemplateRegion;
    }
    const auto siblings = templateChildren(owner, input);
    if (siblings.isEmpty()) return;
    int index = -1;
    for (int i = 0; i < siblings.size(); ++i)
        if (siblings[i].owner == activeTemplate && siblings[i].input == activeTemplateRegion &&
            siblings[i].kind == templateTargetKind) index = i;
    index = index < 0 ? (direction < 0 ? siblings.size() - 1 : 0)
        : (index + direction + siblings.size()) % siblings.size();
    selectTemplateTarget(siblings[index]);
}

void MyCodeEditor::enterTemplateTarget() {
    if (activeTemplate.isEmpty()) { templateNavigate(1); return; }
    if (templateTargetKind == TemplateTargetKind::Region) {
        const auto children = templateChildren(activeTemplate, activeTemplateRegion);
        if (!children.isEmpty()) selectTemplateTarget(children.first());
        return;
    }
    if (templateTargetKind == TemplateTargetKind::Instance) {
        const auto children = templateChildren(activeTemplate);
        const bool container = std::any_of(children.begin(), children.end(), [](const auto& target) {
            return target.kind == TemplateTargetKind::Region;
        });
        if (container) { selectTemplateTarget(children.first()); return; }
    }
    if (templateTargetKind == TemplateTargetKind::Handwriting) {
        selectTemplateRegion(activeTemplate, activeTemplateRegion, true);
        return;
    }
    editTemplateInstance();
}

void MyCodeEditor::leaveTemplateTarget() {
    if (templateTargetKind == TemplateTargetKind::Handwriting) {
        selectTemplateTarget({activeTemplate, activeTemplateRegion, TemplateTargetKind::Region});
    } else if (templateTargetKind != TemplateTargetKind::Instance) {
        selectTemplateTarget({activeTemplate, {}, TemplateTargetKind::Instance});
    } else if (const auto* item = templateModel()->instance(activeTemplate); item && !item->parentId.isEmpty()) {
        selectTemplateTarget({item->parentId, item->parentSlot, TemplateTargetKind::Region});
    } else {
        updateTemplatePresentation();
    }
}

bool MyCodeEditor::selectTemplateRegion(const QString& owner, const QString& name, bool enter) {
    if (!authoringLock) setTemplateAuthoringLocked(true);
    if (templateInputEditing()) {
        finishTemplateInput();
        if (templateInputEditing()) return false;
    }
    for (const auto& region : templateRegions()) {
        if (region.owner != owner || region.name != name) continue;
        selectTemplateTarget({owner, name, enter ? TemplateTargetKind::Handwriting : TemplateTargetKind::Region});
        if (enter) {
            QTextCursor context(document()); context.setPosition(region.start);
            const auto plan = EditorStructuralInputController::newlineEdit(context, state->syntax);
            auto indent = plan.text.left(plan.caretOffset).mid(1);
            const auto firstLine = context.block().text();
            int spaces = 0;
            while (spaces < firstLine.size() && (firstLine[spaces] == ' ' || firstLine[spaces] == '\t')) ++spaces;
            if (spaces > indent.size()) indent = firstLine.left(spaces);
            if (editTemplateInstance(owner, name, templateRegionEntry(region))) indentTemplateContinuation(indent);
        }
        setFocus(Qt::MouseFocusReason);
        return true;
    }
    return false;
}

QString MyCodeEditor::templateTargetPath() const {
    QStringList path;
    const auto* item = templateModel()->instance(activeTemplate);
    while (item) {
        path.prepend(item->label);
        const auto parentSlot = item->parentSlot;
        item = templateModel()->instance(item->parentId);
        if (item) for (const auto& input : item->inputs)
            if (input.name == parentSlot) { path.prepend(inputLabel(input)); break; }
    }
    const auto* selected = templateModel()->instance(activeTemplate);
    const auto region = selectedTemplateRegion();
    if (selected && !region.isEmpty()) {
        for (const auto& input : selected->inputs)
            if (input.name == region) { path << inputLabel(input); break; }
        if (!templateEditing && templateTargetKind == TemplateTargetKind::Handwriting) path << "Handwritten code";
    } else if (selected && (templateEditing || templateTargetKind == TemplateTargetKind::Inputs)) {
        path << "Inputs";
    }
    return path.isEmpty() ? QStringLiteral("Document") : path.join(" / ");
}

void MyCodeEditor::setTemplateTriggerHeld(bool held) {
    if (templateTriggerHeld == held) return;
    templateTriggerHeld = held;
    updateTemplateRegionBar();
}

int MyCodeEditor::templateRegionBarHeight() const {
    return templateRegionBar && !templateRegionBar->isHidden() ? templateRegionBar->sizeHint().height() : 0;
}

void MyCodeEditor::layoutTemplateRegionBar() {
    if (!templateRegionBar) return;
    const auto rect = contentsRect();
    templateRegionBar->setGeometry(rect.left(), rect.top(), rect.width(), templateRegionBarHeight());
    if (auto* path = templateRegionBar->findChild<QLabel*>("templateLocation"))
        path->setText(path->fontMetrics().elidedText(path->property("fullText").toString(), Qt::ElideLeft, qMax(0, path->width())));
}

void MyCodeEditor::updateTemplateRegionBar() {
    if (updatingTemplateRegionBar) return;
    updatingTemplateRegionBar = true;
    const auto regions = templateRegions();
    if (!templateRegionBar && !authoringLock && !templateTriggerHeld && regions.isEmpty()) {
        updatingTemplateRegionBar = false;
        return;
    }
    QString owner = activeTemplate;
    while (const auto* item = templateModel()->instance(owner)) {
        if (std::any_of(regions.begin(), regions.end(), [&](const auto& region) { return region.owner == owner; })) break;
        owner = item->parentId;
    }
    if (owner.isEmpty() && !regions.isEmpty()) owner = regions.first().owner;
    QString key = owner;
    for (const auto& region : regions) if (region.owner == owner) key += '/' + region.name;
    if (!templateRegionBar || templateRegionBarKey != key) {
        delete templateRegionBar.data();
        templateRegionBar = new QWidget(this);
        templateRegionBar->setObjectName("templateRegionBar");
        templateRegionBar->setAutoFillBackground(true);
        templateRegionBarKey = key;
        auto* rows = new QVBoxLayout(templateRegionBar);
        rows->setContentsMargins(7, 4, 7, 4); rows->setSpacing(3);
        auto* status = new QHBoxLayout;
        auto* mode = new QLabel(templateRegionBar); mode->setObjectName("templateModeBadge");
        auto* path = new QLabel(templateRegionBar); path->setObjectName("templateLocation");
        path->setMinimumWidth(0); path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        status->addWidget(mode); status->addWidget(path, 1); rows->addLayout(status);
        auto* buttons = new QHBoxLayout; buttons->setSpacing(5);
        for (const auto& region : regions) if (region.owner == owner) {
            auto* button = new QToolButton(templateRegionBar);
            button->setObjectName("templateRegion_" + region.name);
            button->setProperty("regionName", region.name);
            button->setFocusPolicy(Qt::NoFocus); button->setCheckable(true);
            button->setToolTip("Enter " + region.label + " handwritten code. Ctrl+Enter returns to navigation.");
            connect(button, &QToolButton::clicked, this, [this, id = region.owner, name = region.name] {
                selectTemplateRegion(id, name, true);
            });
            buttons->addWidget(button);
        }
        buttons->addStretch(); rows->addLayout(buttons);
    }
    const bool visible = authoringLock || templateTriggerHeld || !regions.isEmpty();
    auto* mode = templateRegionBar->findChild<QLabel*>("templateModeBadge");
    const auto stateLabel = authoringLock ? (templateSearching ? "Search" : templateEditing ? "Editing inputs" : "Select") : "";
    mode->setText(authoringLock ? QString("TEMPLATE LOCK · %1").arg(stateLabel) :
        templateTriggerHeld ? "F24 HELD · Type to search · Space to lock" :
        templateEditing ? "TEMPLATE INPUTS · Editing" : "TEMPLATE REGIONS · Normal editing");
    const bool dark = palette().color(QPalette::Base).lightnessF() < 0.5;
    const QString color = authoringLock ? (dark ? "#71c6ff" : "#075b9a") : (dark ? "#dedede" : "#303030");
    const auto style = QString("QLabel { font-weight: 600; color: %1; padding: 2px 4px; border: 1px solid %1; border-radius: 3px; }").arg(color);
    if (mode->styleSheet() != style) mode->setStyleSheet(style);
    auto* path = templateRegionBar->findChild<QLabel*>("templateLocation");
    const QString full = templateTargetPath();
    path->setProperty("fullText", full); path->setToolTip(full + "\nEnter: enter · ↑/↓: siblings · Ctrl+Enter: parent · Esc: exit lock");
    for (const auto& region : regions) if (region.owner == owner) {
        auto* button = templateRegionBar->findChild<QToolButton*>("templateRegion_" + region.name);
        const bool empty = cachedDocumentText().mid(region.start, region.length).trimmed().isEmpty();
        button->setText(region.label + (empty ? " · Empty" : ""));
        button->setChecked(region.owner == activeTemplate && region.name == selectedTemplateRegion());
    }
    templateRegionBar->setVisible(visible);
    state->gutter.updateViewportMargins(this);
    state->gutter.resizeTo(this, contentsRect());
    layoutTemplateRegionBar();
    updatingTemplateRegionBar = false;
}

void MyCodeEditor::paintTemplateRegions(QPainter& painter, QPaintEvent* event) {
    painter.save(); painter.setClipRect(event->rect());
    for (const auto& region : templateRegions()) {
        const auto first = document()->findBlock(region.start);
        const auto last = document()->findBlock(qMax(region.start, region.start + region.length - 1));
        if (!first.isValid() || !last.isValid()) continue;
        const auto a = blockGeometry(first.blockNumber()), b = blockGeometry(last.blockNumber());
        QRectF rect(7 + 9 * region.depth, a.top + 1, qMax(1, viewport()->width() - 17 - 9 * region.depth), b.top + b.height - a.top - 2);
        if (!rect.intersects(event->rect())) continue;
        const bool selected = activeTemplate == region.owner && selectedTemplateRegion() == region.name;
        auto color = palette().color(selected ? QPalette::Highlight : QPalette::Mid);
        painter.setPen(QPen(color, selected ? 2 : 1, selected ? Qt::SolidLine : Qt::DashLine));
        painter.drawRoundedRect(rect, 3, 3);
        if (cachedDocumentText().mid(region.start, region.length).trimmed().isEmpty() && first.text().trimmed().isEmpty()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            const QString hint = selected && templateEditing ? " · @ to insert · Ctrl+Enter: navigation" : " · Empty · Double-click to enter";
            painter.drawText(rect.adjusted(28, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, region.label + hint);
        }
    }
    painter.restore();
}

bool MyCodeEditor::handleTemplateRegionMouse(const QPoint& point, bool enter) {
    for (const auto& region : templateRegions()) {
        const auto first = document()->findBlock(region.start);
        const auto last = document()->findBlock(qMax(region.start, region.start + region.length - 1));
        const auto a = blockGeometry(first.blockNumber()), b = blockGeometry(last.blockNumber());
        if (point.y() < a.top || point.y() > b.top + b.height) continue;
        const bool empty = cachedDocumentText().mid(region.start, region.length).trimmed().isEmpty() && first.text().trimmed().isEmpty();
        if (!enter && empty && templateEditing && activeTemplate == region.owner && selectedTemplateRegion() == region.name) return false;
        if (empty || (point.x() >= 4 + 9 * region.depth && point.x() <= 16 + 9 * region.depth))
            return selectTemplateRegion(region.owner, region.name, enter);
    }
    return false;
}
