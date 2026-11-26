#pragma once
// Bloque E — proceso de trabajo de la app (cdng-convert). Port del prototipo en Python: mismo protocolo
// (--request FICHERO, --preview-server, --self-test), mismos eventos JSON por stdout, mismos códigos de salida
// (0 bien, 1 error, 3 cancelado) y mismos textos, traducidos con la tabla de la app.
#include "../codec.hpp"
#include "../container.hpp"
#include "../dng.hpp"
#include "../signal.hpp"
#include "json.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cdng::worker {

struct Cancelled {};

// Traducción con app/i18n/es.json cuando CDNG_LANGUAGE=es.
std::string t(const std::string &source);
// Sustituye {name} por su valor, como str.format en los textos del Python.
std::string format(std::string text, const std::map<std::string, std::string> &values);

// Una línea JSON por evento en stdout, con flush (emit).
void emit(const char *kind, json::Object values = {});

// Comprueba los ficheros embebidos (resources() en Python) y la huella de la curva del Video Assist.
void checkResources(const std::filesystem::path &root);

// sequence(): una posición por frame de la toma, en orden; los números que faltan quedan como rutas vacías (huecos de
// hasta MaxMissingRun frames seguidos). Al menos dos DNG con el mismo prefijo.
constexpr int MaxMissingRun = 48;
// Si el fichero es un fotograma de la toma: extensión .dng (en mayúsculas o minúsculas) y no un «._nombre.DNG», los
// atributos que macOS deja al copiar a un disco exFAT o FAT (las tarjetas de cámara). En macOS no se ven, pero en Linux
// y Windows son ficheros normales y romperían la numeración de la toma.
bool isDngFrame(const std::filesystem::path &file);
std::vector<std::filesystem::path> sequence(const std::filesystem::path &folder);
// Igual, con los DNG ya listados (la exploración del lote no vuelve a listar la carpeta).
std::vector<std::filesystem::path> sequence(const std::filesystem::path &folder, std::vector<std::filesystem::path> files);
// Frames sustituidos por negro: como mucho este porcentaje de la toma (más indica una toma dañada, no un frame).
constexpr double MaxReplacedShare = 0.10;

Route routeFor(const Dng &dng);
std::string profileFor(Route route);   // id del perfil Camera Support que necesita la ruta ("" si ninguno)

// source_info: comprobaciones por frame y datos que viajan al clip.
struct SourceInfo {
    Route route = Route::Va;
    double gain = 1;                       // ganancia lineal antes del mapa de señal
    std::optional<int> sourceIso;
    int iso = 800;                         // ISO que declara el clip
    std::optional<int> kelvin, tint;       // As Shot; vacío en la ruta Pocket (el clip dice 5600/10)
    std::optional<std::pair<int, int>> defaultCrop;   // DefaultCropSize (50720)
    std::string profile;
};
SourceInfo sourceInfo(const Dng &dng, const OgModel &og);

// Recursos cargados una vez y compartidos entre hilos (todo const).
struct Engine {
    std::filesystem::path root;
    OgModel og;
    Codec codec;
    TemplateFrame tpl;
    std::unique_ptr<SignalMap> va, ogMap;
    mutable std::unique_ptr<SignalMap> pocket;   // tabla 3D de la ruta Pocket: se carga al primer uso
    mutable std::once_flag pocketOnce;
    explicit Engine(const std::filesystem::path &root);
    const SignalMap &map(Route route) const;
};

// develop(): DNG → imagen de la ruta recortada y reescalada a width × height → planos del decodificador.
Planes develop(const Engine &engine, const Dng &dng, const SourceInfo &info, int width, int height);
// Revelado orientativo para mostrar (dng_to_709 × 2^BaselineExposure, recorte y reescalado), en sRGB de 8 bits.
std::vector<uint8_t> displayImage(const Dng &dng, int width, int height);
// display_camera: RGB de cámara lineal → sRGB de 8 bits con la matriz del DNG.
std::vector<uint8_t> displayCamera(const RgbImage &camera, const Dng &dng, double baseline);
// Imagen PPM (P6) que la app lee con QImage::fromData.
std::vector<uint8_t> ppm(const std::vector<uint8_t> &rgb, int width, int height);
std::pair<int, int> fitInside(int width, int height, int maxWidth, int maxHeight);   // Image.thumbnail

// Opciones y tamaño de salida (encoding_options, output_size).
// Frames en paralelo: la app ofrece de 1 a los núcleos lógicos del Mac. Medido en un M3 Pro (12 núcleos, UHD, ruido
// Media, 5:1): 1 → 0,55 frames/s, 4 → 1,71, 8 → 2,31, 12 → 2,64; ≈ 0,45 GB de memoria por proceso.
constexpr int MaxWorkers = 32;
struct Options {
    int workers = 2; std::optional<int> q; double ratio = 5; double nrLuma = 0, nrChroma = 0;
    bool noiseSynthesis = true;   // false: campos de síntesis de ruido a 0/0, el descodificador no añade ruido
};
std::pair<int, int> outputSize(int requestWidth, int requestHeight, std::pair<int, int> source);

int convert(const std::filesystem::path &request);
int previewServer();

}  // namespace cdng::worker
