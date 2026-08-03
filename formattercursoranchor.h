#ifndef FORMATTERCURSORANCHOR_H
#define FORMATTERCURSORANCHOR_H

#include <QString>

#include <memory>

class QPlainTextEdit;
class TriviaPositionMap;

enum class FormatterPositionAffinity {
    Leading,
    Trailing
};

// The mapper owns all token/trivia knowledge. tokenIdentity is normally the
// Tree-sitter leaf kind plus spelling, tokenOrdinal identifies that leaf in
// source order, and tokenOffset is the UTF-16 offset inside the leaf.
struct FormatterLogicalPosition {
    QString tokenIdentity;
    int tokenOrdinal = -1;
    int tokenOffset = 0;
    int absoluteFallback = -1;
    FormatterPositionAffinity affinity =
        FormatterPositionAffinity::Leading;

    bool hasTokenIdentity() const;
};

class FormatterPositionMapper
{
public:
    virtual ~FormatterPositionMapper() = default;

    // The implementation must reject incompatible token streams by returning
    // -1 from restorePosition. FormatterCursorAnchor never infers semantic
    // compatibility or scans source text itself.
    virtual FormatterLogicalPosition capturePosition(
        int oldPosition,
        FormatterPositionAffinity affinity) const = 0;
    virtual int restorePosition(
        const FormatterLogicalPosition& position,
        int newDocumentLength) const = 0;
};

class FormatterTriviaPositionMapper final
    : public FormatterPositionMapper
{
public:
    FormatterTriviaPositionMapper(const QString& oldText,
                                  const QString& newText);
    ~FormatterTriviaPositionMapper() override;

    FormatterLogicalPosition capturePosition(
        int oldPosition,
        FormatterPositionAffinity affinity) const override;
    int restorePosition(
        const FormatterLogicalPosition& position,
        int newDocumentLength) const override;

    bool isCompatible() const;

private:
    std::unique_ptr<TriviaPositionMap> positionMap;
};

struct FormatterScrollState {
    int value = 0;
    int minimum = 0;
    int maximum = 0;
    double ratio = 0.0;
};

struct FormatterCursorAnchorState {
    FormatterLogicalPosition cursorPosition;
    FormatterLogicalPosition selectionAnchor;
    FormatterLogicalPosition topVisiblePosition;
    FormatterScrollState verticalScroll;
    FormatterScrollState horizontalScroll;
    int topVisiblePixelOffset = 0;
    int oldDocumentLength = 0;
    bool hasSelection = false;
    bool valid = false;
};

struct FormatterCursorRestoreResult {
    bool cursorMapped = false;
    bool selectionAnchorMapped = false;
    bool viewportMapped = false;
};

class FormatterCursorAnchor
{
public:
    bool capture(QPlainTextEdit* editor,
                 const FormatterPositionMapper& mapper);
    FormatterCursorRestoreResult restore(
        QPlainTextEdit* editor,
        const FormatterPositionMapper& mapper) const;

    bool isValid() const;
    const FormatterCursorAnchorState& state() const;

private:
    FormatterCursorAnchorState capturedState;
};

#endif // FORMATTERCURSORANCHOR_H
