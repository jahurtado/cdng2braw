#include "profile_store.h"
#include "profile_window.h"
#include "converter_window.h"
#include "i18n.h"
#include "automation_server.h"
#ifdef Q_OS_WIN
#include "sdk_probe.h"
#include <QJsonArray>
#include <cstdlib>
#endif
#include <QApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <cstdio>
#include <memory>

int main(int argc, char **argv) {
    // Read-only CLI needs neither a display server nor a running Resolve.
    if (argc == 2 && QByteArray(argv[1]) == "--profile-status") {
        QCoreApplication app(argc, argv);
        cdng::initializeLanguage();
        const cdng::ProfileStore store;
        const auto state = store.inspect();
        const auto output = QJsonDocument(QJsonObject{{"state", cdng::stateName(state.state)},
            {"detail", state.detail}, {"resolve_loaded", "unverified"},
            {"file_verified", state.state == cdng::ProfileState::Ready},
            {"expected_profile_sha256", cdng::ProfileSha}, {"expected_signal_sha256", cdng::SignalSha},
            {"support", store.paths().support}}).toJson();
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
        return state.state == cdng::ProfileState::Error ? 1 : 0;
    }
#ifdef Q_OS_WIN
    // ¿Lee el decodificador BRAW de Resolve la carpeta Camera Support y qué ficheros? Ver sdk_probe.h. La ventana
    // del perfil lo ejecuta en un proceso aparte para decir si Resolve carga el perfil, no solo si el fichero está.
    if (argc == 2 && QByteArray(argv[1]) == "--probe-camera-support") {
        QCoreApplication app(argc, argv);
        const auto exe = cdng::Paths::resolveExecutable();
        const auto probe = exe.isEmpty() ? cdng::SdkProbe{}
                                         : cdng::probeCameraSupport(QFileInfo(exe).absolutePath() + "/BlackmagicRawAPI.dll");
        const auto output = QJsonDocument(QJsonObject{{"dll", probe.dll}, {"codec", probe.codec},
            {"listed", QJsonArray::fromStringList(probe.listed)}, {"read", QJsonArray::fromStringList(probe.read)},
            {"error", probe.error}}).toJson();
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
        std::fflush(stdout);
        // Sin destructores: la DLL de Blackmagic deja hilos que no conviene esperar ni descargar.
        std::_Exit(probe.error.isEmpty() ? 0 : 1);
    }
#endif
    // CLI de instalación del perfil (sin pantalla): --install-profile/--uninstall-profile [<id>] [--language L]
    // [--result FICHERO]. El id es el de ProfileSpec (sigma-fp, pocket-og); vacío = sigma. En Windows la ventana
    // del perfil se relanza así elevada (UAC): un proceso elevado no comparte stdout, por eso --result.
    if (argc >= 2 && (QByteArray(argv[1]) == "--install-profile" || QByteArray(argv[1]) == "--uninstall-profile")) {
        QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        auto option = [&](const char *name) { const auto at = args.indexOf(name); return at >= 0 && at + 1 < args.size() ? args[at + 1] : QString(); };
        cdng::initializeLanguage(option("--language"));
        const bool install = QByteArray(argv[1]) == "--install-profile";
        const QString id = args.size() >= 3 && !args[2].startsWith("--") ? args[2] : QString();
        const cdng::ProfileStore store(cdng::Paths::system(), cdng::Bundle::embedded(cdng::ProfileSpec::byId(id)));
        const auto result = install ? store.install() : store.remove();
#ifdef Q_OS_WIN
        // Hasta 0.7.0-beta.1 el perfil se instalaba bajo ProgramData, donde el SDK no lo lee: se retira esa copia.
        if (result.ok) {
            const QDir legacy(QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData", "C:/ProgramData")) +
                              "/Blackmagic Design/Blackmagic RAW/Camera Support");
            if (!store.spec().file.isEmpty() && legacy.absolutePath() != QDir(store.paths().support).absolutePath()) {
                if (legacy.exists(store.spec().file)) QFile::remove(legacy.filePath(store.spec().file));
                if (legacy.exists() && legacy.isEmpty()) QDir().rmdir(legacy.absolutePath());
            }
        }
#endif
        const auto state = store.inspect();
        const auto output = QJsonDocument(QJsonObject{{"ok", result.ok}, {"changed", result.changed},
            {"message", result.message}, {"backup", result.backup}, {"state", cdng::stateName(state.state)},
            {"support", store.paths().support}, {"luts", store.paths().luts}, {"lut_paths", QJsonArray::fromStringList(store.lutPaths())}}).toJson();
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
        if (const auto path = option("--result"); !path.isEmpty()) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(output) != output.size()) return 1;
        }
        return result.ok ? 0 : 1;
    }
    QApplication app(argc, argv);
    app.setApplicationName("CDNG to BRAW Converter");
    app.setWindowIcon(QIcon(":/ui/icon.png"));
    app.setApplicationVersion("1.0.1");
    const auto args = app.arguments();
    const auto lang = args.indexOf("--language");
    if (lang >= 0 && (lang + 1 >= args.size() || !QStringList{"system", "en", "es"}.contains(args[lang + 1]))) return 2;
    cdng::initializeLanguage(lang >= 0 ? args[lang + 1] : QString());
    // Opciones de automatización (solo con --automation): carpeta de perfiles de prueba y motor alternativo.
    auto value = [&](const char *name) { const auto at = args.indexOf(name); return at >= 0 && at + 1 < args.size() ? args[at + 1] : QString(); };
    const bool automation = args.contains("--automation");
    const auto profileRoot = automation ? value("--profile-root") : QString();
    // Ruta absoluta: el trabajo de conversión arranca el motor desde su carpeta temporal.
    const auto backend = automation && !value("--backend").isEmpty() ? QFileInfo(value("--backend")).absoluteFilePath() : QString();
    const auto paths = profileRoot.isEmpty() ? cdng::Paths::system() : cdng::Paths{profileRoot + "/support", profileRoot + "/backups", true, profileRoot + "/LUT/cdng2braw"};
    auto makeWindow = [paths, backend] { return new cdng::ConverterWindow(cdng::ProfileStore(paths), backend); };
    QPointer<cdng::ConverterWindow> window = makeWindow();
    std::function<void(cdng::ConverterWindow *)> connectLanguage;
    connectLanguage = [&](cdng::ConverterWindow *current) {
        QObject::connect(current, &cdng::ConverterWindow::languageChangeRequested, &app, [&, current](const QString &preference) {
            const auto state = current->snapshot();
            cdng::initializeLanguage(preference, true);
            auto *replacement = makeWindow();
            replacement->restore(state); connectLanguage(replacement); replacement->show();
            window = replacement; current->close(); current->deleteLater();
        });
    };
    connectLanguage(window); window->show();
    const auto screenshot = args.indexOf("--screenshot");
    if (screenshot >= 0) {
        if (screenshot + 1 >= args.size()) return 2;
        const auto path = args[screenshot + 1];
        QTimer::singleShot(150, window, [&window, &app, path] {
            app.exit(window->grab().save(path) ? 0 : 1);
        });
    }
    // --automation [NOMBRE]: servidor de control de la interfaz para pruebas (automation_server.h). Publica el
    // socket y el token en stdout y en un fichero 0600 del directorio temporal.
    std::unique_ptr<cdng::AutomationServer> server;
    if (automation) {
        const auto at = args.indexOf("--automation");
        const auto name = at + 1 < args.size() && !args[at + 1].startsWith("--") ? args[at + 1] : QString();
        server = std::make_unique<cdng::AutomationServer>(name, paths);
        QString error;
        if (!server->listen(&error)) { std::fprintf(stderr, "automation: %s\n", qPrintable(error)); return 2; }
        const auto file = server->writeConnectionFile(&error);
        const auto line = QJsonDocument(QJsonObject{{"automation", server->serverName()}, {"token", server->token()},
            {"pid", QCoreApplication::applicationPid()}, {"connection_file", file}}).toJson(QJsonDocument::Compact) + "\n";
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout); std::fflush(stdout);
    }
    // 0.5: the Video Assist route needs no installed profile, so no start-up install dialog.
    return app.exec();
}
