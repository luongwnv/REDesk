// REDesk — Qt Quick UI client entry point (ADR-001 §3.5).
//
// Boots a QGuiApplication + QQmlApplicationEngine and loads the QML module's
// Main.qml. The C++ types (VideoItem, SessionController) are registered into the
// `REDesk.UI` module automatically via QML_ELEMENT / QML_SINGLETON, so QML just
// `import REDesk.UI`.
//
// Uses Quick (not Widgets) and a single QQmlApplicationEngine — there is no
// QMediaPlayer/QVideoSink anywhere; the live stream is presented by VideoItem
// (QQuickRhiItem) per ADR §3.5.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QtQml/QQmlContext>

#include "generated/redesk_version.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName(QStringLiteral("REDesk"));
    QGuiApplication::setOrganizationName(QStringLiteral("REDesk"));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(redesk::kVersion));

    // ADR §3.5: present the live-video region with minimal latency. Prefer a
    // backend that allows ASAP/mailbox presentation of the video surface; chrome
    // stays vsync'd. The fine-grained policy lives in VideoItem's renderer.
    // (Backend selection is left to QSG defaults here; pin per-OS in deployment.)

    QQmlApplicationEngine engine;

    // Surface the version to QML for the about/footer affordance.
    engine.rootContext()->setContextProperty(
        QStringLiteral("appVersion"), QString::fromUtf8(redesk::kVersion));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("REDesk.UI"), QStringLiteral("Main"));

    return app.exec();
}
