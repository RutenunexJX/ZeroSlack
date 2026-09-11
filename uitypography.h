#pragma once
#include <QFontDatabase>
#include <QDir>
#include <QGuiApplication>
#include <QWidget>

namespace UiTypography {
enum class Role { PageTitle, PanelTitle, Section, Body, Metadata, Badge };

inline QFont font(Role role = Role::Body)
{
    static const QStringList families = [] {
        QStringList preferred { QStringLiteral("Noto Sans"), QStringLiteral("Segoe UI"),
            QStringLiteral("Noto Sans SC"), QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Arial") };
#ifdef Q_OS_WIN
        // Offscreen Qt has no native font database; use installed system fonts for previews.
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            const QDir fonts(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) + QStringLiteral("/Fonts"));
            for (const auto* file : {"cour.ttf", "courbd.ttf", "consola.ttf", "consolab.ttf", "segoeui.ttf", "seguisb.ttf", "segoeuib.ttf", "msyh.ttc", "NotoSansSC-VF.ttf", "NotoSans-Regular.ttf", "NotoSans-Bold.ttf"})
                QFontDatabase::addApplicationFont(fonts.filePath(QString::fromLatin1(file)));
        }
#endif
        preferred.append(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        return preferred;
    }();
    QFont result;
    result.setFamilies(families);
    result.setStyleHint(QFont::SansSerif);
    result.setFixedPitch(false);
    result.setKerning(true);
    const bool strong = role == Role::PageTitle || role == Role::PanelTitle
        || role == Role::Section;
    result.setWeight(strong ? QFont::DemiBold : QFont::Normal);
    result.setPixelSize(role == Role::PageTitle ? 18 : role == Role::PanelTitle ? 14
        : role == Role::Badge ? 11 : role == Role::Metadata || role == Role::Section ? 12 : 13);
    return result;
}

inline void apply(QWidget* widget, Role role)
{
    if (!widget) return;
    widget->setFont(font(role));
    widget->setProperty("uiTextRole", role == Role::Metadata ? "metadata"
        : role == Role::PageTitle ? "pageTitle" : role == Role::PanelTitle ? "panelTitle"
        : role == Role::Section ? "section" : role == Role::Badge ? "badge" : "body");
}
}
