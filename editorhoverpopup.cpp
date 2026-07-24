#include "editorhoverpopup.h"

#include "effectivevalueservice.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
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

EditorHoverPopup::EditorHoverPopup(QWidget* parent,
                                   PlacementMode newPlacementMode)
    : QFrame(parent,
             newPlacementMode == PlacementMode::TopLevelTool
                 ? (Qt::ToolTip
                    | Qt::FramelessWindowHint
                    | Qt::BypassWindowManagerHint)
                 : Qt::Widget)
    , placementMode(newPlacementMode)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setObjectName(QStringLiteral("editorHoverPopup"));
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame#editorHoverPopup {"
        "background: palette(base);"
        "border: 1px solid palette(mid);"
        "border-radius: 5px;"
        "}"
        "QLabel { color: palette(text); }"));

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    if (placementMode == PlacementMode::EmbeddedChild && qApp)
        qApp->installEventFilter(this);
}

EditorHoverPopup::~EditorHoverPopup()
{
    if (placementMode == PlacementMode::EmbeddedChild && qApp)
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

void EditorHoverPopup::showHover(const SymbolHoverReport& report,
                                 const QPoint& globalPosition,
                                 const QFont& editorFont)
{
    resetContent();
    setFont(editorFont);
    targetFile.clear();
    targetLine = -1;
    targetColumn = -1;

    QFont titleFont = editorFont;
    titleFont.setBold(true);
    addLabel(report.symbolName, QStringLiteral("font-weight:600;"), titleFont);
    if (!report.unavailableReason.isEmpty()) {
        addLabel(report.unavailableReason,
                 QStringLiteral("color: palette(mid);"),
                 editorFont);
    } else {
        auto addField = [&](const QString& label,
                            const QString& value,
                            const QFont& font = QFont(),
                            bool wrap = false) {
            if (value.isEmpty())
                return;
            QLabel* field = addLabel(
                QStringLiteral("%1: %2").arg(label, value),
                QString(),
                font.family().isEmpty() ? editorFont : font);
            if (wrap) {
                field->setMaximumWidth(720);
                field->setWordWrap(true);
            }
        };

        addField(QStringLiteral("kind"), report.displayKind);
        addField(QStringLiteral("owner"), report.ownerName);

        QFont codeFont = editorFont;
        addField(QStringLiteral("declaration"),
                 report.declarationText,
                 codeFont,
                 true);
        const QString location =
            locationText(report.definitionFile, report.definitionLine);
        if (!location.isEmpty())
            addLabel(location,
                     QStringLiteral("color: palette(mid);"),
                     editorFont);

        const bool staleEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Stale;
        if (staleEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addLabel(QStringLiteral(
                         "effective value: stale (waiting for the current document revision)"),
                     QStringLiteral("color: palette(mid);"),
                     editorFont);
        }
        const bool unavailableEffectiveValue =
            report.effectiveValueStatus == EffectiveValueStatus::Unavailable
            || report.effectiveValueStatus == EffectiveValueStatus::Error;
        if (unavailableEffectiveValue
            && (report.parameterLike || report.enumMember || report.port)) {
            addLabel(QStringLiteral("effective value: unavailable"),
                     QStringLiteral("color: palette(mid);"),
                     editorFont);
        }

        if (report.parameterLike) {
            if (hasCurrentEffectiveValue(report)) {
                addField(effectiveValueLabel(report),
                         report.valueText,
                         codeFont,
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
                     codeFont,
                     true);
        } else if (report.enumMember) {
            if (hasCurrentEffectiveValue(report)) {
                addField(effectiveValueLabel(report),
                         report.valueText,
                         codeFont,
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
                         report.packedDimensionsText,
                         codeFont);
                addField(QStringLiteral("unpacked dimensions"),
                         report.unpackedDimensionsText,
                         codeFont);
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
            addLabel(QStringLiteral("evaluation failed: %1")
                         .arg(report.evaluationFailureReason),
                     QStringLiteral("color: palette(mid);"),
                     editorFont);
        }

        if (!report.macroSignatureText.isEmpty()) {
            addLabel(QStringLiteral("macro: %1").arg(report.macroSignatureText),
                     QStringLiteral("padding-top: 2px;"),
                     codeFont);
        }
        if (!report.macroBodyText.isEmpty()) {
            addLabel(QStringLiteral("body: %1").arg(report.macroBodyText),
                     QStringLiteral("color: palette(text);"),
                     codeFont);
        }
    }

    adjustSize();
    resize(qMin(width(), 760), qMin(height(), 480));
    moveNear(globalPosition);
    show();
    raise();
}

void EditorHoverPopup::showPreview(const DefinitionPreviewReport& report,
                                   const QPoint& globalPosition,
                                   const QFont& editorFont)
{
    resetContent();
    setFont(editorFont);
    targetFile = report.targetFile;
    targetLine = report.targetLine;
    targetColumn = report.targetColumn;

    QFont titleFont = editorFont;
    titleFont.setBold(true);
    const QString title = report.displayKind.isEmpty()
        ? report.symbolName
        : QStringLiteral("%1  %2").arg(report.symbolName, report.displayKind);
    addLabel(title, QStringLiteral("font-weight:600;"), titleFont);

    const QString location =
        locationText(report.targetFile, report.targetLine);
    if (!location.isEmpty())
        addLabel(location, QStringLiteral("color: palette(mid);"), editorFont);

    QFont codeFont = editorFont;
    if (report.available) {
        for (int i = 0; i < report.codeLines.size(); ++i) {
            const int lineNumber = report.firstLineNumber + i;
            const QString line =
                QStringLiteral("%1  %2")
                    .arg(lineNumber, 4)
                    .arg(report.codeLines.at(i));
            const bool highlighted = lineNumber == report.highlightedLine;
            addLabel(line,
                     highlighted
                         ? QStringLiteral("background: rgba(97,175,239,0.18);"
                                          "padding: 1px 4px;")
                         : QStringLiteral("padding: 1px 4px;"),
                     codeFont);
        }
    } else {
        addLabel(report.unavailableReason.isEmpty()
                     ? QStringLiteral("Definition preview unavailable.")
                     : report.unavailableReason,
                 QStringLiteral("color: palette(mid);"),
                 editorFont);
    }

    adjustSize();
    resize(qMin(width(), 760), qMin(height(), 460));
    moveNear(globalPosition);
    show();
    raise();
}

void EditorHoverPopup::showCodePreview(const CodePreviewReport& report,
                                       const QPoint& globalPosition,
                                       const QFont& editorFont)
{
    resetContent();
    setFont(editorFont);
    targetFile = report.fileName;
    targetLine = report.targetLine;
    targetColumn = report.targetColumn;

    QFont titleFont = editorFont;
    titleFont.setBold(true);
    addLabel(report.title.isEmpty()
                 ? QStringLiteral("Code preview")
                 : report.title,
             QStringLiteral("font-weight:600;"),
             titleFont);
    if (!report.detail.isEmpty())
        addLabel(report.detail, QStringLiteral("color: palette(mid);"), editorFont);
    if (!report.locationDisplayName.isEmpty()) {
        addLabel(report.preciseRange
                     ? QStringLiteral("evidence at %1").arg(report.locationDisplayName)
                     : QStringLiteral("near %1").arg(report.locationDisplayName),
                 QStringLiteral("color: palette(mid);"),
                 editorFont);
    }

    QFont codeFont = editorFont;
    if (report.available) {
        for (int i = 0; i < report.codeLines.size(); ++i) {
            const int lineNumber = report.firstLineNumber + i;
            const QString line =
                QStringLiteral("%1  %2")
                    .arg(lineNumber, 4)
                    .arg(report.codeLines.at(i));
            const bool highlighted = lineNumber == report.highlightedLine;
            addLabel(line,
                     highlighted
                         ? QStringLiteral("background: rgba(64,156,255,0.18);"
                                          "padding: 1px 4px;")
                         : QStringLiteral("padding: 1px 4px;"),
                     codeFont);
            if (highlighted && !report.caretLine.isEmpty()) {
                addLabel(QStringLiteral("      %1").arg(report.caretLine),
                         QStringLiteral("color: #2563eb; padding: 0 4px;"),
                         codeFont);
            }
        }
    } else {
        addLabel(report.unavailableReason.isEmpty()
                     ? QStringLiteral("Code preview unavailable.")
                     : report.unavailableReason,
                 QStringLiteral("color: palette(mid);"),
                 editorFont);
    }

    adjustSize();
    resize(qMin(width(), 780), qMin(height(), 480));
    moveNear(globalPosition);
    show();
    raise();
}

void EditorHoverPopup::showNumericLiteral(const QString& displayText,
                                          const QPoint& globalPosition,
                                          const QFont& editorFont)
{
    resetContent();
    setFont(editorFont);
    targetFile.clear();
    targetLine = -1;
    targetColumn = -1;

    QFont codeFont = editorFont;
    addLabel(displayText, QStringLiteral("font-weight:600;"), codeFont);

    adjustSize();
    resize(qMin(width(), 520), qMin(height(), 120));
    moveNear(globalPosition);
    show();
    raise();
}

void EditorHoverPopup::closePopup()
{
    hide();
    targetFile.clear();
    targetLine = -1;
    targetColumn = -1;
}

bool EditorHoverPopup::hasNavigableTarget() const
{
    return !targetFile.isEmpty() && targetLine > 0;
}

bool EditorHoverPopup::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched)
    if (placementMode != PlacementMode::EmbeddedChild
        || !isVisible()
        || !event) {
        return QFrame::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
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
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
}

QLabel* EditorHoverPopup::addLabel(const QString& text,
                                   const QString& style,
                                   const QFont& font)
{
    auto* label = new QLabel(text, this);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setWordWrap(false);
    QFont labelFont = this->font();
    if (!font.family().isEmpty()) {
        labelFont.setWeight(font.weight());
        labelFont.setItalic(font.italic());
        labelFont.setUnderline(font.underline());
        labelFont.setStrikeOut(font.strikeOut());
    }
    label->setFont(labelFont);
    if (!style.isEmpty())
        label->setStyleSheet(style);
    layout->addWidget(label);
    return label;
}

void EditorHoverPopup::moveNear(const QPoint& globalPosition)
{
    if (placementMode == PlacementMode::EmbeddedChild && parentWidget()) {
        QWidget* host = parentWidget();
        QPoint target = host->mapFromGlobal(globalPosition) + QPoint(14, 20);
        const QRect bounds = host->rect();
        if (target.x() + width() > bounds.right())
            target.setX(qMax(bounds.left(), bounds.right() - width()));
        if (target.y() + height() > bounds.bottom()) {
            const int above = host->mapFromGlobal(globalPosition).y()
                - height() - 12;
            target.setY(qMax(bounds.top(), above));
        }
        move(target);
        return;
    }

    QPoint target = globalPosition + QPoint(14, 20);
    const QRect screen =
        QGuiApplication::screenAt(globalPosition)
            ? QGuiApplication::screenAt(globalPosition)->availableGeometry()
            : QApplication::primaryScreen()->availableGeometry();
    if (target.x() + width() > screen.right())
        target.setX(qMax(screen.left(), screen.right() - width()));
    if (target.y() + height() > screen.bottom())
        target.setY(qMax(screen.top(), globalPosition.y() - height() - 12));
    move(target);
}
