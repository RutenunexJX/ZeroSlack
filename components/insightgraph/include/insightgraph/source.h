#pragma once
#include <QString>

struct RtlInsightCodeLink {
    QString fileName;
    int line = 0;
    int column = 0;
    QString fileDisplayName;
    QString lineDisplayName;
};
