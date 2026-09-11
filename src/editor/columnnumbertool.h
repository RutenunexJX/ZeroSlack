#ifndef COLUMNNUMBERTOOL_H
#define COLUMNNUMBERTOOL_H

#include <QString>
#include <QStringList>

enum class ColumnNumberBase {
    Dec,
    Hex,
    Bin,
    Oct
};

enum class ColumnNumberStyle {
    Plain,
    CLike,
    SvUnsized,
    SvSized
};

enum class ColumnNumberDirection {
    Up,
    Down
};

enum class ColumnNumberPad {
    None,
    Space,
    Zero
};

enum class ColumnNumberReplaceMode {
    ReplaceSelection,
    InsertAtColumn
};

struct ColumnNumberConfig {
    qint64 start = 0;
    ColumnNumberBase base = ColumnNumberBase::Dec;
    ColumnNumberStyle style = ColumnNumberStyle::Plain;
    int bitWidth = 8;
    ColumnNumberDirection direction = ColumnNumberDirection::Up;
    qint64 step = 1;
    int repeat = 1;
    bool fixedDigitWidth = false;
    int digitWidth = 0;
    ColumnNumberPad pad = ColumnNumberPad::None;
    bool uppercaseHex = true;
    ColumnNumberReplaceMode replaceMode =
        ColumnNumberReplaceMode::ReplaceSelection;
};

ColumnNumberConfig defaultColumnNumberConfig();
ColumnNumberConfig inferColumnNumberConfig(const QString& text);
QString formatColumnNumber(qint64 value, const ColumnNumberConfig& config);
QStringList previewColumnNumbers(const ColumnNumberConfig& config,
                                 int lineCount);

#endif // COLUMNNUMBERTOOL_H
