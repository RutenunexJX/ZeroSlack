#ifndef WAVEFORMPREVIEWLOADER_H
#define WAVEFORMPREVIEWLOADER_H

#include "zeroslackexport.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstddef>
#include <functional>
#include <memory>

class QLibrary;
class QWidget;

class ZEROSLACK_API WaveformPreviewLoader final : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSupportedAbiVersion = 1;
    static constexpr const char* kViewContract =
        "wave-workbench.waveform-view/v1";
    static constexpr const char* kPreviewContract = "wave-preview/v1";

    WaveformPreviewLoader();
    ~WaveformPreviewLoader();

    QWidget* createView(const QString& libraryPath,
                        QWidget* parent,
                        QString* failureReason = nullptr);
    bool replacePreview(QWidget* view,
                        const QByteArray& payload,
                        QString* failureReason = nullptr) const;
    bool setPresentationState(QWidget* view,
                              const QString& state,
                              const QString& message = QString(),
                              QString* failureReason = nullptr) const;
    bool setTheme(QWidget* view,
                  const QString& theme,
                  QString* failureReason = nullptr) const;
    bool setCompact(QWidget* view,
                    bool compact,
                    QString* failureReason = nullptr) const;
    bool fitAll(QWidget* view,
                QString* failureReason = nullptr) const;
    bool zoomIn(QWidget* view,
                QString* failureReason = nullptr) const;
    bool zoomOut(QWidget* view,
                 QString* failureReason = nullptr) const;
    void setSourceNavigationHandler(
        std::function<void(const QString&, int, int,
                           const QString&, const QString&)> handler);

    QString loadedLibraryPath() const;

private slots:
    void handleSourceNavigationRequested(const QString& sourceFile,
                                         int sourceLine,
                                         int sourceColumn,
                                         const QString& semanticId,
                                         const QString& laneId);

private:
    using AbiVersionFunction = int (*)();
    using CreateViewFunction = int (*)(
        QWidget*, QWidget**, char*, std::size_t);
    using SetPreviewFunction = int (*)(
        QWidget*, const char*, std::size_t, char*, std::size_t);

    bool loadLibrary(const QString& libraryPath,
                     QString* failureReason);
    bool validateView(QWidget* view,
                      QString* failureReason) const;
    bool invokeBoolean(QWidget* view,
                       const char* method,
                       const QString& value,
                       QString* failureReason) const;
    bool invokeVoid(QWidget* view,
                    const char* method,
                    QString* failureReason) const;

    std::unique_ptr<QLibrary> library;
    QString currentLibraryPath;
    AbiVersionFunction abiVersionFunction = nullptr;
    CreateViewFunction createViewFunction = nullptr;
    SetPreviewFunction setPreviewFunction = nullptr;
    std::function<void(const QString&, int, int,
                       const QString&, const QString&)>
        sourceNavigationHandler;
};

#endif // WAVEFORMPREVIEWLOADER_H
