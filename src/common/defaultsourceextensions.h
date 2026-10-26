#pragma once
#include <QStringList>

namespace SourceFiles {
inline QStringList defaultExtensions()
{
    return {QStringLiteral(".sv"), QStringLiteral(".svh"),
            QStringLiteral(".v"), QStringLiteral(".vh"),
            QStringLiteral(".vp"), QStringLiteral(".svp"), QStringLiteral(".svi")};
}
}
