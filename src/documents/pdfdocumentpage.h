#pragma once
#include "zeroslackexport.h"
#include <QDateTime>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QWidget>

class WebEditorContainer;
class QFileSystemWatcher;
class QLabel;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QToolButton;
class QTimer;

class ZEROSLACK_API PdfDocumentPage final : public QWidget {
    Q_OBJECT
public:
    explicit PdfDocumentPage(QWidget* parent = nullptr, const QString& restoredViewId = {});
    ~PdfDocumentPage() override;
    static bool supportsFile(const QString& path);
    bool openFile(const QString& path, QString* error = nullptr);
    QString fileName() const { return path; }
    QString title() const;
    QString viewIdentity() const { return viewId; }
    bool ready() const { return initialized; }
    QString lastError() const { return failure; }
    int pageCount() const { return pages; }
    int lastRenderedPage() const { return renderedPage; }
    bool hasRenderedPage(int number) const { return renderedPages.contains(number); }
    int matchCount() const { return matches; }
    qint64 bytesRead() const { return readBytes; }
    QJsonObject viewState() const { return state; }
    void restoreViewState(const QJsonObject& value);
    void executeCommand(const QString& command);
    void setPage(int number);
    void setScale(const QString& scale);
    void findText(const QString& text, bool previous = false);
    void releaseView();
signals:
    void activated();
    void fileCommandRequested(const QString& command);
    void statusChanged();
    void viewStateChanged();
protected:
    void changeEvent(QEvent* event) override;
private:
    struct Range { QString request; qint64 position; qint64 end; };
    void receive(const QJsonObject& message);
    void initialize();
    void pumpRange();
    QByteArray readRange(qint64 begin, qint64 count);
    void invalidate(const QString& reason);
    bool sourceUnchanged() const;
    void command(QJsonObject message);
    WebEditorContainer* web = nullptr;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* rangeTimer = nullptr;
    QTimer* startupTimer = nullptr;
    QLabel* notice = nullptr;
    QLabel* pageTotal = nullptr;
    QToolButton* reload = nullptr;
    QSpinBox* pageNumber = nullptr;
    QComboBox* zoom = nullptr;
    QLineEdit* search = nullptr;
    QQueue<Range> ranges;
    QString path, physicalPath, viewId, session, failure;
    QDateTime modified;
    qint64 size = 0, readBytes = 0;
    QJsonObject state;
    int pages = 0, renderedPage = 0, matches = 0;
    QSet<int> renderedPages;
    bool hello = false, initialized = false, released = false, sourceAvailable = false;
};
