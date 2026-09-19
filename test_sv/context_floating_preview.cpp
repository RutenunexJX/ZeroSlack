#include "applicationthememanager.h"
#include "contextfloatingwindow.h"
#include "liveinsightscontextview.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

// Native desktop fixture for DWM composition, which offscreen tests cannot render.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ApplicationThemeManager::instance().applyToApplication();
    QMainWindow main;
    main.setWindowTitle(QStringLiteral("ZeroSlack Acrylic verification"));
    auto* background = new QWidget(&main);
    background->setObjectName(QStringLiteral("acrylicTestBackground"));
    background->setStyleSheet(QStringLiteral(
        "#acrylicTestBackground { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
        "stop:0 #76c7c0, stop:0.4 #c8a5e5, stop:0.7 #edb37a, stop:1 #79a7e0); }"));
    main.setCentralWidget(background);
    auto* layout = new QVBoxLayout(background);
    auto* caption = new QLabel(QStringLiteral("BACKGROUND  /  0123456789"), background);
    caption->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: bold; color: #203047;"));
    layout->addWidget(caption, 0, Qt::AlignCenter);
    layout->addStretch();
    main.resize(1000, 700);
    main.move(main.screen()->availableGeometry().center() - main.rect().center());
    main.show();

    ContextFloatingWindow floating(&main, background);
    auto* content = new QWidget;
    auto* controls = new QVBoxLayout(content);
    auto* theme = new QPushButton(QStringLiteral("Switch light / dark"), content);
    controls->addWidget(theme);
    auto* slider = new QSlider(Qt::Horizontal, content);
    slider->setRange(60, 100);
    slider->setValue(floating.backgroundOpacity());
    controls->addWidget(slider);
    auto* status = new QLabel(content);
    controls->addWidget(status);
    // Use the production insight view, including its normal application styling.
    controls->addWidget(new LiveInsightsContextView(nullptr, LiveInsightKind::Kernel, content), 1);
    QObject::connect(slider, &QSlider::valueChanged, &floating, &ContextFloatingWindow::setBackgroundOpacity);
    QObject::connect(theme, &QPushButton::clicked, &floating, [] {
        auto& manager = ApplicationThemeManager::instance();
        manager.setMode(isDarkTheme(manager.mode()) ? ThemeMode::Light : ThemeMode::Dark);
    });
    QObject::connect(&floating, &ContextFloatingWindow::closeRequested, &app, &QApplication::quit);
    ContextResource resource;
    resource.providerId = QStringLiteral("preview");
    resource.resourceId = QStringLiteral("native-acrylic");
    resource.uri = QUrl(QStringLiteral("preview:/acrylic"));
    resource.title = QStringLiteral("ZeroSlack Acrylic floating preview");
    floating.setActionsAvailable(false, false);
    floating.setInitialSize(QSize(560, 440));
    floating.setView(resource, content);
    status->setText(floating.hasAcrylicBackdrop()
        ? QStringLiteral("Desktop Acrylic enabled · text opacity 100%")
        : QStringLiteral("Solid fallback · text opacity 100%"));
    return app.exec();
}
