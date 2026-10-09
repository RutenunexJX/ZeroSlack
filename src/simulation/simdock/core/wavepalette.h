#pragma once
#include <QList>
#include <QString>

namespace simdock {
enum class WaveTheme { QuestaDefault, Mocha, Nord, SolarizedLight };

struct WavePalette {
    WaveTheme theme;
    QString id, name;
    QString background, pane, text, grid, selection;
    QString clock, reset, data, handshake, state, unknown, highZ;
};

const QList<WavePalette>& wavePalettes();
const WavePalette& wavePalette(WaveTheme theme);
WaveTheme waveThemeFromId(const QString& id);
QString waveAppearanceScript(WaveTheme theme);
QString waveSignalColorsScript(WaveTheme theme, const QString& signalList = QStringLiteral("[find signals -r /*]"));
}
