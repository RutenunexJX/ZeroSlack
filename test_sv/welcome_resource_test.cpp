#include "welcomeanimation.h"
#include <QtWidgets>
#include <QtTest>

class WelcomeResourceTest : public QObject {
    Q_OBJECT
    QString source() const { return QCoreApplication::applicationDirPath()+"/resources/welcome"; }
    static QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
    static bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size(); }
    bool copyBundle(const QString& dest, bool alternate = false) {
        const QString from=source()+(alternate ? "/examples/alternate" : "");
        for (const auto& name : {"manifest.json","rtl.scene.json","waveform.scene.json","registers.scene.json"})
            if (!write(dest+'/'+name,read(from+'/'+name))) return false;
        return true;
    }
    void replaceScene(const QString& root, const QJsonObject& scene) {
        const auto bytes=QJsonDocument(scene).toJson(); QVERIFY(write(root+"/rtl.scene.json",bytes));
        auto manifest=QJsonDocument::fromJson(read(root+"/manifest.json")).object();
        auto cards=manifest["cards"].toObject();auto entry=cards["rtl"].toObject();
        entry["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        cards["rtl"]=entry;manifest["cards"]=cards;QVERIFY(write(root+"/manifest.json",QJsonDocument(manifest).toJson()));
    }
    static QImage render(const WelcomeAnimation& scene, qreal progress = -1, qreal dpr = 1) {
        QImage image(QSize(460,360)*dpr,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(dpr);image.fill(Qt::white);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(2,2);
        QPalette palette;palette.setColor(QPalette::Text,Qt::black);palette.setColor(QPalette::Base,Qt::white);
        scene.paint(painter,palette,QColor("#f1f4fa"),progress);return image;
    }
private slots:
    void defaultScenesAndIndependentReplacement() {
        WelcomeAnimationLibrary defaults(source());QVERIFY2(defaults.diagnostics().isEmpty(),qPrintable(defaults.diagnostics().join('\n')));
        WelcomeAnimationLibrary alternate(source()+"/examples/alternate");QVERIFY(alternate.diagnostics().isEmpty());
        for (const QString id : {"rtl","waveform","registers"}) {
            const auto& scene=defaults.scene(id);QVERIFY(scene.isValid());QVERIFY(scene.hasMotion());QCOMPARE(scene.durationMs(),2400);
            const auto resting=render(scene);QVERIFY(resting!=render(scene,.25));QVERIFY(resting!=render(alternate.scene(id)));
            QCOMPARE(render(scene,-1,2).devicePixelRatio(),2.);QVERIFY(!render(scene,-1,1.5).isNull());
        }
    }
    void validationAndRecovery() {
        QString parseError;
        QVERIFY(!WelcomeAnimation::parse("{broken",&parseError).isValid());QVERIFY(!parseError.isEmpty());
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(copyBundle(dir.path()));
        const auto original=QJsonDocument::fromJson(read(dir.path()+"/rtl.scene.json")).object();
        for (int scenario=0;scenario<10;++scenario) {
            QVERIFY(copyBundle(dir.path()));auto scene=original;
            if (scenario==0) QVERIFY(QFile::remove(dir.path()+"/rtl.scene.json"));
            if (scenario==1) QVERIFY(write(dir.path()+"/rtl.scene.json","{broken"));
            if (scenario==2) {scene["version"]=2;replaceScene(dir.path(),scene);}
            if (scenario==3) {scene["layers"]=QJsonArray{QJsonObject{{"type","script"}}};replaceScene(dir.path(),scene);}
            if (scenario==4) {scene["durationMs"]=10001;replaceScene(dir.path(),scene);}
            if (scenario==5) {QJsonArray nodes;for(int i=0;i<257;++i)nodes.append(QJsonObject{{"type","rect"}});scene["layers"]=nodes;replaceScene(dir.path(),scene);}
            if (scenario==6) {scene["layers"]=QJsonArray{QJsonObject{{"type","rect"},{"tracks",QJsonObject{{"width",QJsonObject{{"keys",QJsonArray{QJsonArray{0,1},QJsonArray{0,2}}}}}}}}};replaceScene(dir.path(),scene);}
            if (scenario==7) QVERIFY(write(dir.path()+"/rtl.scene.json",QByteArray(256*1024+1,' ')));
            if (scenario==8) {auto m=QJsonDocument::fromJson(read(dir.path()+"/manifest.json")).object();auto cards=m["cards"].toObject();cards["rtl"]=QJsonObject{{"scene","../outside.json"},{"sha256",QString(64,'0')}};m["cards"]=cards;QVERIFY(write(dir.path()+"/manifest.json",QJsonDocument(m).toJson()));}
            if (scenario==9) {scene["layers"]=QJsonArray{QJsonObject{{"type","group"},{"scaleX",8},{"layers",QJsonArray{QJsonObject{{"type","rect"},{"scaleX",2}}}}}};replaceScene(dir.path(),scene);}
            WelcomeAnimationLibrary failed(dir.path());QVERIFY2(!failed.scene("rtl").isValid(),qPrintable(QString::number(scenario)));
            QVERIFY(!failed.scene("rtl").hasMotion());QVERIFY(failed.scene("waveform").isValid());QVERIFY(failed.scene("registers").isValid());QVERIFY(!failed.diagnostics().isEmpty());
            QVERIFY(!render(failed.scene("rtl")).isNull());
            QVERIFY(copyBundle(dir.path()));WelcomeAnimationLibrary recovered(dir.path());QVERIFY(recovered.diagnostics().isEmpty());QVERIFY(recovered.scene("rtl").isValid());
        }
        auto manifest=QJsonDocument::fromJson(read(dir.path()+"/manifest.json")).object();manifest["version"]=2;
        QVERIFY(write(dir.path()+"/manifest.json",QJsonDocument(manifest).toJson()));
        WelcomeAnimationLibrary unsupported(dir.path());QVERIFY(!unsupported.scene("rtl").isValid());QVERIFY(!unsupported.scene("waveform").isValid());
        QVERIFY(QFile::remove(dir.path()+"/manifest.json"));WelcomeAnimationLibrary missing(dir.path());QVERIFY(!missing.scene("registers").isValid());
        QCOMPARE(render(missing.scene("rtl")),render(missing.scene("waveform")));
    }
    void immutablePlaybackWithoutFilesystem() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(copyBundle(dir.path()));WelcomeAnimationLibrary library(dir.path());
        const auto before=render(library.scene("rtl"),.37);
        for(const auto& name : {"manifest.json","rtl.scene.json","waveform.scene.json","registers.scene.json"})QVERIFY(QFile::remove(dir.path()+'/'+name));
        QCOMPARE(render(library.scene("rtl"),.37),before);
        QElapsedTimer timer;timer.start();
        for (int i=0;i<120;++i) for(const QString id : {"rtl","waveform","registers"}) QVERIFY(!render(library.scene(id),i/119.).isNull());
        qInfo() << "360 vector frames at 2x logical size:" << timer.elapsed() << "ms (offscreen; not display latency)";
    }
};
QTEST_MAIN(WelcomeResourceTest)
#include "welcome_resource_test.moc"
