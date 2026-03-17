#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtPlugin>

Q_IMPORT_PLUGIN(TsimCAT_UIPlugin)
Q_IMPORT_PLUGIN(TsimCAT_BackendPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");

    QQmlApplicationEngine engine;

    using namespace Qt::StringLiterals;
    const QUrl url(u"qrc:/qt/qml/TsimCAT/UI/Main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    return app.exec();
}