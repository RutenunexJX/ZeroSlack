#include "welcomeanimation.h"
#include "activitylogservice.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <array>
#include <cmath>
#include <functional>

namespace {
constexpr qsizetype SceneLimit = 256 * 1024;
constexpr int NodeLimit = 256, PathLimit = 8192, TextLimit = 8192;
enum Property { X, Y, Width, Height, Radius, TranslateX, TranslateY, ScaleX, ScaleY,
    Rotation, Opacity, FillOpacity, StrokeOpacity, StrokeWidth, Characters, PropertyCount };
const QStringList propertyNames{"x", "y", "width", "height", "radius", "translateX", "translateY",
    "scaleX", "scaleY", "rotation", "opacity", "fillOpacity", "strokeOpacity", "strokeWidth", "characters"};
struct Track { int property; bool step; QVector<QPointF> keys; };
struct Node {
    QString type, fill = "none", stroke = "none", text, family = "Consolas";
    qreal fontSize = 9;
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
    std::array<qreal, PropertyCount> values{};
    QPainterPath path;
    QVector<int> textEnds;
    QVector<Track> tracks;
    QVector<Node> children;
};
bool colorValid(const QString& color) {
    static const QRegularExpression hex("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");
    return QStringList{"none", "@ink", "@base", "@muted", "@accent", "@card"}.contains(color)
        || (hex.match(color).hasMatch() && QColor(color).isValid());
}
bool number(const QJsonValue& value, qreal* result, qreal min = -4096, qreal max = 4096) {
    if (!value.isDouble()) return false;
    *result = value.toDouble();
    return std::isfinite(*result) && *result >= min && *result <= max;
}
bool propertyValue(int p, const QJsonValue& value, qreal* result) {
    if (p == Opacity || p == FillOpacity || p == StrokeOpacity) return number(value, result, 0, 1);
    if (p == ScaleX || p == ScaleY) return number(value, result, .01, 8);
    if (p == StrokeWidth) return number(value, result, 0, 32);
    if (p == Characters) return number(value, result, 0, 1024);
    if (p == Width || p == Height || p == Radius) return number(value, result, 0, 4096);
    return number(value, result);
}
struct Budget { int nodes = 0, segments = 0, text = 0, tracks = 0; };
bool parseNodes(const QJsonArray& layers, QVector<Node>* nodes, Budget& budget, QString* error, int depth = 0, qreal parentScale = 1) {
    const auto fail = [&](const QString& reason) { *error = reason; return false; };
    if (depth > 8) return fail("Scene nesting exceeds 8 levels.");
    for (const auto& item : layers) {
        if (!item.isObject() || ++budget.nodes > NodeLimit) return fail("Invalid layer or more than 256 layers.");
        const auto o = item.toObject();
        Node n;
        n.values[ScaleX] = n.values[ScaleY] = n.values[Opacity] = n.values[FillOpacity] = n.values[StrokeOpacity] = 1;
        n.values[StrokeWidth] = 1; n.values[Characters] = 1024;
        n.type = o.value("type").toString();
        if (!QStringList{"group", "rect", "ellipse", "line", "path", "text"}.contains(n.type)) return fail("Unsupported layer type.");
        for (int p = 0; p < PropertyCount; ++p)
            if (o.contains(propertyNames[p]) && !propertyValue(p, o.value(propertyNames[p]), &n.values[p]))
                return fail("Invalid property: " + propertyNames[p]);
        for (auto pair : {std::pair{"fill", &n.fill}, std::pair{"stroke", &n.stroke}}) {
            if (o.contains(pair.first)) *pair.second = o.value(pair.first).toString();
            if (!colorValid(*pair.second)) return fail("Invalid color or theme token.");
        }
        if (o.contains("tracks") && !o.value("tracks").isObject()) return fail("tracks must be an object.");
        const auto tracks = o.value("tracks").toObject();
        for (auto it = tracks.begin(); it != tracks.end(); ++it) {
            const int property = propertyNames.indexOf(it.key());
            if (property < 0 || !it.value().isObject() || ++budget.tracks > 512) return fail("Invalid or too many tracks.");
            const auto t = it.value().toObject();
            const QString mode = t.value("mode").toString("linear");
            const auto keys = t.value("keys").toArray();
            if ((mode != "linear" && mode != "step") || keys.size() < 2 || keys.size() > 64) return fail("Invalid keyframe track.");
            Track track{property, mode == "step", {}};
            qreal previous = -1;
            for (const auto& key : keys) {
                const auto tuple = key.toArray(); qreal t = 0, v = 0;
                if (tuple.size() != 2 || !number(tuple[0], &t, 0, 1) || t <= previous
                    || !propertyValue(property, tuple[1], &v)) return fail("Invalid keyframe time/value.");
                previous = t; track.keys.append({t, v});
            }
            if (track.keys.first().x() != 0 || track.keys.last().x() != 1) return fail("Tracks must include times 0 and 1.");
            n.tracks.append(std::move(track));
        }
        qreal maximumScale = qMax(n.values[ScaleX],n.values[ScaleY]);
        for (const auto& track : n.tracks)
            if (track.property == ScaleX || track.property == ScaleY)
                for (const auto& key : track.keys) maximumScale = qMax(maximumScale,key.y());
        maximumScale *= parentScale;
        if (maximumScale > 8) return fail("Combined layer scale exceeds 8.");
        if (n.type == "path") {
            const auto commands = o.value("path").toArray();
            if (commands.isEmpty()) return fail("A path needs commands.");
            bool moved = false;
            for (const auto& command : commands) {
                if (++budget.segments > PathLimit) return fail("Scene path budget exceeded.");
                const auto c = command.toArray();
                const QString op = c.isEmpty() ? QString() : c[0].toString();
                const int length = op == "M" || op == "L" ? 3 : op == "C" ? 7 : op == "Q" ? 5 : op == "Z" ? 1 : 0;
                if (!length || c.size() != length || (!moved && op != "M")) return fail("Invalid path command.");
                std::array<qreal,6> v{};
                for (int i = 1; i < length; ++i) if (!number(c[i], &v[i-1])) return fail("Invalid path coordinate.");
                if (op == "M") { n.path.moveTo(v[0],v[1]); moved = true; }
                else if (op == "L") n.path.lineTo(v[0],v[1]);
                else if (op == "C") n.path.cubicTo(v[0],v[1],v[2],v[3],v[4],v[5]);
                else if (op == "Q") n.path.quadTo(v[0],v[1],v[2],v[3]);
                else n.path.closeSubpath();
            }
        }
        if (n.type == "text") {
            if (!o.value("text").isString()) return fail("Text layer needs text.");
            n.text = o.value("text").toString(); budget.text += n.text.size();
            if (n.text.size() > 1024 || budget.text > TextLimit) return fail("Scene text budget exceeded.");
            if (o.contains("fontFamily")) n.family = o.value("fontFamily").toString();
            if (n.family.isEmpty() || n.family.size() > 64) return fail("Invalid font family.");
            if (o.contains("fontSize") && !number(o.value("fontSize"), &n.fontSize, 4, 72)) return fail("Invalid font size.");
            const auto align = o.value("align").toString("left");
            if (align != "left" && align != "center" && align != "right") return fail("Invalid text alignment.");
            n.alignment = Qt::AlignVCenter | (align == "left" ? Qt::AlignLeft : align == "right" ? Qt::AlignRight : Qt::AlignHCenter);
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, n.text);
            for (int end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) n.textEnds.append(end);
        }
        if (n.type == "group") {
            if (!o.value("layers").isArray() || !parseNodes(o.value("layers").toArray(), &n.children, budget, error, depth+1, maximumScale)) return fail(error->isEmpty() ? "Group needs layers." : *error);
        } else if (o.contains("layers")) return fail("Only groups may contain layers.");
        nodes->append(std::move(n));
    }
    return true;
}
qreal evaluate(const Track& track, qreal progress) {
    for (int i = 1; i < track.keys.size(); ++i) {
        const auto a = track.keys[i-1], b = track.keys[i];
        if (progress < b.x()) return track.step ? a.y() : a.y() + (b.y()-a.y()) * (progress-a.x()) / (b.x()-a.x());
    }
    return track.keys.last().y();
}
QColor color(const QString& name, const QPalette& palette, const QColor& accent, const QColor& card) {
    if (name == "@ink") return palette.color(QPalette::Text);
    if (name == "@base") return palette.color(QPalette::Base);
    if (name == "@muted") return palette.color(QPalette::PlaceholderText);
    if (name == "@accent") return accent;
    if (name == "@card") return card;
    return QColor(name);
}
void paintNodes(QPainter& painter, const QVector<Node>& nodes, const QPalette& palette,
                const QColor& accent, const QColor& card, qreal progress) {
    for (const auto& node : nodes) {
        auto v = node.values;
        if (progress >= 0) for (const auto& track : node.tracks) v[track.property] = evaluate(track, qBound(0., progress, 1.));
        painter.save();
        painter.translate(v[TranslateX],v[TranslateY]); painter.rotate(v[Rotation]); painter.scale(v[ScaleX],v[ScaleY]);
        painter.setOpacity(painter.opacity()*v[Opacity]);
        auto fill = color(node.fill, palette, accent, card), stroke = color(node.stroke, palette, accent, card);
        fill.setAlphaF(fill.alphaF()*v[FillOpacity]); stroke.setAlphaF(stroke.alphaF()*v[StrokeOpacity]);
        painter.setBrush(node.fill == "none" ? QBrush(Qt::NoBrush) : QBrush(fill));
        painter.setPen(node.stroke == "none" ? QPen(Qt::NoPen) : QPen(stroke,v[StrokeWidth]));
        const QRectF rect(v[X],v[Y],v[Width],v[Height]);
        if (node.type == "rect") painter.drawRoundedRect(rect,v[Radius],v[Radius]);
        else if (node.type == "ellipse") painter.drawEllipse(rect);
        else if (node.type == "line") painter.drawLine(rect.topLeft(),rect.bottomRight());
        else if (node.type == "path") painter.drawPath(node.path);
        else if (node.type == "text") {
            QFont font(node.family); font.setPointSizeF(node.fontSize); painter.setFont(font);
            const int count = qBound(0,int(v[Characters]),int(node.textEnds.size()));
            const auto text = count == node.textEnds.size() ? node.text : node.text.left(count ? node.textEnds[count-1] : 0);
            if (v[Height] > 0) painter.drawText(rect,node.alignment,text);
            else painter.drawText(rect.topLeft(),text);
        } else paintNodes(painter,node.children,palette,accent,card,progress);
        painter.restore();
    }
}
QByteArray readResource(const QString& root, const QString& relative, qsizetype limit, QString* error) {
    const QString path = QDir(root).filePath(relative);
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonical = QFileInfo(path).canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(':') || relative.contains('\\')
        || relative.split('/').contains("..") || canonicalRoot.isEmpty() || canonical.isEmpty()
        || !canonical.startsWith(canonicalRoot + '/', pathCase)) {
        *error = "Missing resource or path outside the resource directory: " + path; return {};
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) { *error = "Unreadable or oversized resource: " + path; return {}; }
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit || bytes.isEmpty()) { *error = "Empty or invalid resource read: " + path; return {}; }
    return bytes;
}
}

struct WelcomeAnimation::Data { QSizeF canvas; QColor accent; int duration = 2400; bool motion = false; QVector<Node> nodes; };
WelcomeAnimation::WelcomeAnimation() = default;
WelcomeAnimation WelcomeAnimation::parse(const QByteArray& bytes, QString* error) {
    WelcomeAnimation result; QString localError; if (!error) error = &localError; error->clear();
    if (bytes.size() > SceneLimit) { *error = "Scene exceeds 256 KiB."; return result; }
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(bytes, &jsonError);
    const auto root = document.object();
    if (jsonError.error != QJsonParseError::NoError || !document.isObject()
        || root.value("format").toString() != "zeroslack.welcome.scene" || root.value("version").toDouble(-1) != 1) {
        *error = "Invalid JSON scene or unsupported scene format/version."; return result;
    }
    auto data = std::make_shared<Data>();
    const auto canvas = root.value("canvas").toArray(); qreal width, height, duration;
    if (canvas.size() != 2 || !number(canvas[0],&width,1,1024) || !number(canvas[1],&height,1,1024)
        || !number(root.value("durationMs"),&duration,100,10000) || std::floor(duration) != duration
        || !root.value("layers").isArray() || root.value("layers").toArray().isEmpty()) {
        *error = "Invalid canvas, duration or layers."; return result;
    }
    const auto accent = root.value("accent").toString();
    if (!accent.startsWith('#') || !colorValid(accent)) { *error = "Scene needs a hexadecimal accent color."; return result; }
    data->canvas = {width,height}; data->duration = int(duration); data->accent = QColor(accent);
    Budget budget;
    if (!parseNodes(root.value("layers").toArray(),&data->nodes,budget,error)) return result;
    data->motion = budget.tracks > 0; result.data = std::move(data); return result;
}
bool WelcomeAnimation::isValid() const { return bool(data); }
bool WelcomeAnimation::hasMotion() const { return data && data->motion; }
int WelcomeAnimation::durationMs() const { return data ? data->duration : 2400; }
QSizeF WelcomeAnimation::canvasSize() const { return data ? data->canvas : QSizeF(230,180); }
QColor WelcomeAnimation::accent(const QPalette& palette) const { return data ? data->accent : palette.color(QPalette::Highlight); }
void WelcomeAnimation::paint(QPainter& painter, const QPalette& palette, const QColor& cardFill, qreal progress) const {
    if (data) { paintNodes(painter,data->nodes,palette,data->accent,cardFill,progress); return; }
    painter.setBrush(palette.color(QPalette::Base)); painter.setPen(QPen(palette.color(QPalette::PlaceholderText),1.6));
    painter.drawRoundedRect(QRectF(7,26,210,126),7,7);
    for (int y : {65,88,111}) painter.drawLine(QPointF(38,y),QPointF(186,y));
}
WelcomeAnimationLibrary::WelcomeAnimationLibrary(const QString& directory) : root(QDir(directory).absolutePath()) {
    const QStringList ids{"rtl", "waveform", "registers"};
    for (const auto& id : ids) scenes.insert(id,{});
    QString error;
    const auto bytes = readResource(root,"manifest.json",64*1024,&error);
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(bytes,&jsonError);
    const auto manifest = document.object();
    if (error.isEmpty() && (jsonError.error != QJsonParseError::NoError || !document.isObject()
        || manifest.value("format").toString() != "zeroslack.welcome.bundle" || manifest.value("version").toDouble(-1) != 1
        || !manifest.value("cards").isObject())) error = "Invalid manifest or unsupported bundle format/version: " + QDir(root).filePath("manifest.json");
    if (!error.isEmpty()) { errors.append(error); return; }
    const auto cards = manifest.value("cards").toObject();
    for (const auto& id : ids) {
        const auto entry = cards.value(id).toObject(); error.clear();
        const auto path = entry.value("scene").toString();
        const auto sceneBytes = readResource(root,path,SceneLimit,&error);
        const auto hash = entry.value("sha256").toString().toLatin1();
        if (error.isEmpty() && (hash.size() != 64 || QCryptographicHash::hash(sceneBytes,QCryptographicHash::Sha256).toHex() != hash.toLower()))
            error = "Scene SHA-256 mismatch; update manifest after editing.";
        if (error.isEmpty()) scenes[id] = WelcomeAnimation::parse(sceneBytes,&error);
        if (!error.isEmpty()) errors.append(id + " (" + QDir(root).filePath(path) + "): " + error);
    }
}
const WelcomeAnimationLibrary& WelcomeAnimationLibrary::applicationLibrary() {
    static const WelcomeAnimationLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath("resources/welcome"));
    static const bool logged = [&] {
        for (const auto& error : library.diagnostics())
            ActivityLogService::getInstance()->append("Welcome resources",ActivityLogLevel::Warning,error);
        return true;
    }();
    Q_UNUSED(logged)
    return library;
}
const WelcomeAnimation& WelcomeAnimationLibrary::scene(const QString& id) const {
    const auto found = scenes.constFind(id);
    if (found != scenes.cend()) return found.value();
    static const WelcomeAnimation fallback; return fallback;
}
