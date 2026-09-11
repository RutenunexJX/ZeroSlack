#include "editorhoverpopup.h"

#include "actionregistry.h"
#include "effectivevalueservice.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"
#include "uitypography.h"

#include <QGraphicsDropShadowEffect>
#include <QScrollArea>
#include <QScrollBar>

#include <QApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString locationText(const QString& fileName, int line)
{
    if (fileName.isEmpty() || line <= 0)
        return QString();
    return QStringLiteral("defined at %1:%2")
        .arg(QFileInfo(fileName).fileName())
        .arg(line);
}

bool hasCurrentEffectiveValue(const SymbolHoverReport& report)
{
    return report.effectiveValueStatus == EffectiveValueStatus::Current;
}

QString effectiveValueLabel(const SymbolHoverReport& report)
{
    if (report.instanceBound || !report.defaultEvaluation)
        return QStringLiteral("effective value");
    return QStringLiteral(
        "default value (unbound instance / \u672a\u7ed1\u5b9a\u5b9e\u4f8b)");
}
}

EditorHoverPopup::EditorHoverPopup(QWidget* parent)
    : QFrame(parent, Qt::Widget)
{
    Q_ASSERT(parent);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("editorHoverPopup"));
    setProperty("unifiedPeekContainer", true);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame#editorHoverPopup {"
        "background: palette(base);"
        "border: 1px solid palette(mid);"
        "border-radius: 5px;"
        "}"
        "QLabel { color: palette(text); }"
        "QLineEdit {"
        "border: 1px solid palette(highlight);"
        "border-radius: 3px;"
        "padding: 3px 6px;"
        "background: palette(base);"
        "color: palette(text);"
        "}"
        "QPlainTextEdit {"
        "border: 1px solid palette(mid);"
        "border-radius: 3px;"
        "background: palette(base);"
        "color: palette(text);"
        "}"));

    defaultStyleSheet = styleSheet();
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(10, 8, 10, 8);
    outerLayout->setSpacing(4);

    headerHost = new QWidget(this);
    headerHost->setObjectName(QStringLiteral("peekHeader"));
    auto* headerLayout = new QHBoxLayout(headerHost);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    symbolIcon = new QLabel(headerHost);
    symbolIcon->setObjectName(QStringLiteral("peekSymbolIcon"));
    symbolIcon->setFixedSize(24, 24);
    symbolIcon->hide();
    headerLayout->addWidget(symbolIcon, 0, Qt::AlignTop);
    auto* titleStack = new QVBoxLayout;
    titleStack->setSpacing(6);
    titleLabel = new QLabel(headerHost);
    titleLabel->setObjectName(QStringLiteral("peekTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    titleStack->addWidget(titleLabel);
    categoryLabel = new QLabel(headerHost);
    categoryLabel->setObjectName(QStringLiteral("peekCategory"));
    categoryLabel->setTextFormat(Qt::PlainText);
    categoryLabel->hide();
    titleStack->addWidget(categoryLabel, 0, Qt::AlignLeft);
    headerLayout->addLayout(titleStack, 1);

    closeButton = new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("peekCloseButton"));
    closeButton->setText(QStringLiteral("\u00d7"));
    closeButton->setToolTip(QStringLiteral("Close peek (Esc)"));
    closeButton->setAutoRaise(true);
    closeButton->setFocusPolicy(Qt::NoFocus);
    headerLayout->addWidget(closeButton);
    outerLayout->addWidget(headerHost);

    layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    outerLayout->addLayout(layout);

    connect(closeButton,
            &QToolButton::clicked,
            this,
            &EditorHoverPopup::closePopup);

    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged, this, [this]() {
        if (currentContent.symbolCard) applyAppearance();
    });
    if (qApp)
        qApp->installEventFilter(this);
}

EditorHoverPopup::~EditorHoverPopup()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

void EditorHoverPopup::setNavigationHandler(NavigationHandler handler)
{
    navigationHandler = std::move(handler);
}

void EditorHoverPopup::setTransientPreview(bool transient)
{
    transientPreview = transient;
}

void EditorHoverPopup::showContent(const PeekContentModel& content,
                                   const QRect& globalAnchorRect,
                                   const QFont& editorFont)
{
    if (content.isEmpty()) {
        closePopup();
        return;
    }

    resetContent();
    setMinimumSize(0, 0);
    currentContent = content;
    contentFont = editorFont;
    anchorRect = globalAnchorRect;
    setFont(editorFont);
    focusReturnWidget = parentWidget();

    targetFile = content.navigationTarget.fileName;
    targetLine = content.navigationTarget.line;
    targetColumn = content.navigationTarget.column;

    QFont titleFont = editorFont;
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setText(content.title);
    titleLabel->setVisible(!content.title.isEmpty());
    closeButton->setFont(editorFont);

    applyAppearance();
    if (content.symbolCard)
        buildSymbolCard();
    else
        for (const PeekContentRow& row : content.rows)
            addLabel(row.text, row.role, row.wordWrap);
    if (content.readOnlyText.enabled) {
        auto* textEdit = new QPlainTextEdit(this);
        textEdit->setObjectName(
            content.readOnlyText.objectName.isEmpty()
                ? QStringLiteral("peekReadOnlyText")
                : content.readOnlyText.objectName);
        textEdit->setFont(editorFont);
        textEdit->setReadOnly(true);
        textEdit->setPlainText(content.readOnlyText.text);
        textEdit->setLineWrapMode(
            content.readOnlyText.wordWrap
                ? QPlainTextEdit::WidgetWidth
                : QPlainTextEdit::NoWrap);
        QSize minimum =
            content.readOnlyText.minimumSize.expandedTo(
                QSize(1, 1));
        if (parentWidget()) {
            minimum.setWidth(
                qMin(minimum.width(),
                     qMax(1, parentWidget()->width() - 28)));
            minimum.setHeight(
                qMin(minimum.height(),
                     qMax(1, parentWidget()->height() - 72)));
        }
        textEdit->setMinimumSize(minimum);
        layout->addWidget(textEdit);
    }
    if (content.editor.enabled) {
        auto* lineEdit = new QLineEdit(this);
        editControl = lineEdit;
        lineEdit->setObjectName(
            content.editor.objectName.isEmpty()
                ? QStringLiteral("peekEditableText")
                : content.editor.objectName);
        lineEdit->setFont(editorFont);
        lineEdit->setReadOnly(content.editor.readOnly);
        lineEdit->setText(content.editor.text);
        lineEdit->setPlaceholderText(
            content.editor.placeholderText);
        if (content.editor.minimumWidth > 0) {
            lineEdit->setMinimumWidth(
                content.editor.minimumWidth);
        }
        connect(
            lineEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString& text) {
                currentContent.editor.text = text;
            });
        layout->addWidget(lineEdit);
        auto* messageLabel = new QLabel(this);
        editMessageControl = messageLabel;
        messageLabel->setObjectName(
            QStringLiteral("peekEditableMessage"));
        messageLabel->setTextFormat(Qt::PlainText);
        messageLabel->setWordWrap(true);
        messageLabel->hide();
        layout->addWidget(messageLabel);
    }
    if (!content.actions.isEmpty()) {
        auto* actionHost = new QWidget(this);
        actionHost->setObjectName(
            QStringLiteral("peekActionBar"));
        auto* actionLayout = new QHBoxLayout(actionHost);
        actionLayout->setContentsMargins(0, 4, 0, 0);
        actionLayout->setSpacing(6);
        actionLayout->addStretch(1);
        for (const PeekContentAction& action : content.actions) {
            if (action.id.isEmpty() || action.label.isEmpty())
                continue;
            auto* button = new QPushButton(
                action.label, actionHost);
            button->setObjectName(
                QStringLiteral("peekAction.%1").arg(action.id));
            button->setProperty("peekActionId", action.id);
            button->setProperty(
                "peekActionRole",
                static_cast<int>(action.role));
            button->setEnabled(action.enabled);
            button->setDefault(action.defaultAction);
            button->setAutoDefault(false);
            button->setFocusPolicy(Qt::NoFocus);
            if (action.role
                == PeekContentActionRole::Destructive) {
                button->setStyleSheet(
                    QStringLiteral("color: palette(highlight);"));
            }
            connect(
                button,
                &QPushButton::clicked,
                this,
                [this, action]() {
                    emit actionTriggered(action.id);
                });
            actionLayout->addWidget(button);
        }
        layout->addWidget(actionHost);
    }

    QSize maximum(
        qMax(1, content.maximumSize.width()),
        qMax(1, content.maximumSize.height()));
    if (parentWidget()) {
        maximum.setWidth(
            qMin(maximum.width(),
                 qMax(1, parentWidget()->width() - 8)));
        maximum.setHeight(
            qMin(maximum.height(),
                 qMax(1, parentWidget()->height() - 8)));
    }
    setMaximumSize(maximum);
    if (content.symbolCard) {
        resizeSymbolCard();
    } else {
        adjustSize();
        resize(qMin(width(), maximum.width()), qMin(height(), maximum.height()));
    }
    moveNear(globalAnchorRect);
    show();
    // Newly inserted child layouts acquire their real size hints when shown.
    if (content.symbolCard) resizeSymbolCard();
    raise();
}

void EditorHoverPopup::applyAppearance()
{
    const bool card = currentContent.symbolCard;
    setProperty("symbolInspector", card);
    auto* outer = qobject_cast<QVBoxLayout*>(QFrame::layout());
    auto* header = qobject_cast<QHBoxLayout*>(headerHost->layout());
    outer->setContentsMargins(card ? 1 : 10, card ? 1 : 8, card ? 1 : 10, card ? 1 : 8);
    outer->setSpacing(card ? 0 : 4);
    header->setContentsMargins(card ? 18 : 0, card ? 16 : 0, card ? 14 : 0, card ? 14 : 0);
    header->setSpacing(card ? 12 : 6);
    symbolIcon->setVisible(card);
    categoryLabel->setVisible(card && !currentContent.category.isEmpty());
    titleLabel->setWordWrap(card);
    titleLabel->setSizePolicy(card ? QSizePolicy::Ignored : QSizePolicy::Preferred, QSizePolicy::Preferred);
    titleLabel->setToolTip(currentContent.title);
    closeButton->setFixedSize(card ? QSize(28, 28) : QSize(24, 24));
    if (!card) {
        setStyleSheet(defaultStyleSheet);
        setGraphicsEffect(nullptr);
        closeButton->setIcon(QIcon());
        closeButton->setText(QStringLiteral("\u00d7"));
        return;
    }

    setFont(UiTypography::font());
    QFont symbolFont = contentFont;
    symbolFont.setPixelSize(15);
    symbolFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(symbolFont);
    UiTypography::apply(categoryLabel, UiTypography::Role::Metadata);
    categoryLabel->setText(currentContent.category);
    closeButton->setText(QString());
    closeButton->setIcon(RoundedIcons::icon(RoundedIcons::Close));
    closeButton->setIconSize(QSize(14, 14));
    const auto& theme = ApplicationThemeManager::instance().theme();
    const bool dark = isDarkTheme(ApplicationThemeManager::instance().mode());
    const QColor accent = currentContent.portAccent
        ? QColor(dark ? "#89d5ca" : "#287c76")
        : QColor(dark ? "#c6b5f3" : "#7764b4");
    auto tint = [](const QColor& base, const QColor& color, double amount) {
        return QColor::fromRgbF(base.redF() * (1-amount) + color.redF() * amount,
                                base.greenF() * (1-amount) + color.greenF() * amount,
                                base.blueF() * (1-amount) + color.blueF() * amount);
    };
    const QColor surface = theme.panelBackground;
    const QColor headerColor = tint(surface, accent, dark ? .09 : .055);
    const QColor border = InsightVisualStyle::subtleBorder(surface, theme.textPrimary);
    setStyleSheet(QStringLiteral(
        "QFrame#editorHoverPopup {background:%1; border:1px solid %2; border-radius:14px;}"
        "QWidget#peekHeader {background:%3; border-top-left-radius:13px; border-top-right-radius:13px;}"
        "QLabel {background:transparent; border:0; color:%4;}"
        "QLabel#peekCategory {color:%5; background:%6; border-radius:9px; padding:2px 9px;}"
        "QLabel#peekFieldLabel, QLabel#peekDetailRow {color:%7;}"
        "QLabel#peekNotice {color:%8; padding:6px 0;}"
        "QScrollArea#peekSymbolScroll, QWidget#peekSymbolBody, QWidget#peekDetails {background:%1; border:0;}"
        "QPlainTextEdit#peekLongDetail {background:%1; color:%7; border:0;}"
        "QWidget#peekFooter {border-top:1px solid %2; background:transparent;}"
        "QToolButton {background:transparent; border:0; border-radius:6px; padding:4px; color:%7;}"
        "QToolButton:hover {background:%6; color:%4;}"
        "QToolButton#peekSourceLink {color:%5; text-decoration:underline;}"
        "QToolButton:disabled {color:%7;}"
    ).arg(surface.name(), border.name(), headerColor.name(), theme.textPrimary.name(),
          accent.name(), tint(surface, accent, dark ? .18 : .12).name(),
          theme.textSecondary.name(), theme.warning.name()));
    QPixmap icon = RoundedIcons::icon(currentContent.portAccent ? RoundedIcons::Right : RoundedIcons::Settings).pixmap(24, 24);
    QPainter painter(&icon);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(icon.rect(), accent);
    painter.end();
    symbolIcon->setPixmap(icon);
    if (!graphicsEffect()) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setOffset(0, 4);
        shadow->setColor(QColor(20, 25, 40, dark ? 80 : 30));
        setGraphicsEffect(shadow);
    }
}

void EditorHoverPopup::buildSymbolCard()
{
    auto* scroll = new QScrollArea(this);
    symbolScroll = scroll;
    scroll->setObjectName(QStringLiteral("peekSymbolScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    auto* body = new QWidget;
    symbolBody = body;
    body->setObjectName(QStringLiteral("peekSymbolBody"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(18, 12, 18, 12);
    bodyLayout->setSpacing(10);
    auto* details = new QWidget(body);
    details->setObjectName(QStringLiteral("peekDetails"));
    auto* detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(0, 10, 0, 0);
    detailLayout->setSpacing(8);
    QFont codeFont = contentFont;
    codeFont.setPixelSize(13);
    codeFont.setWeight(QFont::Normal);
    for (const auto& row : currentContent.rows) {
        if (!row.detail && !row.fieldLabel.isEmpty()) {
            auto* field = new QWidget(body);
            auto* fieldLayout = new QHBoxLayout(field);
            fieldLayout->setContentsMargins(0, 0, 0, 0);
            fieldLayout->setSpacing(16);
            auto* label = new QLabel(row.fieldLabel, field);
            label->setObjectName(QStringLiteral("peekFieldLabel"));
            label->setTextFormat(Qt::PlainText);
            label->setFont(UiTypography::font());
            fieldLayout->addWidget(label, 0, Qt::AlignTop);
            auto* value = new QLabel(row.fieldValue, field);
            value->setObjectName(QStringLiteral("peekFieldValue"));
            value->setAccessibleName(row.text);
            value->setProperty("peekField", row.fieldLabel);
            value->setTextFormat(Qt::PlainText);
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setAlignment(Qt::AlignRight | Qt::AlignTop);
            value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            QFont valueFont = codeFont;
            if (row.prominent) valueFont.setPixelSize(20);
            value->setFont(valueFont);
            fieldLayout->addWidget(value, 1);
            bodyLayout->addWidget(field);
        } else if (row.detail && (row.text.size() > 180 || row.text.count(QLatin1Char('\n')) > 3)) {
            auto* text = new QPlainTextEdit(row.text, details);
            text->setObjectName(QStringLiteral("peekLongDetail"));
            text->setReadOnly(true);
            text->setFont(codeFont);
            text->setFrameShape(QFrame::NoFrame);
            text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            text->setFixedHeight(110);
            detailLayout->addWidget(text);
        } else {
            auto* label = new QLabel(row.text, row.detail ? details : body);
            label->setObjectName(row.detail ? QStringLiteral("peekDetailRow") : QStringLiteral("peekNotice"));
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setFont(row.role == PeekContentRowRole::Code ? codeFont : UiTypography::font());
            (row.detail ? detailLayout : bodyLayout)->addWidget(label);
        }
    }
    const bool hasDetails = detailLayout->count() > 0;
    bodyLayout->addWidget(details);
    details->hide();
    scroll->setWidget(body);
    layout->addWidget(scroll);
    auto* footer = new QWidget(this);
    symbolFooter = footer;
    footer->setObjectName(QStringLiteral("peekFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 9, 14, 10);
    footerLayout->setSpacing(8);
    if (hasNavigableTarget()) {
        auto* source = new QToolButton(footer);
        source->setObjectName(QStringLiteral("peekSourceLink"));
        source->setText(QStringLiteral("%1:%2").arg(QFileInfo(targetFile).fileName()).arg(targetLine));
        source->setToolTip(QStringLiteral("%1:%2 — Go to definition").arg(targetFile).arg(targetLine));
        source->setAccessibleName(source->toolTip());
        source->setIcon(RoundedIcons::icon(RoundedIcons::File));
        source->setIconSize(QSize(16, 16));
        source->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        source->setFont(UiTypography::font(UiTypography::Role::Metadata));
        source->setFocusPolicy(Qt::NoFocus);
        source->setCursor(Qt::PointingHandCursor);
        source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        source->setEnabled(bool(navigationHandler));
        connect(source, &QToolButton::clicked, this, [this]() {
            // Copy the callback and target: navigation may close or destroy this popup.
            auto navigate = navigationHandler;
            const auto target = currentContent.navigationTarget;
            if (navigate && target.isValid()) navigate(target.fileName, target.line, target.column);
        });
        footerLayout->addWidget(source, 1);
    } else {
        footerLayout->addStretch(1);
    }
    if (hasDetails) {
        auto* toggle = new QToolButton(footer);
        toggle->setObjectName(QStringLiteral("peekDetailsToggle"));
        toggle->setText(QStringLiteral("More details"));
        toggle->setCheckable(true);
        toggle->setIcon(RoundedIcons::icon(RoundedIcons::Down));
        toggle->setIconSize(QSize(12, 12));
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setFont(UiTypography::font(UiTypography::Role::Metadata));
        toggle->setFocusPolicy(Qt::NoFocus);
        toggle->setCursor(Qt::PointingHandCursor);
        connect(toggle, &QToolButton::toggled, this, [this, details, toggle](bool expanded) {
            details->setVisible(expanded);
            toggle->setText(expanded ? QStringLiteral("Less details") : QStringLiteral("More details"));
            toggle->setIcon(RoundedIcons::icon(expanded ? RoundedIcons::Up : RoundedIcons::Down));
            resizeSymbolCard();
        });
        footerLayout->addWidget(toggle);
    }
    layout->addWidget(footer);
    body->show();
    scroll->show();
    footer->show();
}

void EditorHoverPopup::resizeSymbolCard()
{
    if (!symbolScroll || !symbolBody || !symbolFooter) return;
    const int cardWidth = qMin(460, maximumWidth());
    setMinimumWidth(0);
    resize(cardWidth, height());
    // Reserve space for a vertical scrollbar before measuring wrapped rows.
    const int bodyWidth = qMax(1, cardWidth - 2 - style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    auto* bodyLayout = symbolBody->layout();
    bodyLayout->invalidate();
    const int bodyHeight = qMax(bodyLayout->minimumSize().height(), bodyLayout->totalHeightForWidth(bodyWidth));
    const int headerHeight = headerHost->layout()->totalHeightForWidth(cardWidth - 2);
    const int footerHeight = symbolFooter->sizeHint().height();
    const int available = qMax(1, maximumHeight() - qMax(0, headerHeight) - footerHeight - 2);
    symbolScroll->setFixedHeight(qBound(1, bodyHeight, available));
    QFrame::layout()->invalidate();
    QFrame::layout()->activate();
    resize(cardWidth, qMin(maximumHeight(), qMax(0, headerHeight) + footerHeight + symbolScroll->height() + 2));
    moveNear(anchorRect);
}

void EditorHoverPopup::showHover(const SymbolHoverReport& report,
                                 const QPoint& globalPosition,
                                 const QFont& editorFont)
{
    PeekContentModel content;
    content.kind =
        report.parameterLike || report.enumMember || report.port
        ? PeekContentKind::EffectiveValue
        : PeekContentKind::DeclarationPreview;
    content.title = report.symbolName;
    content.maximumSize = QSize(520, 480);
    content.symbolCard = true;
    content.category = report.displayKind.trimmed();
    if (!content.category.isEmpty()) content.category[0] = content.category[0].toUpper();
    content.portAccent = report.port;
    content.navigationTarget = {report.definitionFile, report.definitionLine, 1};

    auto addRow = [&content](const QString& text,
                             PeekContentRowRole role =
                                 PeekContentRowRole::Body,
                             bool wrap = false) {
        if (!text.isEmpty())
            content.rows.append({text, role, wrap});
    };
    if (!report.unavailableReason.isEmpty()) {
        addRow(report.unavailableReason, PeekContentRowRole::Warning, true);
    } else {
        auto addField = [&](const QString& label,
                            const QString& value,
                            bool wrap = false) {
            if (value.isEmpty())
                return;
            PeekContentRow row{QStringLiteral("%1: %2").arg(label, value),
                               PeekContentRowRole::Body, wrap};
            row.detail = true;
            if (label == effectiveValueLabel(report) || label == QStringLiteral("type")
                || label == QStringLiteral("resolved type") || label == QStringLiteral("signedness")
                || label == QStringLiteral("enum")) {
                row.detail = false;
                row.fieldValue = value;
                row.fieldLabel = label;
                row.fieldLabel[0] = row.fieldLabel[0].toUpper();
                if (label == effectiveValueLabel(report)) {
                    row.fieldLabel = report.defaultEvaluation && !report.instanceBound
                        ? QStringLiteral("Default value") : QStringLiteral("Value");
                    row.prominent = true;
                } else if (label == QStringLiteral("resolved type")) {
                    row.fieldLabel = QStringLiteral("Type");
                }
            }
            content.rows.append(row);
        };

        addField(QStringLiteral("owner"), report.ownerName);

        addField(QStringLiteral("declaration"),
                 report.declarationText,
                 true);

        const bool staleEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Stale;
        if (staleEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addRow(
                QStringLiteral(
                    "effective value: stale (waiting for the current document revision)"),
                PeekContentRowRole::Warning, true);
        }
        const bool unavailableEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Unavailable
            || report.effectiveValueStatus == EffectiveValueStatus::Error;
        if (unavailableEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addRow(QStringLiteral("effective value: unavailable"),
                   PeekContentRowRole::Warning, true);
        }

        if (report.parameterLike) {
            if (hasCurrentEffectiveValue(report)) {
                addField(effectiveValueLabel(report), report.valueText, true);
                if (report.defaultEvaluation && !report.instanceBound)
                    addRow(QStringLiteral("Default evaluation · no instance bound"), PeekContentRowRole::Muted, true);
                addField(QStringLiteral("value source"),
                         report.valueSource);
                addField(QStringLiteral("instance path"),
                         report.instancePath);
                addField(QStringLiteral("type"),
                         report.resolvedTypeText);
                addField(QStringLiteral("bit width"),
                         report.bitWidthText);
            }
            addField(QStringLiteral("expression"),
                     report.expressionText,
                     true);
        } else if (report.enumMember) {
            if (hasCurrentEffectiveValue(report)) {
                addField(effectiveValueLabel(report), report.valueText, true);
                if (report.defaultEvaluation && !report.instanceBound)
                    addRow(QStringLiteral("Default evaluation · no instance bound"), PeekContentRowRole::Muted, true);
                addField(QStringLiteral("enum"), report.enumTypeName);
                addField(QStringLiteral("underlying bit width"),
                         report.enumUnderlyingBitWidthText);
                addField(QStringLiteral("value source"),
                         report.valueSource);
                addField(QStringLiteral("instance path"),
                         report.instancePath);
            }
        } else if (report.port) {
            if (hasCurrentEffectiveValue(report)) {
                addField(QStringLiteral("resolved type"),
                         report.resolvedTypeText);
                addField(QStringLiteral("packed dimensions"),
                         report.packedDimensionsText);
                addField(QStringLiteral("unpacked dimensions"),
                         report.unpackedDimensionsText);
                addField(QStringLiteral("bit width"),
                         report.bitWidthText);
                addField(QStringLiteral("signedness"),
                         report.signednessText);
                addField(QStringLiteral("interface"),
                         report.interfaceName);
                addField(QStringLiteral("modport"), report.modportName);
                addField(QStringLiteral("instance path"),
                         report.instancePath);
            }
        } else if (!report.typeText.isEmpty()) {
            addField(QStringLiteral("type"), report.typeText);
        }

        if (!report.evaluationFailureReason.isEmpty()
            && !staleEffectiveValue) {
            addRow(QStringLiteral("evaluation failed: %1")
                       .arg(report.evaluationFailureReason),
                   PeekContentRowRole::Warning);
        }

        if (!report.macroSignatureText.isEmpty()) {
            addRow(QStringLiteral("macro: %1")
                       .arg(report.macroSignatureText),
                   PeekContentRowRole::Code, true);
        }
        if (!report.macroBodyText.isEmpty()) {
            PeekContentRow body{QStringLiteral("body: %1").arg(report.macroBodyText), PeekContentRowRole::Code, true};
            body.detail = true;
            content.rows.append(body);
        }
    }

    showContent(content,
                QRect(globalPosition, QSize(1, 1)),
                editorFont);
}

void EditorHoverPopup::showPreview(const DefinitionPreviewReport& report,
                                   const QPoint& globalPosition,
                                   const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::DefinitionPreview;
    content.maximumSize = QSize(760, 460);
    content.navigationTarget = {
        report.targetFile,
        report.targetLine,
        report.targetColumn};
    const QString title = report.displayKind.isEmpty()
        ? report.symbolName
        : QStringLiteral("%1  %2").arg(report.symbolName, report.displayKind);
    content.title = title;

    const QString location =
        locationText(report.targetFile, report.targetLine);
    if (!location.isEmpty())
        content.rows.append(
            {location, PeekContentRowRole::Muted, false});

    if (report.available) {
        for (int i = 0; i < report.codeLines.size(); ++i) {
            const int lineNumber = report.firstLineNumber + i;
            const QString line =
                QStringLiteral("%1  %2")
                    .arg(lineNumber, 4)
                    .arg(report.codeLines.at(i));
            const bool highlighted = lineNumber == report.highlightedLine;
            content.rows.append(
                {line,
                 highlighted
                     ? PeekContentRowRole::HighlightedCode
                     : PeekContentRowRole::Code,
                 false});
        }
    } else {
        content.rows.append(
            {report.unavailableReason.isEmpty()
                 ? QStringLiteral("Definition preview unavailable.")
                 : report.unavailableReason,
             PeekContentRowRole::Muted,
             false});
    }

    if (content.navigationTarget.isValid()) {
        const ActionDescriptor* descriptor =
            findActionById(QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen));
        const ActionAliasDescriptor alias = descriptor
            ? descriptor->aliasForSurface(
                  ActionSurface::ContextMenu)
            : ActionAliasDescriptor();
        if (descriptor && !alias.label.isEmpty()) {
            PeekContentAction action;
            action.id = descriptor->id;
            action.label = alias.label;
            action.role = PeekContentActionRole::Secondary;
            content.actions.append(action);
        }
    }

    showContent(content,
                QRect(globalPosition, QSize(1, 1)),
                editorFont);
}

void EditorHoverPopup::showCodePreview(const CodePreviewReport& report,
                                       const QPoint& globalPosition,
                                       const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::CodePreview;
    content.maximumSize = QSize(780, 480);
    content.navigationTarget = {
        report.fileName,
        report.targetLine,
        report.targetColumn};
    content.title = report.title.isEmpty()
        ? QStringLiteral("Code preview")
        : report.title;
    if (!report.detail.isEmpty())
        content.rows.append(
            {report.detail, PeekContentRowRole::Muted, false});
    if (!report.locationDisplayName.isEmpty()) {
        content.rows.append(
            {report.preciseRange
                 ? QStringLiteral("evidence at %1")
                       .arg(report.locationDisplayName)
                 : QStringLiteral("near %1")
                       .arg(report.locationDisplayName),
             PeekContentRowRole::Muted,
             false});
    }

    if (report.available) {
        for (int i = 0; i < report.codeLines.size(); ++i) {
            const int lineNumber = report.firstLineNumber + i;
            const QString line =
                QStringLiteral("%1  %2")
                    .arg(lineNumber, 4)
                    .arg(report.codeLines.at(i));
            const bool highlighted = lineNumber == report.highlightedLine;
            content.rows.append(
                {line,
                 highlighted
                     ? PeekContentRowRole::HighlightedCode
                     : PeekContentRowRole::Code,
                 false});
            if (highlighted && !report.caretLine.isEmpty()) {
                content.rows.append(
                    {QStringLiteral("      %1").arg(report.caretLine),
                     PeekContentRowRole::Caret,
                     false});
            }
        }
    } else {
        content.rows.append(
            {report.unavailableReason.isEmpty()
                 ? QStringLiteral("Code preview unavailable.")
                 : report.unavailableReason,
             PeekContentRowRole::Muted,
             false});
    }

    showContent(content,
                QRect(globalPosition, QSize(1, 1)),
                editorFont);
}

void EditorHoverPopup::showNumericLiteral(const QString& displayText,
                                          const QPoint& globalPosition,
                                          const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::NumericRadix;
    content.title = QStringLiteral("Numeric value");
    const int lineCount = qMax(1, displayText.count(QLatin1Char('\n')) + 1);
    const int contentHeight =
        104 + lineCount * QFontMetrics(editorFont).lineSpacing();
    content.maximumSize = QSize(520, qBound(160, contentHeight, 320));
    content.rows.append(
        {displayText, PeekContentRowRole::Code, false});
    showContent(content,
                QRect(globalPosition, QSize(1, 1)),
                editorFont);
}

void EditorHoverPopup::showDiagnosticDetail(
    const QString& title,
    const QString& message,
    const QStringList& details,
    const QRect& globalAnchorRect,
    const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::DiagnosticDetail;
    content.title = title.isEmpty()
        ? QStringLiteral("Diagnostic")
        : title;
    content.maximumSize = QSize(720, 420);
    if (!message.isEmpty()) {
        content.rows.append(
            {message, PeekContentRowRole::Warning, true});
    }
    for (const QString& detail : details) {
        if (!detail.isEmpty()) {
            content.rows.append(
                {detail, PeekContentRowRole::Body, true});
        }
    }
    showContent(content, globalAnchorRect, editorFont);
}

void EditorHoverPopup::showDeclarationPreview(
    const QString& title,
    const QString& declaration,
    const PeekNavigationTarget& target,
    const QRect& globalAnchorRect,
    const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::DeclarationPreview;
    content.title = title.isEmpty()
        ? QStringLiteral("Declaration")
        : title;
    content.navigationTarget = target;
    content.maximumSize = QSize(760, 320);
    if (target.isValid()) {
        content.rows.append(
            {locationText(target.fileName, target.line),
             PeekContentRowRole::Muted,
             false});
    }
    if (!declaration.isEmpty()) {
        content.rows.append(
            {declaration, PeekContentRowRole::Code, true});
    }
    showContent(content, globalAnchorRect, editorFont);
}

void EditorHoverPopup::move(const QPoint& globalPosition)
{
    if (QWidget* host = parentWidget())
        QFrame::move(host->mapFromGlobal(globalPosition));
}

void EditorHoverPopup::move(int globalX, int globalY)
{
    move(QPoint(globalX, globalY));
}

void EditorHoverPopup::closePopup()
{
    const bool hadContent = isVisible() || !currentContent.isEmpty();
    QWidget* focused = QApplication::focusWidget();
    const bool restoreFocus =
        focused
        && (focused == this || isAncestorOf(focused));
    hide();
    targetFile.clear();
    targetLine = -1;
    targetColumn = -1;
    currentContent = PeekContentModel();
    titleLabel->clear();
    resetContent();
    if (restoreFocus && focusReturnWidget)
        focusReturnWidget->setFocus(Qt::OtherFocusReason);
    focusReturnWidget.clear();
    if (hadContent)
        emit closed();
}

bool EditorHoverPopup::hasNavigableTarget() const
{
    return !targetFile.isEmpty() && targetLine > 0;
}

QLineEdit* EditorHoverPopup::editableLineEdit() const
{
    return editControl.data();
}

void EditorHoverPopup::setEditableMessage(
    const QString& message,
    bool error)
{
    if (!editMessageControl)
        return;
    editMessageControl->setText(message);
    editMessageControl->setStyleSheet(
        error
            ? QStringLiteral("color: palette(highlight);")
            : QStringLiteral("color: palette(mid);"));
    editMessageControl->setVisible(!message.isEmpty());
}

const PeekContentModel& EditorHoverPopup::contentModel() const
{
    return currentContent;
}

bool EditorHoverPopup::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched)
    if (!isVisible() || !event) {
        return QFrame::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter) {
            for (const PeekContentAction& action :
                 currentContent.actions) {
                if (action.enabled && action.defaultAction) {
                    emit actionTriggered(action.id);
                    return true;
                }
            }
        }
    } else if (event->type() == QEvent::MouseButtonPress
               || event->type()
                      == QEvent::NonClientAreaMouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
        if (!globalRect.contains(mouseEvent->globalPosition().toPoint()))
            closePopup();
    } else if ((event->type() == QEvent::ApplicationDeactivate
                || event->type() == QEvent::WindowDeactivate)
               && transientPreview) {
        closePopup();
    }

    return QFrame::eventFilter(watched, event);
}

void EditorHoverPopup::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton
        && hasNavigableTarget()
        && navigationHandler) {
        navigationHandler(targetFile, targetLine, targetColumn);
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

void EditorHoverPopup::resetContent()
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            if (widget == editControl.data()) {
                widget->hide();
                widget->setParent(nullptr);
                widget->deleteLater();
                editControl.clear();
            } else {
                delete widget;
            }
        }
        delete item;
    }
    editMessageControl.clear();
}

QLabel* EditorHoverPopup::addLabel(const QString& text,
                                   PeekContentRowRole role,
                                   bool wordWrap)
{
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("peekContentRow"));
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setWordWrap(wordWrap);
    label->setFont(font());
    if (wordWrap)
        label->setMaximumWidth(720);

    switch (role) {
    case PeekContentRowRole::Muted:
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        break;
    case PeekContentRowRole::Code:
        label->setStyleSheet(QStringLiteral("padding: 1px 4px;"));
        break;
    case PeekContentRowRole::HighlightedCode:
        label->setStyleSheet(
            QStringLiteral("background: palette(alternate-base);"
                           "padding: 1px 4px;"));
        break;
    case PeekContentRowRole::Caret:
        label->setStyleSheet(
            QStringLiteral("color: palette(highlight); padding: 0 4px;"));
        break;
    case PeekContentRowRole::Warning:
        label->setStyleSheet(
            QStringLiteral("color: palette(highlight);"));
        break;
    case PeekContentRowRole::Body:
        break;
    }
    layout->addWidget(label);
    return label;
}

void EditorHoverPopup::moveNear(const QRect& globalAnchorRect)
{
    const QRect anchor = globalAnchorRect.normalized();
    QWidget* host = parentWidget();
    if (!host)
        return;

    const QRect localAnchor(
        host->mapFromGlobal(anchor.topLeft()),
        host->mapFromGlobal(anchor.bottomRight()));
    const QRect bounds = host->rect().adjusted(4, 4, -4, -4);
    QPoint target(localAnchor.right() + 14,
                  localAnchor.bottom() + 4);
    if (target.x() + width() > bounds.right())
        target.setX(localAnchor.left() - width() - 14);
    if (target.x() < bounds.left())
        target.setX(qMax(bounds.left(), bounds.right() - width() + 1));
    if (target.y() + height() > bounds.bottom()) {
        const int above = localAnchor.top() - height() - 4;
        target.setY(qMax(bounds.top(), above));
    }
    QFrame::move(target);
}

QString execPeekActionPrompt(
    QWidget* host,
    const PeekContentModel& content,
    const QRect& globalAnchorRect,
    const QFont& font)
{
    if (!host || content.actions.isEmpty())
        return QString();

    QEventLoop eventLoop;
    QPointer<EditorHoverPopup> peek =
        new EditorHoverPopup(host);
    peek->setProperty("peekActionPrompt", true);
    QString selectedAction;

    QObject::connect(
        peek.data(),
        &EditorHoverPopup::actionTriggered,
        &eventLoop,
        [&selectedAction, &peek](const QString& actionId) {
            selectedAction = actionId;
            if (peek)
                peek->closePopup();
        });
    QObject::connect(
        peek.data(),
        &EditorHoverPopup::closed,
        &eventLoop,
        &QEventLoop::quit);
    QObject::connect(
        host,
        &QObject::destroyed,
        &eventLoop,
        &QEventLoop::quit);

    peek->showContent(content, globalAnchorRect, font);
    if (peek && peek->isVisible())
        eventLoop.exec();

    if (peek)
        delete peek.data();
    return selectedAction;
}
