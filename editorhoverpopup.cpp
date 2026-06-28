#include "editorhoverpopup.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
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
}

EditorHoverPopup::EditorHoverPopup(QWidget* parent)
    : QFrame(parent,
             Qt::ToolTip
                 | Qt::FramelessWindowHint
                 | Qt::BypassWindowManagerHint)
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
}

void EditorHoverPopup::setNavigationHandler(NavigationHandler handler)
{
    navigationHandler = std::move(handler);
}

void EditorHoverPopup::showHover(const SymbolHoverReport& report,
                                 const QPoint& globalPosition,
                                 const QFont& editorFont)
{
    resetContent();
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
        QStringList meta;
        if (!report.displayKind.isEmpty())
            meta.append(report.displayKind);
        if (!report.sourceRole.isEmpty())
            meta.append(report.sourceRole);
        if (!report.ownerName.isEmpty())
            meta.append(QStringLiteral("owner: %1").arg(report.ownerName));
        if (!report.typeText.isEmpty())
            meta.append(QStringLiteral("type: %1").arg(report.typeText));
        if (!meta.isEmpty())
            addLabel(meta.join(QStringLiteral(" | ")), QString(), editorFont);
        const QString location =
            locationText(report.definitionFile, report.definitionLine);
        if (!location.isEmpty())
            addLabel(location,
                     QStringLiteral("color: palette(mid);"),
                     editorFont);
        QFont codeFont = editorFont;
        codeFont.setStyleHint(QFont::Monospace);
        codeFont.setFixedPitch(true);
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
    resize(qMin(width(), 520), qMin(height(), 220));
    moveNear(globalPosition);
    show();
}

void EditorHoverPopup::showPreview(const DefinitionPreviewReport& report,
                                   const QPoint& globalPosition,
                                   const QFont& editorFont)
{
    resetContent();
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
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setFixedPitch(true);
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
}

void EditorHoverPopup::showCodePreview(const CodePreviewReport& report,
                                       const QPoint& globalPosition,
                                       const QFont& editorFont)
{
    resetContent();
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
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setFixedPitch(true);
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
}

void EditorHoverPopup::showNumericLiteral(const QString& displayText,
                                          const QPoint& globalPosition,
                                          const QFont& editorFont)
{
    resetContent();
    targetFile.clear();
    targetLine = -1;
    targetColumn = -1;

    QFont codeFont = editorFont;
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setFixedPitch(true);
    addLabel(displayText, QStringLiteral("font-weight:600;"), codeFont);

    adjustSize();
    resize(qMin(width(), 520), qMin(height(), 120));
    moveNear(globalPosition);
    show();
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
    if (!font.family().isEmpty())
        label->setFont(font);
    if (!style.isEmpty())
        label->setStyleSheet(style);
    layout->addWidget(label);
    return label;
}

void EditorHoverPopup::moveNear(const QPoint& globalPosition)
{
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
