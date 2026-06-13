#include "relationshipprogressdialog.h"

#include <QTextCursor>
#include <QTime>
#include <QtGlobal>

void RelationshipProgressDialog::onDetailsToggled(bool show)
{
    config.showDetails = show;
    detailsGroup->setVisible(show);
    detailsButton->setText(show ? "Hide Details" : "Show Details");

    if (show) {
        int newHeight = height() + 150;
        resize(width(), newHeight);
    } else {
        int newHeight = qMax(200, height() - 150);
        resize(width(), newHeight);
    }
}

void RelationshipProgressDialog::logProgress(const QString& message)
{
    if (!config.showDetails || !detailsText) return;

    QString timestamp = QTime::currentTime().toString("hh:mm:ss");
    QString logLine = QString("[%1] %2").arg(timestamp, message);

    detailsText->append(logLine);

    QTextCursor cursor = detailsText->textCursor();
    cursor.movePosition(QTextCursor::End);
    detailsText->setTextCursor(cursor);
}

QString RelationshipProgressDialog::formatTime(qint64 seconds)
{
    if (seconds < 60) {
        return QString("%1s").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    } else {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;
        return QString("%1h %2m %3s").arg(hours).arg(minutes).arg(secs);
    }
}

QString RelationshipProgressDialog::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024);
    } else {
        return QString("%1 MB").arg(QString::number((double)bytes / (1024 * 1024), 'f', 1));
    }
}

QString RelationshipProgressDialog::formatSpeed(double filesPerSecond)
{
    if (filesPerSecond < 1.0) {
        return QString("%1/min").arg(QString::number(filesPerSecond * 60, 'f', 1));
    } else {
        return QString("%1/s").arg(QString::number(filesPerSecond, 'f', 1));
    }
}
