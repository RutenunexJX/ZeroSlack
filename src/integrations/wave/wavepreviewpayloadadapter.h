#ifndef WAVEPREVIEWPAYLOADADAPTER_H
#define WAVEPREVIEWPAYLOADADAPTER_H

#include "wavepreviewservice.h"
#include "zeroslackexport.h"

#include <QByteArray>
#include <QString>

#include <QtGlobal>

struct ZEROSLACK_API WavePreviewPayloadBuildResult
{
    QByteArray payload;
    QString error;
    quint64 generation = 0;

    bool ok() const
    {
        return error.isEmpty() && !payload.isEmpty() && generation > 0;
    }
};

class ZEROSLACK_API WavePreviewPayloadAdapter
{
public:
    static constexpr quint64 kMaximumGeneration =
        9'007'199'254'740'991ULL;

    static quint64 nextGeneration();
    static WavePreviewPayloadBuildResult buildSymbolic(
        const WavePreviewReport& report,
        const QString& documentPath,
        const QString& workspaceRoot = QString(),
        quint64 generation = 0);
};

#endif // WAVEPREVIEWPAYLOADADAPTER_H
