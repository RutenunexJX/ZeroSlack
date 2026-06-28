#ifndef INCLUDEHEADERWORKFLOWTYPES_H
#define INCLUDEHEADERWORKFLOWTYPES_H

#include <QString>

struct IncludeNewHeaderChoice {
    QString text;
    QString description;
    QString previewText;
};

struct IncludeNewHeaderRequest {
    QString fileStem;
    QString extension;
    QString currentFileName;
    QString templateName;
    QString templateBody;
    QString cursorToken;
};

struct IncludeNewHeaderResult {
    bool success = false;
    QString includePath;
    QString filePath;
    QString errorMessage;
    int cursorPosition = 0;
};

#endif // INCLUDEHEADERWORKFLOWTYPES_H
