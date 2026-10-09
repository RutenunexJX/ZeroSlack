#include "wavepalette.h"

namespace simdock {
const QList<WavePalette>& wavePalettes()
{
    static const QList<WavePalette> palettes{
        {WaveTheme::QuestaDefault, QStringLiteral("default"), QStringLiteral("Questa default"),
         QStringLiteral("#000000"), QStringLiteral("#808080"), QStringLiteral("#ffffff"), QStringLiteral("#808080"), QStringLiteral("#4d4d4d"),
         QStringLiteral("#39ff14"), QStringLiteral("#39ff14"), QStringLiteral("#b3ffb3"), QStringLiteral("#39ff14"), QStringLiteral("#b3ffb3"), QStringLiteral("#ff0000"), QStringLiteral("#0000ff")},
        {WaveTheme::Mocha, QStringLiteral("mocha"), QStringLiteral("Catppuccin Mocha"),
         QStringLiteral("#1e1e2e"), QStringLiteral("#181825"), QStringLiteral("#cdd6f4"), QStringLiteral("#313244"), QStringLiteral("#45475a"),
         QStringLiteral("#a6adc8"), QStringLiteral("#fab387"), QStringLiteral("#89b4fa"), QStringLiteral("#a6e3a1"), QStringLiteral("#cba6f7"), QStringLiteral("#f38ba8"), QStringLiteral("#f9e2af")},
        {WaveTheme::Nord, QStringLiteral("nord"), QStringLiteral("Nord"),
         QStringLiteral("#2e3440"), QStringLiteral("#3b4252"), QStringLiteral("#eceff4"), QStringLiteral("#434c5e"), QStringLiteral("#4c566a"),
         QStringLiteral("#d8dee9"), QStringLiteral("#d08770"), QStringLiteral("#88c0d0"), QStringLiteral("#a3be8c"), QStringLiteral("#b48ead"), QStringLiteral("#bf616a"), QStringLiteral("#ebcb8b")},
        {WaveTheme::SolarizedLight, QStringLiteral("solarized-light"), QStringLiteral("Solarized Light"),
         QStringLiteral("#fdf6e3"), QStringLiteral("#eee8d5"), QStringLiteral("#586e75"), QStringLiteral("#eee8d5"), QStringLiteral("#eee8d5"),
         QStringLiteral("#839496"), QStringLiteral("#cb4b16"), QStringLiteral("#268bd2"), QStringLiteral("#859900"), QStringLiteral("#6c71c4"), QStringLiteral("#dc322f"), QStringLiteral("#b58900")}
    };
    return palettes;
}

const WavePalette& wavePalette(WaveTheme theme)
{
    for (const auto& palette : wavePalettes()) if (palette.theme == theme) return palette;
    return wavePalettes().first();
}

WaveTheme waveThemeFromId(const QString& id)
{
    for (const auto& palette : wavePalettes()) if (palette.id == id) return palette.theme;
    return WaveTheme::QuestaDefault;
}

QString waveAppearanceScript(WaveTheme theme)
{
    // Keep the original values inside the managed Questa process, so switching
    // back to its defaults also works when the GUI is reused for another run.
    const QString restore = QStringLiteral(R"tcl(
if {[info exists ::simdock::waveOriginalOptions]} {
    configure wave {*}$::simdock::waveOriginalOptions
    array set ::PrefWave $::simdock::waveOriginalPreferences
    array set ::LogicStyleTable $::simdock::waveOriginalLogic
}
)tcl");
    const auto& p = wavePalette(theme);
    QString body;
    if (p.theme == WaveTheme::QuestaDefault) {
        body = restore;
    } else {
        body = QStringLiteral(R"tcl(
set ::simdock::waveOptionMap {
    -background background -foreground foreground -wavebackground waveBackground
    -gridcolor gridColor -textcolor textColor -timecolor timeColor -vectorcolor vectorColor
    -selectbackground selectBackground -selectforeground selectForeground
    -waveselectcolor waveSelectColor -cursorcolor cursorColor -cursordeltacolor cursorDeltaColor
}
if {![info exists ::simdock::waveOriginalOptions]} {
    set ::simdock::waveOriginalOptions {}
    set ::simdock::waveOriginalPreferences {}
    foreach {option key} $::simdock::waveOptionMap {
        # A newly created Wave widget can still report placeholder colors until
        # its first repaint. PrefWave holds the session's actual default colors.
        lappend ::simdock::waveOriginalOptions $option $::PrefWave($key)
        lappend ::simdock::waveOriginalPreferences $key $::PrefWave($key)
    }
    set ::simdock::waveOriginalLogic [array get ::LogicStyleTable]
}
)tcl") + restore;
        body += QStringLiteral("set ::simdock::waveColors {background %1 foreground %2 waveBackground %3 gridColor %4 textColor %2 timeColor %2 vectorColor %5 selectBackground %6 selectForeground %2 waveSelectColor %6 cursorColor %7 cursorDeltaColor %2}\n")
            .arg(p.pane, p.text, p.background, p.grid, p.data, p.selection, p.highZ);
        body += QStringLiteral(R"tcl(
array set ::PrefWave $::simdock::waveColors
foreach {option key} $::simdock::waveOptionMap {
    configure wave $option [dict get $::simdock::waveColors $key]
}
)tcl");
        body += QStringLiteral("foreach {state color} {LOGIC_0 %1 LOGIC_1 %1 LOGIC_H %1 LOGIC_L %1 LOGIC_U %2 LOGIC_X %2 LOGIC_W %2 LOGIC_Z %3 LOGIC_DC %3} {\n"
            "    lset ::LogicStyleTable($state) 1 $color\n}\n").arg(p.handshake, p.unknown, p.highZ);
    }
    return QStringLiteral("if {[catch {\n%1} ::simdock::waveColorError]} {\n"
        "    puts \"SimDock warning: Waveform colors could not be applied: $::simdock::waveColorError\"\n"
        "} else {\n    puts \"SimDock waveform colors: %2\"\n}\n").arg(body, p.name);
}

QString waveSignalColorsScript(WaveTheme theme, const QString& signalList)
{
    const auto& p = wavePalette(theme);
    if (p.theme == WaveTheme::QuestaDefault) return {};
    // Names are classified conservatively. Other signals retain Questa's scalar
    // / vector distinction; this never changes radix, values, or simulation data.
    return QStringLiteral(R"tcl(
if {[catch {
    set ::simdock::waveColoredLeaves {}
    foreach signal %5 {
        set name [lindex [split $signal /] end]
        set leaf [string tolower $name]
        if {![regexp {^[a-z0-9_]+$} $leaf] || [dict exists $::simdock::waveColoredLeaves $name]} {continue}
        set color ""
        if {[regexp {(^|_)(clk|clock)([0-9]*|_[a-z0-9_]+)$} $leaf]} {
            set color %1
        } elseif {[regexp {(^|_)(rst|reset)(n|b|_[a-z0-9_]+)?$} $leaf]} {
            set color %2
        } elseif {[regexp {(^|_)(valid|ready|enable|en|req|ack)(_[a-z0-9_]+)?$} $leaf]} {
            set color %3
        } elseif {[regexp {(^|_)(state|fsm|mode)(_[a-z0-9_]+)?$} $leaf]} {
            set color %4
        }
        if {$color ne ""} {
            # Questa's property matcher cannot address bracketed generate paths
            # literally. All rows with this leaf name have the same category.
            property wave -color $color */$name
            dict set ::simdock::waveColoredLeaves $name 1
        }
    }
} ::simdock::waveColorError]} {
    puts "SimDock warning: Signal colors could not be applied: $::simdock::waveColorError"
}
)tcl").arg(p.clock, p.reset, p.handshake, p.state, signalList);
}
}
