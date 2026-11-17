#include "uicontrols.h"
#include "editorhoverpopup.h"

#include "actionregistry.h"
#include "effectivevalueservice.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"

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
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

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

class PeekTextRow final : public QWidget {
public:
    PeekTextRow(const QString& text, const QString& name, const QFont& font, QWidget* parent)
        : QWidget(parent), view(UiControls::readOnlyText(text, this))
    {
        view->setObjectName(name);
        view->setFont(font);
        view->setFrameShape(QFrame::NoFrame);
        view->setStyleSheet(QStringLiteral("QPlainTextEdit {background:transparent;border:0;padding:0;}"));
        view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        view->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->document()->setDocumentMargin(0);
        view->setFocusPolicy(Qt::ClickFocus);
        view->setMinimumSize(0,0);
        view->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        auto* row = new QVBoxLayout(this);
        row->setContentsMargins(0,0,0,0);
        row->setSizeConstraint(QLayout::SetNoConstraint);
        row->addWidget(view);
        QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    int heightForWidth(int width) const override
    {
        QTextDocument measure;
        measure.setDefaultFont(view->font());
        measure.setDocumentMargin(0);
        auto option = measure.defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        measure.setDefaultTextOption(option);
        measure.setPlainText(view->toPlainText());
        measure.setTextWidth(qMax(1,width));
        return qCeil(measure.size().height()) + 2;
    }
    QSize sizeHint() const override { return QSize(240,heightForWidth(240)); }
    QSize minimumSizeHint() const override { return QSize(0,view->fontMetrics().height()+2); }
private:
    QPlainTextEdit* view;
};

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
        "QLabel { color: palette(text); }")
        + QStringLiteral(
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
    titleLabel = UiControls::label(headerHost);
    titleLabel->setObjectName(QStringLiteral("peekTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerLayout->addWidget(titleLabel,1);
    closeButton = UiControls::toolButton(this);
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
    if (content.symbolCard || content.readOnlyText.contentSized) {
        titleLabel->hide();
        summaryText = new PeekTextRow(content.title,QStringLiteral("peekTitleText"),titleLabel->font(),headerHost);
        if (!content.symbolCard) summaryText->findChild<QPlainTextEdit*>()->setFocusPolicy(Qt::NoFocus);
        qobject_cast<QHBoxLayout*>(headerHost->layout())->insertWidget(0,summaryText,1);
    }
    if (content.symbolCard)
        buildSymbolCard();
    else
        for (const PeekContentRow& row : content.rows)
            addLabel(row.text, row.role, row.wordWrap);
    if (content.readOnlyText.enabled) {
        auto* textEdit = UiControls::readOnlyText(this);
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
        if (content.readOnlyText.contentSized) {
            definitionText = textEdit;
            textEdit->setMinimumSize(0,0);
            textEdit->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
            textEdit->setFocusPolicy(Qt::NoFocus);
            const auto block = textEdit->document()->findBlockByNumber(content.readOnlyText.highlightedLine);
            if (block.isValid()) {
                QTextEdit::ExtraSelection selection;
                selection.cursor = QTextCursor(block);
                selection.format.setBackground(palette().alternateBase());
                selection.format.setProperty(QTextFormat::FullWidthSelection,true);
                textEdit->setExtraSelections({selection});
                auto cursor = selection.cursor;
                cursor.setPosition(block.position()+qBound(0,content.readOnlyText.highlightedColumn,block.length()-1));
                textEdit->setTextCursor(cursor);
            }
        } else {
            textEdit->setMinimumSize(minimum);
        }
        layout->addWidget(textEdit);
    }
    if (content.editor.enabled) {
        auto* lineEdit = UiControls::lineEdit(this);
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
        auto* messageLabel = UiControls::label(this);
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
            auto* button = UiControls::pushButton(
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
            if (content.readOnlyText.contentSized) button->setMinimumSize(button->sizeHint());
            if (action.role
                == PeekContentActionRole::Destructive
                && !button->property("zeroslackElaControl").toBool()) {
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
    // New children added to an already visible popup must participate in this layout pass.
    for (int i=0;i<layout->count();++i)
        if (auto* widget=layout->itemAt(i)->widget()) widget->show();
    if (summaryText) summaryText->show();
    headerHost->show();
    QFrame::layout()->invalidate();
    if (content.symbolCard) {
        resizeSymbolCard();
    } else if (content.readOnlyText.contentSized) {
        resizeDefinitionPreview();
    } else {
        adjustSize();
        resize(qMin(width(), maximum.width()), qMin(height(), maximum.height()));
    }
    moveNear(globalAnchorRect);
    show();
    // Newly inserted child layouts acquire their real size hints when shown.
    if (content.symbolCard) resizeSymbolCard();
    if (content.readOnlyText.contentSized) resizeDefinitionPreview();
    raise();
}

void EditorHoverPopup::showEmbeddedContent(
    QWidget* body, const QString& title, const QRect& globalAnchorRect,
    const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::PinloomPreview;
    content.title = title;
    content.maximumSize = QSize(420, 400);
    showContent(content, globalAnchorRect, editorFont);
    auto* scroll = UiControls::scrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);
    layout->addWidget(scroll);
    resize(maximumWidth(), maximumHeight());
    moveNear(globalAnchorRect);
}

void EditorHoverPopup::applyAppearance()
{
    const bool card = currentContent.symbolCard;
    setProperty("symbolInspector", card);
    auto* outer = qobject_cast<QVBoxLayout*>(QFrame::layout());
    auto* header = qobject_cast<QHBoxLayout*>(headerHost->layout());
    outer->setContentsMargins(card ? 1 : 10, card ? 1 : 8, card ? 1 : 10, card ? 1 : 8);
    outer->setSpacing(card ? 0 : 4);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    titleLabel->setWordWrap(card);
    titleLabel->setTextInteractionFlags(card
        ? Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard : Qt::TextSelectableByMouse);
    titleLabel->setSizePolicy(card ? QSizePolicy::Ignored : QSizePolicy::Preferred, QSizePolicy::Preferred);
    titleLabel->setToolTip(QString());
    closeButton->setFixedSize(QSize(24,24));
    if (!card) {
        header->setAlignment(closeButton, Qt::Alignment());
        setStyleSheet(defaultStyleSheet);
        setGraphicsEffect(nullptr);
        closeButton->setIcon(QIcon());
        closeButton->setText(QStringLiteral("\u00d7"));
        return;
    }
    setFont(contentFont);
    QFont codeFont = contentFont;
    codeFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(codeFont);
    closeButton->setText(QString());
    closeButton->setIcon(RoundedIcons::icon(RoundedIcons::Close));
    closeButton->setIconSize(QSize(14,14));
    header->setAlignment(closeButton, Qt::AlignTop);
    const auto& theme = ApplicationThemeManager::instance().theme();
    const auto surface = theme.panelBackground;
    const auto border = InsightVisualStyle::subtleBorder(surface, theme.textPrimary);
    setStyleSheet(QStringLiteral(
        "QFrame#editorHoverPopup {background:%1;border:1px solid %2;border-radius:6px;}"
        "QWidget#peekHeader, QWidget#peekDetails {background:transparent;}"
        "QLabel, QPlainTextEdit {background:transparent;border:0;color:%3;}"
        "QPlainTextEdit#peekComment {color:%4;} QPlainTextEdit#peekNotice {color:%5;}"
        "QScrollArea#peekSymbolScroll, QWidget#peekSymbolBody {background:%1;border:0;}")
        .arg(surface.name(),border.name(),theme.textPrimary.name(),theme.textSecondary.name(),theme.warning.name()));
    setGraphicsEffect(nullptr);
}

void EditorHoverPopup::buildSymbolCard()
{
    auto* scroll = UiControls::scrollArea(this);
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
    bodyLayout->setContentsMargins(10,8,10,8);
    bodyLayout->setSpacing(6);
    // Summary and optional enum members share one scrolling layout.
    QFrame::layout()->removeWidget(headerHost);
    bodyLayout->addWidget(headerHost);
    const auto addText = [&](const PeekContentRow& row, QVBoxLayout* target) {
        const auto name = row.role == PeekContentRowRole::Warning ? QStringLiteral("peekNotice")
            : row.role == PeekContentRowRole::Muted ? QStringLiteral("peekComment")
            : row.detail ? QStringLiteral("peekDetailRow") : QStringLiteral("peekSummaryRow");
        target->addWidget(new PeekTextRow(row.text,name,contentFont,target->parentWidget()));
    };
    for (const auto& row : currentContent.rows)
        if (!row.detail) addText(row, bodyLayout);

    QStringList sections;
    for (const auto& row : currentContent.rows)
        if (row.detail && !row.detailSection.isEmpty() && !sections.contains(row.detailSection))
            sections.append(row.detailSection);
    for (const auto& section : sections) {
        auto* toggle = UiControls::toolButton(body);
        toggle->setObjectName(QStringLiteral("peekDetailsToggle"));
        toggle->setProperty("peekSection", section);
        toggle->setText(section);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setFont(contentFont);
        toggle->setFocusPolicy(Qt::NoFocus);
        toggle->setAutoRaise(true);
        toggle->setCursor(Qt::PointingHandCursor);
        bodyLayout->addWidget(toggle,0,Qt::AlignLeft);
        auto* details = new QWidget(body);
        details->setObjectName(QStringLiteral("peekDetails"));
        details->setProperty("peekSection", section);
        auto* detailLayout = new QVBoxLayout(details);
        detailLayout->setContentsMargins(0,0,0,0);
        detailLayout->setSpacing(6);
        for (const auto& row : currentContent.rows)
            if (row.detail && row.detailSection == section) addText(row, detailLayout);
        bodyLayout->addWidget(details);
        details->hide();
        connect(toggle, &QToolButton::clicked, this, [this, details, toggle] {
            const bool expanded = !details->isVisible();
            details->setVisible(expanded);
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            resizeSymbolCard();
        });
    }
    scroll->setWidget(body);
    layout->addWidget(scroll);
    headerHost->show(); body->show(); scroll->show();
}

void EditorHoverPopup::resizeSymbolCard()
{
    if (!symbolScroll || !symbolBody) return;
    if (parentWidget())
        setMaximumSize(currentContent.maximumSize.boundedTo(parentWidget()->size() - QSize(8,8)).expandedTo(QSize(1,1)));
    int desired = QFontMetrics(titleLabel->font()).horizontalAdvance(currentContent.title.left(80)) + 58;
    for (const auto& row : currentContent.rows)
        if (!row.detail) desired = qMax(desired, QFontMetrics(font()).horizontalAdvance(row.text.left(80)) + 24);
    const int cardWidth = qMin(maximumWidth(), qMax(240, desired));
    const auto measureBody = [&](int bodyWidth) {
        const int textWidth = qMax(1,bodyWidth-20);
        for (auto* widget : symbolBody->findChildren<QWidget*>()) {
            if (auto* row = dynamic_cast<PeekTextRow*>(widget))
                row->setFixedHeight(row->heightForWidth(qMax(1,textWidth-(row==summaryText ? closeButton->width()+8 : 0))));
        }
        headerHost->setFixedHeight(qMax(closeButton->height(),summaryText ? summaryText->height() : 0));
        auto* bodyLayout = symbolBody->layout();
        bodyLayout->invalidate();
        const int measured = bodyLayout->totalHeightForWidth(bodyWidth);
        return qMax(bodyLayout->minimumSize().height(), measured >= 0 ? measured : bodyLayout->sizeHint().height());
    };
    const int availableHeight = qMax(1,maximumHeight()-2);
    int bodyHeight = measureBody(qMax(1,cardWidth-2));
    if (bodyHeight > availableHeight)
        bodyHeight = measureBody(qMax(1,cardWidth-2-style()->pixelMetric(QStyle::PM_ScrollBarExtent)));
    symbolScroll->setFixedHeight(qMin(availableHeight,bodyHeight));
    resize(cardWidth,symbolScroll->height()+2);
    QFrame::layout()->activate();
    moveNear(anchorRect);
}

void EditorHoverPopup::resizeDefinitionPreview()
{
    if (!definitionText) return;
    if (parentWidget())
        setMaximumSize(currentContent.maximumSize.boundedTo(parentWidget()->size()-QSize(8,8)).expandedTo(QSize(1,1)));
    int width = 320;
    for (const auto& line : currentContent.readOnlyText.text.split(QLatin1Char('\n')))
        width = qMax(width,definitionText->fontMetrics().horizontalAdvance(line)+52);
    width = qMin(maximumWidth(),width);
    definitionText->setMinimumHeight(0);
    definitionText->setMaximumHeight(QWIDGETSIZE_MAX);
    const int innerWidth = qMax(1,width-20);
    headerHost->setFixedHeight(qMax(closeButton->height(),summaryText
        ? summaryText->heightForWidth(qMax(1,innerWidth-closeButton->width()-8)) : titleLabel->sizeHint().height()));
    if (summaryText) summaryText->setFixedHeight(headerHost->height());
    int controls = 16+headerHost->height()+4;
    for (int i=0;i<layout->count();++i) {
        const auto* item=layout->itemAt(i);
        if (item->widget()!=definitionText) {
            const auto* widget=item->widget();
            controls += widget && widget->hasHeightForWidth() ? widget->heightForWidth(innerWidth) : item->sizeHint().height();
        }
    }
    controls += qMax(0,layout->count()-1)*layout->spacing();
    const int wantedHeight = definitionText->document()->blockCount()*definitionText->fontMetrics().lineSpacing()
        + style()->pixelMetric(QStyle::PM_ScrollBarExtent)+12;
    definitionText->setFixedHeight(qMax(1,qMin(wantedHeight,maximumHeight()-controls)));
    resize(width,qMin(maximumHeight(),controls+definitionText->height()));
    QFrame::layout()->activate();
    definitionText->ensureCursorVisible();
    moveNear(anchorRect);
}

void EditorHoverPopup::showHover(const SymbolHoverReport& report,
                                 const QPoint& globalPosition,
                                 const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = report.parameterLike || report.enumMember || report.port
        ? PeekContentKind::EffectiveValue : PeekContentKind::DeclarationPreview;
    content.symbolCard = true;
    content.maximumSize = QSize(520,480);
    const auto addRow = [&](const QString& text, PeekContentRowRole role, const QString& section = QString()) {
        if (text.trimmed().isEmpty()) return;
        PeekContentRow row{text,role,true};
        row.detail = !section.isEmpty(); row.detailSection = section;
        content.rows.append(row);
    };
    if (!report.unavailableReason.isEmpty()) {
        content.title = QStringLiteral("Information unavailable");
        addRow(report.unavailableReason,PeekContentRowRole::Warning);
    } else if (!report.moduleName.isEmpty()) {
        content.title = report.moduleName;
    } else {
        const bool current = hasCurrentEffectiveValue(report);
        if (report.parameterLike || report.enumMember) {
            if (current && !report.valueText.isEmpty()) {
                const QString domain = !report.parameterLike ? QString()
                    : report.defaultEvaluation && !report.instanceBound
                        ? QStringLiteral("Default ") : QStringLiteral("Effective ");
                content.title = domain + report.valueText;
            }
            if (report.enumMember && current)
                addRow(report.enumTypeName,PeekContentRowRole::Code);
            if (report.enumMember && current && !report.enumMembers.isEmpty())
                addRow(report.enumMembers.join(QLatin1Char('\n')),PeekContentRowRole::Code,QStringLiteral("Enum members"));
        } else if (current) {
            QString type = report.resolvedTypeText.isEmpty() ? report.typeText : report.resolvedTypeText;
            type.replace(QStringLiteral("$["),QStringLiteral(" ["));
            if (!report.unpackedDimensionsText.isEmpty() && !type.contains(report.unpackedDimensionsText))
                type += QLatin1Char(' ') + report.unpackedDimensionsText;
            content.title = (report.direction + QLatin1Char(' ') + type).trimmed();
        }
        if (content.title.isEmpty()) {
            content.title = report.effectiveValueStatus == EffectiveValueStatus::Stale
                ? QStringLiteral("Analysis stale") : QStringLiteral("Information unavailable");
        }
        if (!current && report.macroSignatureText.isEmpty())
            addRow(report.effectiveValueStatus == EffectiveValueStatus::Stale
                ? QStringLiteral("Waiting for the current document revision.")
                : report.evaluationFailureReason.isEmpty() ? QStringLiteral("No current semantic result.")
                : report.evaluationFailureReason,PeekContentRowRole::Warning);
        if (current && !report.enumMember && !report.instanceBound)
            addRow(report.ownerName,PeekContentRowRole::Muted);
        if (current && report.parameterLike) {
            if (report.expressionText != report.valueText)
                addRow(report.expressionText,PeekContentRowRole::Code);
            if (report.instanceBound) {
                addRow(report.instancePath,PeekContentRowRole::Muted);
                QString source = report.valueSource;
                source.remove(QStringLiteral("slang elaborated "),Qt::CaseInsensitive);
                addRow(source,PeekContentRowRole::Muted);
            }
        }
        if (!report.macroSignatureText.isEmpty()) content.title = report.macroSignatureText;
        addRow(report.macroBodyText,PeekContentRowRole::Code);
    }
    if (!report.commentText.isEmpty()) addRow(report.commentText.section(QLatin1Char('\n'),0,0),PeekContentRowRole::Muted);
    showContent(content,QRect(globalPosition,QSize(1,1)),editorFont);
}

void EditorHoverPopup::showPreview(const DefinitionPreviewReport& report,
                                   const QPoint& globalPosition,
                                   const QFont& editorFont)
{
    PeekContentModel content;
    content.kind = PeekContentKind::DefinitionPreview;
    content.maximumSize = QSize(760, 460);
    if (report.available)
        content.navigationTarget = {report.targetFile, report.targetLine, report.targetColumn};
    const QString title = report.displayKind.isEmpty()
        ? report.symbolName
        : QStringLiteral("%1  %2").arg(report.symbolName, report.displayKind);
    content.title = title;
    if (report.sourceDescription == QStringLiteral("semantic snapshot"))
        content.rows.append({QStringLiteral("Showing last analyzed text."), PeekContentRowRole::Muted, false});

    const QString location =
        locationText(report.targetFile, report.targetLine);
    if (!location.isEmpty())
        content.rows.append(
            {location, PeekContentRowRole::Muted, false});

    if (report.available) {
        QStringList lines;
        for (int i = 0; i < report.codeLines.size(); ++i) {
            const int lineNumber = report.firstLineNumber + i;
            const QString line =
                QStringLiteral("%1  %2")
                    .arg(lineNumber, 4)
                    .arg(report.codeLines.at(i));
            lines.append(line);
        }
        content.readOnlyText.enabled = true;
        content.readOnlyText.contentSized = true;
        content.readOnlyText.objectName = QStringLiteral("peekDefinitionCode");
        content.readOnlyText.text = lines.join(QLatin1Char('\n'));
        content.readOnlyText.highlightedLine = report.highlightedLine-report.firstLineNumber;
        content.readOnlyText.highlightedColumn = qMax(0,report.targetColumn-1)+6;
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
    if (report.available)
        content.navigationTarget = {report.fileName, report.targetLine, report.targetColumn};
    content.title = report.title.isEmpty()
        ? QStringLiteral("Code preview")
        : report.title;
    if (report.sourceDescription == QStringLiteral("semantic snapshot"))
        content.rows.append({QStringLiteral("Showing last analyzed text."), PeekContentRowRole::Muted, false});
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
    if (!isVisible() || !event) {
        return QFrame::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
        }
        if (currentContent.symbolCard) {
            const auto* receiver = qobject_cast<QWidget*>(watched);
            const bool inside = receiver && (receiver == this || isAncestorOf(receiver));
            const bool editingKey = !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
                && (!keyEvent->text().isEmpty() || keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Delete);
            if (editingKey && receiver && parentWidget()
                && (inside || receiver == parentWidget() || parentWidget()->isAncestorOf(receiver))) {
                const QPointer<QWidget> editor = focusReturnWidget;
                closePopup();
                if (inside && editor) {
                    QApplication::sendEvent(editor, keyEvent);
                    return true;
                }
            }
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
        const QWidget* activePopup = QApplication::activePopupWidget();
        if (!globalRect.contains(mouseEvent->globalPosition().toPoint())
            && !(activePopup && isAncestorOf(activePopup)))
            closePopup();
    } else if (event->type() == QEvent::Resize && watched == parentWidget()) {
        if (currentContent.symbolCard) resizeSymbolCard();
        if (currentContent.readOnlyText.contentSized) resizeDefinitionPreview();
    } else if ((event->type() == QEvent::ApplicationDeactivate
                || event->type() == QEvent::WindowDeactivate)
               && transientPreview) {
        closePopup();
    }

    return QFrame::eventFilter(watched, event);
}

void EditorHoverPopup::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!currentContent.symbolCard && event->button() == Qt::LeftButton
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
    delete summaryText.data();
    summaryText.clear();
    definitionText.clear();
    headerHost->setMinimumHeight(0);
    headerHost->setMaximumHeight(QWIDGETSIZE_MAX);
    if (headerHost->parentWidget() != this) {
        headerHost->setParent(this);
        qobject_cast<QVBoxLayout*>(QFrame::layout())->insertWidget(0, headerHost);
        headerHost->show();
    }
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
    auto* label = UiControls::label(text, this);
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
