#ifndef SEMANTICSOURCERANGE_H
#define SEMANTICSOURCERANGE_H

#include <QString>

struct SemanticSourceRange {
    QString fileName;
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;

    bool isValid() const
    {
        return !fileName.isEmpty() && line > 0 && column > 0;
    }
};

#endif // SEMANTICSOURCERANGE_H
