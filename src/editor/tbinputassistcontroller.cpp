#include "tbinputassistcontroller.h"
#include "mycodeeditor.h"
#include "uicontrols.h"
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr auto sourceId = "tb-input-assist";
QString key(int i) { return QStringLiteral("tb-input:%1").arg(i); }
QLabel* label(const QString& text, QWidget* parent) {
    auto* value = UiControls::label(text, parent);
    value->setTextFormat(Qt::PlainText); value->setWordWrap(true);
    value->setMinimumWidth(0);
    return value;
}
}
TbInputAssistController::TbInputAssistController(MyCodeEditor* e) : QObject(e), editor(e) {
    if (!TbInputAssistService::interactiveEnabled()) return;
    editor->installEventFilter(this);
    connect(editor, &QPlainTextEdit::textChanged, this, &TbInputAssistController::invalidate);
    refreshTimer.setSingleShot(true); refreshTimer.setInterval(180);
    connect(editor, &MyCodeEditor::documentChangeApplied, this, [this] {
        if (hasInputContext) refreshTimer.start();
    });
    connect(&refreshTimer, &QTimer::timeout, editor, &MyCodeEditor::refreshSemanticPresentation);
}
void TbInputAssistController::publish(const QList<TbInputAssist>& inputs) {
    if (!TbInputAssistService::interactiveEnabled()) { invalidate(); return; }
    if (document != editor->document() || file != editor->documentFileName()
        || revision != editor->semanticDocumentRevision()
        || token.snapshot != SemanticIndex::getInstance()->snapshot()
        || !editor->tbAssistDependenciesCurrent()) closePopup();
    items = inputs; regions.clear();
    hasInputContext = !inputs.isEmpty();
    document = editor->document(); file = editor->documentFileName();
    revision = editor->semanticDocumentRevision();
    token = SemanticIndex::getInstance()->snapshotToken();
    QList<EditorAnnotation> annotations;
    for (int i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        EditorAnnotation annotation;
        annotation.kind = EditorAnnotationKind::EffectiveValue;
        annotation.placement = EditorAnnotationPlacement::EndOfLine;
        annotation.range = {item.start, item.start + item.length, item.line, item.line};
        annotation.text = item.annotation;
        annotation.detail = item.status;
        annotation.semanticKey = key(i);
        annotation.priority = 75;
        annotations << annotation;
    }
    editor->setTbAssistAnnotations(annotations);
}
void TbInputAssistController::invalidate() {
    closePopup(); items.clear(); regions.clear(); token = {};
    if (!TbInputAssistService::interactiveEnabled() || document != editor->document() || file != editor->documentFileName()) { hasInputContext = false; refreshTimer.stop(); }
    editor->setTbAssistAnnotations({});
}
void TbInputAssistController::closePopup() {
    if (popup) { popup->hide(); popup->deleteLater(); popup = nullptr; }
}
void TbInputAssistController::beginPaint() { regions.clear(); }
void TbInputAssistController::painted(const QString& id, const QRect& rect) { regions.insert(id, rect); }
bool TbInputAssistController::click(const QPoint& pos) {
    for (auto it = regions.cbegin(); it != regions.cend(); ++it) if (it.value().contains(pos)) return open(it.key().mid(9).toInt());
    return false;
}
bool TbInputAssistController::hover(const QPoint& pos) {
    for (const auto& rect : regions) if (rect.contains(pos)) { editor->viewport()->setCursor(Qt::PointingHandCursor); return true; }
    editor->viewport()->setCursor(Qt::IBeamCursor);
    return false;
}
bool TbInputAssistController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor && (event->type() == QEvent::Hide || event->type() == QEvent::Close || event->type() == QEvent::Resize)) closePopup();
    if (watched == popup && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { closePopup(); return true; }
    return false;
}
bool TbInputAssistController::valid(const TbInputAssist& input) const {
    return TbInputAssistService::interactiveEnabled() && document && editor->document() == document && file == editor->documentFileName()
        && revision == editor->semanticDocumentRevision() && token.isValid()
        && token.snapshot == SemanticIndex::getInstance()->snapshot()
        && editor->tbAssistDependenciesCurrent()
        && input.start >= 0 && editor->cachedDocumentSlice(input.start, input.length) == input.expression;
}
void TbInputAssistController::apply(const TbInputAssist& input, int start, int length,
    const QString& expected, const QString& replacement) {
    if (!valid(input) || !input.current || editor->isReadOnly()
        || start < 0 || editor->cachedDocumentSlice(start, length) != expected) {
        closePopup();
        emit editor->editorStatusMessageRequested(QStringLiteral("The document or semantic facts changed; reopen the input helper."));
        return;
    }
    closePopup();
    auto userEdit = editor->beginUserEdit();
    auto edit = editor->beginSynchronousEditTransaction();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(start); cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
    cursor.endEditBlock();
    editor->setTextCursor(cursor); editor->setFocus(Qt::OtherFocusReason);
}
void TbInputAssistController::addContextAction(QMenu* menu, int position) {
    for (int i = 0; i < items.size(); ++i) if (editor->document()->findBlock(position).blockNumber() == items[i].line) {
        auto* action = menu->addAction(QStringLiteral("Edit TB input: %1…").arg(items[i].signal));
        connect(action, &QAction::triggered, this, [this, i] { open(i); });
    }
}
QFrame* TbInputAssistController::makePopup(const TbInputAssist& input, const QString& title) {
    closePopup();
    popup = new QFrame(editor);
    popup->setObjectName(QStringLiteral("tbInputAssistPopup"));
    popup->installEventFilter(this);
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setAutoFillBackground(true);
    popup->setBackgroundRole(QPalette::Base);
    popup->setStyleSheet(QStringLiteral("QFrame#tbInputAssistPopup { background: palette(base); border: 1px solid palette(mid); border-radius: 5px; }"));
    auto* layout = new QVBoxLayout(popup);
    auto* heading = new QHBoxLayout;
    heading->addWidget(label(input.signal + QStringLiteral(" · ") + title, popup), 1);
    auto* close = UiControls::toolButton(popup);
    close->setText(QStringLiteral("×")); close->setAccessibleName(QStringLiteral("Close input helper"));
    heading->addWidget(close); layout->addLayout(heading);
    connect(close, &QToolButton::clicked, this, &TbInputAssistController::closePopup);
    return popup;
}
void TbInputAssistController::placePopup(const TbInputAssist& input) {
    if (!popup) return;
    const auto available = editor->viewport()->geometry().adjusted(4, 4, -4, -4);
    const int width = qMin(460, qMax(140, available.width()));
    popup->setFixedWidth(width); popup->adjustSize();
    QTextCursor cursor(editor->document()); cursor.setPosition(input.start);
    const auto anchor = editor->viewport()->mapTo(editor, editor->cursorRect(cursor).bottomLeft());
    const int x = qBound(available.left(), anchor.x(), qMax(available.left(), available.right() - width + 1));
    const int y = qBound(available.top(), anchor.y() + 5, qMax(available.top(), available.bottom() - popup->height() + 1));
    popup->move(x, y); popup->show(); popup->raise();
}
bool TbInputAssistController::open(int index) {
    if (index < 0 || index >= items.size()) return false;
    const auto input = items[index];
    if (!valid(input)) { invalidate(); return false; }
    if (input.enumValues.isEmpty()) { showRule(input, !input.rule.bound); return true; }
    auto* frame = makePopup(input, QStringLiteral("Enum initializer"));
    auto* layout = qobject_cast<QVBoxLayout*>(frame->layout());
    layout->addWidget(label(QStringLiteral("Source: %1").arg(input.expression), frame));
    if (!input.status.isEmpty()) layout->addWidget(label(input.status, frame));
    auto* scroll = UiControls::scrollArea(frame); scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll); auto* choices = new QVBoxLayout(body);
    for (const auto& member : input.enumValues) {
        const QString expression = member.expressionText;
        const QString display = expression.isEmpty() ? member.name : expression;
        auto* button = UiControls::pushButton(QStringLiteral("%1 = %2").arg(display, member.displayValueText), body);
        button->setObjectName(QStringLiteral("tbEnum.%1").arg(member.name));
        button->setToolTip(expression.isEmpty() ? member.expressionFailureReason
            : expression + QStringLiteral(" = ") + member.valueText);
        button->setEnabled(input.current && member.available && !expression.isEmpty() && !editor->isReadOnly());
        choices->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, input, expression] {
            if (!expression.isEmpty()) apply(input, input.start, input.length, input.expression, expression);
        });
    }
    scroll->setWidget(body); scroll->setMinimumHeight(qMin(190, 42 * input.enumValues.size() + 12));
    scroll->setMaximumHeight(220); layout->addWidget(scroll);
    placePopup(input);
    return true;
}
void TbInputAssistController::showRule(const TbInputAssist& input, bool binding) {
    auto* frame = makePopup(input, binding ? QStringLiteral("Bind engineering rule") : QStringLiteral("Engineering value"));
    auto* layout = qobject_cast<QVBoxLayout*>(frame->layout());
    auto* scroll = UiControls::scrollArea(frame); scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll); auto* form = new QFormLayout(body);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    scroll->setWidget(body); layout->addWidget(scroll);
    auto* message = label({}, frame); message->setObjectName(QStringLiteral("tbRuleStatus"));
    auto* applyButton = UiControls::pushButton(binding ? QStringLiteral("Bind rule") : QStringLiteral("Apply to source"), frame);
    applyButton->setObjectName(QStringLiteral("tbRuleApply"));
    if (binding) {
        auto* kind = UiControls::comboBox(body); kind->setObjectName(QStringLiteral("tbRuleKind"));
        kind->addItem(QStringLiteral("y = a / (N + b)"), QStringLiteral("reciprocal"));
        kind->addItem(QStringLiteral("y = a * N + b"), QStringLiteral("linear"));
        kind->setCurrentIndex(input.rule.kind == QStringLiteral("linear") ? 1 : 0);
        auto* scale = UiControls::lineEdit(input.rule.scale, body); scale->setObjectName(QStringLiteral("tbRuleScale"));
        auto* offset = UiControls::lineEdit(input.rule.bound ? input.rule.offset : QStringLiteral("0"), body); offset->setObjectName(QStringLiteral("tbRuleOffset"));
        auto* unit = UiControls::lineEdit(input.rule.unit, body); unit->setObjectName(QStringLiteral("tbRuleUnit"));
        auto* origin = UiControls::lineEdit(input.rule.source, body); origin->setObjectName(QStringLiteral("tbRuleSource"));
        form->addRow(QStringLiteral("Pure numeric rule (N is the source value)"), kind);
        form->addRow(QStringLiteral("a · explicit scale / reference"), scale);
        form->addRow(QStringLiteral("b · offset"), offset);
        form->addRow(QStringLiteral("Engineering unit"), unit);
        form->addRow(QStringLiteral("Rule source / reference"), origin);
        const auto rule = [=] { TbEngineeringRule r; r.bound = true; r.kind = kind->currentData().toString(); r.scale = scale->text(); r.offset = offset->text(); r.unit = unit->text(); r.source = origin->text(); return r; };
        const auto update = [=] {
            auto proposal = input; proposal.rule = rule();
            QString issue = TbInputAssistService::validateRule(proposal.rule);
            if (!input.current) issue = input.status;
            const QString value = issue.isEmpty() ? TbInputAssistService::engineeringValue(proposal, &issue) : QString();
            message->setText(issue.isEmpty() ? QStringLiteral("Current source %1 → %2 %3\nThe rule is saved as an adjacent source comment.").arg(input.expression, value, proposal.rule.unit) : issue);
            applyButton->setEnabled(input.current && TbInputAssistService::validateRule(proposal.rule).isEmpty() && !editor->isReadOnly());
        };
        for (auto* field : {scale, offset, unit, origin}) connect(field, &QLineEdit::textChanged, frame, update);
        connect(kind, &QComboBox::currentIndexChanged, frame, update);
        connect(applyButton, &QPushButton::clicked, this, [this, input, rule] {
            const int statementLine = editor->document()->findBlock(input.statementStart).position();
            const auto prefix = editor->cachedDocumentSlice(statementLine, input.statementStart - statementLine);
            if (!prefix.trimmed().isEmpty()) { emit editor->editorStatusMessageRequested(QStringLiteral("Put this initializer on its own source line before binding a rule.")); return; }
            const QString comment = prefix + TbInputAssistService::ruleComment(input, rule()) + QLatin1Char('\n');
            const int at = input.ruleStart >= 0 ? input.ruleStart : statementLine;
            const int length = input.ruleStart >= 0 ? input.ruleLength : 0;
            apply(input, at, length, editor->cachedDocumentSlice(at, length), comment);
        });
        update();
    } else {
        QString error;
        const auto current = TbInputAssistService::engineeringValue(input, &error);
        auto* target = UiControls::lineEdit(current, body); target->setObjectName(QStringLiteral("tbEngineeringTarget"));
        form->addRow(QStringLiteral("Target (%1)").arg(input.rule.unit), target);
        form->addRow(label(QStringLiteral("Rule: %1; a=%2, b=%3\nSource: %4\nCurrent code: %5")
            .arg(input.rule.kind == QStringLiteral("linear") ? QStringLiteral("y=a*N+b") : QStringLiteral("y=a/(N+b)"), input.rule.scale, input.rule.offset, input.rule.source, input.expression), body));
        auto* preview = label({}, body); preview->setObjectName(QStringLiteral("tbEngineeringPreview")); form->addRow(preview);
        auto* rebind = UiControls::pushButton(QStringLiteral("Edit rule…"), body); form->addRow(rebind);
        connect(rebind, &QPushButton::clicked, this, [this, input] { showRule(input, true); });
        const auto update = [=] {
            const auto p = TbInputAssistService::preview(input, target->text());
            message->setText(p.error);
            preview->setText(p.valid ? QStringLiteral("Write: %1\nAchievable: %2 %3\nError: %4\n%5")
                .arg(p.expression, p.actual, input.rule.unit, p.difference, p.quantized ? QStringLiteral("Quantized to the nearest integer configuration; apply to accept.") : QStringLiteral("Exact conversion.")) : QString());
            applyButton->setEnabled(p.valid && !editor->isReadOnly());
        };
        connect(target, &QLineEdit::textChanged, frame, update);
        connect(applyButton, &QPushButton::clicked, this, [this, input, target] {
            const auto p = TbInputAssistService::preview(input, target->text());
            if (p.valid) apply(input, input.start, input.length, input.expression, p.expression);
        });
        update();
    }
    scroll->setMinimumHeight(qMin(binding ? 350 : 265, qMax(90, editor->height() - 185)));
    scroll->setMaximumHeight(qMax(90, editor->height() - 185));
    layout->addWidget(message); layout->addWidget(applyButton);
    placePopup(input);
}
