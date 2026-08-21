#include "editorhoverpopup.h"

#include "actionregistry.h"
#include "effectivevalueservice.h"

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

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(10, 8, 10, 8);
    outerLayout->setSpacing(4);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    titleLabel = new QLabel(this);
    titleLabel->setObjectName(QStringLiteral("peekTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    headerLayout->addWidget(titleLabel, 1);

    closeButton = new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("peekCloseButton"));
    closeButton->setText(QStringLiteral("\u00d7"));
    closeButton->setToolTip(QStringLiteral("Close peek (Esc)"));
    closeButton->setAutoRaise(true);
    closeButton->setFocusPolicy(Qt::NoFocus);
    headerLayout->addWidget(closeButton);
    outerLayout->addLayout(headerLayout);

    layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    outerLayout->addLayout(layout);

    connect(closeButton,
            &QToolButton::clicked,
            this,
            &EditorHoverPopup::closePopup);

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
    currentContent = content;
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
    adjustSize();
    resize(qMin(width(), maximum.width()),
           qMin(height(), maximum.height()));
    moveNear(globalAnchorRect);
    show();
    raise();
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
    content.maximumSize = QSize(760, 480);

    auto addRow = [&content](const QString& text,
                             PeekContentRowRole role =
                                 PeekContentRowRole::Body,
                             bool wrap = false) {
        if (!text.isEmpty())
            content.rows.append({text, role, wrap});
    };
    if (!report.unavailableReason.isEmpty()) {
        addRow(report.unavailableReason, PeekContentRowRole::Muted);
    } else {
        auto addField = [&](const QString& label,
                            const QString& value,
                            bool wrap = false) {
            if (value.isEmpty())
                return;
            addRow(QStringLiteral("%1: %2").arg(label, value),
                   PeekContentRowRole::Body,
                   wrap);
        };

        addField(QStringLiteral("kind"), report.displayKind);
        addField(QStringLiteral("owner"), report.ownerName);

        addField(QStringLiteral("declaration"),
                 report.declarationText,
                 true);
        const QString location =
            locationText(report.definitionFile, report.definitionLine);
        if (!location.isEmpty())
            addRow(location, PeekContentRowRole::Muted);

        const bool staleEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Stale;
        if (staleEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addRow(
                QStringLiteral(
                    "effective value: stale (waiting for the current document revision)"),
                PeekContentRowRole::Muted);
        }
        const bool unavailableEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Unavailable
            || report.effectiveValueStatus == EffectiveValueStatus::Error;
        if (unavailableEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addRow(QStringLiteral("effective value: unavailable"),
                   PeekContentRowRole::Muted);
        }

        if (report.parameterLike) {
            if (hasCurrentEffectiveValue(report)) {
                addField(effectiveValueLabel(report),
                         report.valueText,
                         true);
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
                addField(effectiveValueLabel(report),
                         report.valueText,
                         true);
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
                   PeekContentRowRole::Code);
        }
        if (!report.macroBodyText.isEmpty()) {
            addRow(QStringLiteral("body: %1").arg(report.macroBodyText),
                   PeekContentRowRole::Code);
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
