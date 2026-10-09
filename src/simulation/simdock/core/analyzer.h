#pragma once
#include "model.h"
#include <QByteArray>
namespace simdock {
SourceFile analyzeSource(const QByteArray& content, const QString& relativePath);
QString syntaxTree(const QByteArray& content);
}
