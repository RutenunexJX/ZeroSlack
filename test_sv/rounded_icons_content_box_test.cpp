#include "mainwindow.h"
#include "navigationwidget.h"
#include "workspacemanager.h"
#include "tabmanager.h"
#include "roundedicons.h"
#include "actionregistry.h"
#include "applicationthememanager.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "testuistyle.h"
#include <QtTest>
#include <QScreen>
#include <QTreeWidgetItemIterator>
#include <QCryptographicHash>

namespace {
bool write(const QString& path,const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content)==content.size();
}
QJsonArray rectangle(const QRect& r) {return {r.x(),r.y(),r.width(),r.height()};}
QJsonObject bounds(const QImage& image,int alpha) {
    int left=image.width(),top=image.height(),right=-1,bottom=-1;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        if(qAlpha(image.pixel(x,y))>=alpha){left=qMin(left,x);right=qMax(right,x);top=qMin(top,y);bottom=qMax(bottom,y);}
    return {{"left",left},{"rightInclusive",right},{"width",right-left+1},{"top",top},{"bottomInclusive",bottom},{"threshold",alpha}};
}
QString output() {return qEnvironmentVariable("ZEROSLACK_ICONS_REPORT");}
}
class RoundedIconsContentBoxTest:public QObject {
    Q_OBJECT
private slots:
    void semanticCatalog() {
        using namespace RoundedIcons;
        const QPalette original = QApplication::palette();
        const QList<QPair<Kind,Kind>> distinct{
            {Add,ZoomIn},{Close,Delete},{Module,Instance},{SourceTop,SimulationTop},
            {Activity,RunLog},{RunLog,SimulationEvents},{Appearance,WaveStyle},
            {File,Files},{BlockText,BlockCode},{BlockReference,CopyLink}};
        for (const bool dark : {false,true}) {
            QPalette palette = original;
            palette.setColor(QPalette::Text,dark?QColor("#e2e5e9"):QColor("#24282d"));
            palette.setColor(QPalette::Disabled,QPalette::Text,dark?QColor("#78818b"):QColor("#8d9299"));
            QApplication::setPalette(palette);
            for (qreal dpr : {1.,1.5,2.}) {
                QList<QByteArray> hashes;
                QImage sheet(16*96,14*58,QImage::Format_ARGB32_Premultiplied);
                sheet.fill(dark?QColor("#20242a"):Qt::white);
                QPainter atlas(&sheet);atlas.setPen(palette.color(QPalette::Text));
                for (int i=0;i<CatalogSize;++i) {
                    const auto kind=static_cast<Kind>(i);
                    QVERIFY2(!glyph(kind).isEmpty(),semanticId(kind));
                    QVERIFY2(!icon(kind).isNull(),semanticId(kind));
                    QImage normal;
                    for (auto mode : {QIcon::Normal,QIcon::Disabled}) {
                        QImage image(qRound(18*dpr),qRound(18*dpr),QImage::Format_ARGB32_Premultiplied);
                        image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
                        QPainter painter(&image);icon(kind).paint(&painter,QRect(0,0,18,18),Qt::AlignCenter,mode,QIcon::Off);painter.end();
                        QVERIFY2(bounds(image,1)["width"].toInt()>0,semanticId(kind));
                        const auto color=palette.color(mode==QIcon::Disabled?QPalette::Disabled:QPalette::Active,QPalette::Text);
                        bool found=false;
                        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                            const auto ink=image.pixel(x,y);const auto alpha=qAlpha(ink);if(alpha<128)continue;
                            QVERIFY(qAbs(qRed(ink)-qRound(color.red()*alpha/255.))<=1
                                && qAbs(qGreen(ink)-qRound(color.green()*alpha/255.))<=1
                                && qAbs(qBlue(ink)-qRound(color.blue()*alpha/255.))<=1);found=true;
                        }
                        QVERIFY2(found,semanticId(kind));
                        if(mode==QIcon::Normal)normal=image;
                    }
                    QImage selected(normal.size(),QImage::Format_ARGB32_Premultiplied);
                    selected.setDevicePixelRatio(dpr);selected.fill(Qt::transparent);
                    QPainter selectionPainter(&selected);
                    icon(kind).paint(&selectionPainter,QRect(0,0,18,18),Qt::AlignCenter,QIcon::Selected,QIcon::On);
                    selectionPainter.end();
                    QCOMPARE(selected,normal);
                    hashes.append(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(normal.constBits()),normal.sizeInBytes()),QCryptographicHash::Sha256));
                    atlas.drawImage(QPoint(i%16*96+38,i/16*58+4),normal);
                    atlas.drawText(QRect(i%16*96,i/16*58+27,96,30),Qt::AlignCenter|Qt::TextWordWrap,QString::fromLatin1(semanticId(kind)));
                }
                atlas.end();
                for(const auto& pair:distinct)QVERIFY2(hashes[pair.first]!=hashes[pair.second],semanticId(pair.first));
                if(!output().isEmpty())QVERIFY(sheet.save(output()+QString("/catalog-%1-%2.png").arg(dark?"dark":"light").arg(qRound(dpr*100))));
            }
        }
        QApplication::setPalette(original);
        QStringList missing;
        QJsonArray mappings;
        for (const auto& action : actionRegistry()) {
            mappings.append(QJsonObject{{"actionId",action.id},{"iconId",QString::fromLatin1(semanticId(actionKind(action.id)))}});
            // This text-only command has no equivalent in the approved catalog.
            if (action.id == "insert.columnNumbers") { QVERIFY(iconForAction(action.id).isNull()); continue; }
            if (iconForAction(action.id).isNull()) missing.append(action.id);
        }
        if(!output().isEmpty())QVERIFY(write(output()+"/registered-actions.json",QJsonDocument(mappings).toJson()));
        QVERIFY2(missing.isEmpty(),qPrintable(missing.join(", ")));
        QVERIFY(iconForAction("not.registered").isNull());
        QCOMPARE(actionKind("file.save"),Save);
        QMenu menu;
        auto* registered=menu.addAction("Arbitrary presentation label");registered->setProperty("actionId","file.save");
        auto* remove=menu.addAction("Delete");auto* close=menu.addAction("Close");auto* userValue=menu.addAction("user-defined value");
        applyMenuIcons(&menu);
        QCOMPARE(registered->icon().cacheKey(),icon(Save).cacheKey());
        QVERIFY(!remove->icon().isNull());QVERIFY(remove->icon().cacheKey()!=close->icon().cacheKey());
        QVERIFY(userValue->icon().isNull());
    }
    void raster() {
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        const bool baseline=qEnvironmentVariableIsSet("ZEROSLACK_ICONS_BASELINE");
        QJsonArray records;
        for(qreal dpr:{1.,1.5,2.}) {
            QList<QJsonObject> measured;
            for(auto kind:{RoundedIcons::Folder,RoundedIcons::FolderOpen,RoundedIcons::File}) {
                const QString name=kind==RoundedIcons::Folder?"folder":kind==RoundedIcons::FolderOpen?"folder-open":"file";
                const int side=qRound(18*dpr);QImage image(side,side,QImage::Format_ARGB32_Premultiplied);
                image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
                QPainter painter(&image);RoundedIcons::icon(kind).paint(&painter,QRect(0,0,18,18),Qt::AlignCenter,QIcon::Normal,QIcon::Off);
                painter.end();
                const auto full=bounds(image,1),core=bounds(image,128);measured<<full;
                if(!output().isEmpty())QVERIFY(image.save(output()+QString("/%1-%2.png").arg(name).arg(qRound(dpr*100))));
                const int minWidth=qFloor((18+1.75)*18/24*dpr),maxWidth=qCeil((18+1.75)*18/24*dpr)+1;
                QJsonObject record{{"kind",name},{"requestedDpr",dpr},{"actualDpr",image.devicePixelRatio()},
                    {"physicalSize",QJsonArray{image.width(),image.height()}},{"logicalRect",QJsonArray{0,0,18,18}},
                    {"mode","Normal"},{"state","Off"},{"textColor",QApplication::palette().color(QPalette::Text).name()},
                    {"alphaNonzero",full},{"alphaCore",core},{"allowedPhysicalWidth",QJsonArray{minWidth,maxWidth}}};
                records.append(record);
                QCOMPARE(image.width(),side);QCOMPARE(image.devicePixelRatio(),dpr);
                if(!baseline)QVERIFY(full["width"].toInt()>=minWidth && full["width"].toInt()<=maxWidth);
            }
            if(!baseline) {
                QCOMPARE(measured[0]["left"],measured[1]["left"]);
                QCOMPARE(measured[0]["left"],measured[2]["left"]);
                QCOMPARE(measured[0]["rightInclusive"],measured[1]["rightInclusive"]);
                QCOMPARE(measured[0]["width"],measured[1]["width"]);
                QCOMPARE(measured[0]["rightInclusive"],measured[2]["rightInclusive"]);
                QCOMPARE(measured[0]["width"],measured[2]["width"]);
            }
        }
        if(!output().isEmpty())QVERIFY(write(output()+"/raster.json",QJsonDocument(records).toJson()));
    }
    void actualFiles() {
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QTemporaryDir fallback;
        const QString workspace=qEnvironmentVariable("ZEROSLACK_ICONS_FIXTURE",fallback.filePath("icon-fixture"));
        QVERIFY(write(workspace+"/rtl/child.sv","module child; endmodule\n"));
        QVERIFY(write(workspace+"/top.sv","module top; child u_child(); endmodule\n"));
        QTemporaryDir state;
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH",state.filePath("session.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH",state.filePath("editing.json").toUtf8());
        MainWindow window;
        window.tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(state.filePath("recovery")));
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        const auto available=window.screen()->availableGeometry();
        window.resize(qMin(1100,available.width()),qMin(760,available.height()));window.show();
        QVERIFY(window.workspaceManager->openWorkspace(workspace));
        auto* nav=window.findChild<NavigationWidget*>();QVERIFY(nav);nav->setActiveTab(NavigationWidget::FileTab);
        auto* tree=nav->findChild<QTreeWidget*>("navigationFileTree");QVERIFY(tree);
        QTreeWidgetItem* folder=nullptr;QTreeWidgetItem* file=nullptr;
        auto locate=[&] {
            folder=nullptr;file=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it) {
                if((*it)->text(0)=="rtl")folder=*it;
                if((*it)->text(0)=="top.sv")file=*it;
            }
            return folder && file;
        };
        QTRY_VERIFY_WITH_TIMEOUT(locate(),15000);
        QJsonArray rows;
        for(bool expanded:{false,true}) {
            folder->setExpanded(expanded);tree->clearSelection();tree->setCurrentItem(nullptr);tree->clearFocus();
            QTest::qWait(300);
            QTRY_VERIFY_WITH_TIMEOUT(locate() && folder->isExpanded()==expanded,10000);
            const QString name=expanded?"files-expanded":"files-collapsed";
            if(!output().isEmpty()) {
                QVERIFY(window.grab().save(output()+"/mainwindow-"+name+".png"));
                QVERIFY(nav->grab().save(output()+"/"+name+".png"));
            }
            for(auto* item:{folder,file}) {
                const auto row=tree->visualItemRect(item);int depth=0;for(auto* parent=item->parent();parent;parent=parent->parent())++depth;
                QStyleOptionViewItem option;option.initFrom(tree);option.rect=row.adjusted(2+depth*14,0,0,0);
                option.features=QStyleOptionViewItem::HasDisplay|QStyleOptionViewItem::HasDecoration;
                option.text=item->text(0);option.icon=item->icon(0);option.decorationSize=tree->iconSize();
                option.decorationPosition=QStyleOptionViewItem::Left;option.displayAlignment=Qt::AlignLeft|Qt::AlignVCenter;
                rows.append(QJsonObject{{"state",name},{"item",item->text(0)},{"row",rectangle(row)},
                    {"delegateContentRect",rectangle(option.rect)},
                    {"styleIconRect",rectangle(tree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration,&option,tree))},
                    {"styleTextRect",rectangle(tree->style()->subElementRect(QStyle::SE_ItemViewItemText,&option,tree))}});
            }
        }
        if(!output().isEmpty())QVERIFY(write(output()+"/files-geometry.json",QJsonDocument(QJsonObject{
            {"platform",QGuiApplication::platformName()},{"dpr",window.devicePixelRatioF()},
            {"windowClient",rectangle(window.geometry())},{"windowFrame",rectangle(window.frameGeometry())},
            {"availableGeometry",rectangle(available)},{"navigationSize",QJsonArray{nav->width(),nav->height()}},
            {"iconSize",QJsonArray{tree->iconSize().width(),tree->iconSize().height()}},
            {"font",tree->font().toString()},{"rows",rows}}).toJson()));
        window.close();
    }
};
int main(int argc,char** argv) {
    QStandardPaths::setTestModeEnabled(true);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackIconTest");QCoreApplication::setApplicationName("ZeroSlackIconTest");
    QTemporaryDir profile;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if(!initializeUiStyleForTest())return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false);ApplicationThemeManager::instance().applyToApplication();
    RoundedIconsContentBoxTest test;return QTest::qExec(&test,argc,argv);
}
#include "rounded_icons_content_box_test.moc"
