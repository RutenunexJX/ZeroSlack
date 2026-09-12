#pragma once

#include <QString>

namespace LargeFileSemanticFixture {
struct Fixture {
    QString text;
    QString moduleName;
    int moduleProbe = -1;
    int editPosition = -1;
    int beginPosition = -1;
    int keywordCursor = -1;
    int unicodePosition = -1;
};

inline Fixture makeFixture()
{
    Fixture fixture;
    constexpr int moduleCount = 24000;
    constexpr int targetIndex = moduleCount / 2;
    fixture.text.reserve(3000000);
    for (int index = 0; index < moduleCount; ++index) {
        const QString moduleName =
            QStringLiteral("large_scope_%1")
                .arg(index, 5, 10, QLatin1Char('0'));
        if (index != targetIndex) {
            fixture.text +=
                QStringLiteral(
                    "module %1;\n"
                    "  logic enable;\n"
                    "  logic value;\n"
                    "  always_comb begin\n"
                    "    value = enable;\n"
                    "  end\n"
                    "endmodule\n")
                    .arg(moduleName);
            continue;
        }

        fixture.moduleName = moduleName;
        const int moduleStart = fixture.text.size();
        const QString feature =
            QStringLiteral(
                "module %1;\n"
                "  logic enable;\n"
                "  logic selected_signal;\n"
                "  always_comb begin\n"
                "    // UTF-16 boundary \u4e2d\u6587\U0001f600\n"
                "    selected_signal = enable;\n"
                "  end\n"
                "endmodule\n"
                "module %1_keyword_probe;\n"
                "  always_comb beg\n"
                "endmodule\n")
                .arg(moduleName);
        fixture.text += feature;
        fixture.moduleProbe =
            moduleStart
            + feature.indexOf(QStringLiteral("logic enable"));
        fixture.editPosition =
            moduleStart
            + feature.indexOf(
                QStringLiteral("selected_signal = enable;"));
        fixture.beginPosition =
            moduleStart
            + feature.indexOf(QStringLiteral("begin"));
        fixture.unicodePosition =
            moduleStart
            + feature.indexOf(
                QStringLiteral("\U0001f600"));
        const int keywordLine =
            feature.lastIndexOf(QStringLiteral("always_comb beg"));
        fixture.keywordCursor =
            moduleStart
            + feature.indexOf(QStringLiteral("beg"), keywordLine)
            + 3;
    }
    return fixture;
}
}
