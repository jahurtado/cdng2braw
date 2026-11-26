#include "worker.hpp"

#include <simdjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <regex>

namespace cdng::worker {

namespace {

std::map<std::string, std::string> loadCatalog() {
    std::map<std::string, std::string> catalog;
    const auto root = resourceRoot();
    for (const auto &candidate : {root / "translations/es.json", root / "app/i18n/es.json"}) {
        if (!std::filesystem::is_regular_file(candidate)) continue;
        simdjson::ondemand::parser parser;
        auto text = simdjson::padded_string::load(candidate.string());
        if (text.error()) continue;
        auto document = parser.iterate(text.value_unsafe());
        for (auto field : document.get_object()) {
            std::string_view key = field.unescaped_key();
            std::string_view value = field.value().get_string();
            catalog.emplace(std::string(key), std::string(value));
        }
        break;
    }
    return catalog;
}

// SHA-256 pequeño para la huella de la curva del Video Assist (como hashlib en Python).
std::string sha256(const std::vector<uint8_t> &data) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m(data);
    const uint64_t bits = uint64_t(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (8 * i)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t block = 0; block < m.size(); block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(m[block + 4 * i]) << 24 | uint32_t(m[block + 4 * i + 1]) << 16 | uint32_t(m[block + 4 * i + 2]) << 8 | m[block + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}

float srgb(float v) {
    v = std::clamp(v, 0.f, 1.f);
    return v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - .055f;
}

}  // namespace

std::string t(const std::string &source) {
    const char *language = std::getenv("CDNG_LANGUAGE");
    if (!language || std::string(language) != "es") return source;
    static const auto catalog = loadCatalog();
    const auto found = catalog.find(source);
    return found == catalog.end() ? source : found->second;
}

std::string format(std::string text, const std::map<std::string, std::string> &values) {
    for (const auto &[key, value] : values) {
        const auto token = "{" + key + "}";
        for (std::size_t at; (at = text.find(token)) != std::string::npos;) text.replace(at, token.size(), value);
    }
    return text;
}

void emit(const char *kind, json::Object values) {
    static std::mutex mutex;
    const auto line = json::Object().str("event", kind).text();
    auto body = values.text();
    std::string out = line.substr(0, line.size() - 1) + (body.size() > 2 ? ", " + body.substr(1) : "}");
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << out << '\n' << std::flush;
}

void checkResources(const std::filesystem::path &root) {
    // Misma lista que tools/stage_converter.py (el paquete copia estos ficheros junto al ejecutable).
    static const char *resources[] = {
        "model/cam/orig.braw", "model/quant_steps.json", "model/vlc-table.json", "model/noise_synthesis.json",
        "model/video_assist_input_transform.json", "model/raw_to_709.json",
        "model/pocket_og_cinemadng_signal.json", "model/pocket_og_cinemadng_camera_support.json",
        "model/nr_normals_6000x64.f32", "model/pocket_inverse_gen4.bin", "model/pocket_forward_gen4.bin"};
    for (const char *name : resources)
        if (!std::filesystem::is_regular_file(root / name)) throw Error(t("The embedded calibration is damaged: ") + name);
    const char *name = "video_assist_input_transform.json";
    if (sha256(readFile(root / "model" / name)) != "f6cff6fcb3b5dbda17c00cb544ee9e73b420256dee3b840fc7bc1f6a5b057ffe")
        throw Error(t("The embedded calibration is damaged: ") + name);
}

bool isDngFrame(const std::filesystem::path &file) {
    const auto name = file.filename().string();
    if (name.rfind("._", 0) == 0) return false;
    auto extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return char(std::tolower((unsigned char)c)); });
    return extension == ".dng";
}

std::vector<std::filesystem::path> sequence(const std::filesystem::path &folder) {
    if (!std::filesystem::is_directory(folder)) throw Error(t("Select a folder containing a CinemaDNG sequence."));
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && isDngFrame(entry.path())) files.push_back(entry.path());
    }
    return sequence(folder, std::move(files));
}

std::vector<std::filesystem::path> sequence(const std::filesystem::path &folder, std::vector<std::filesystem::path> files) {
    std::error_code ec;
    if (files.size() < 2) throw Error(t("The folder must contain at least two DNG sequence frames."));
    // Una sola toma numerada: no se mezclan tomas ni se cambia el tiempo en silencio.
    static const std::regex numbered(R"((.*?)(\d+))");
    std::string prefix; bool first = true;
    std::vector<std::pair<long long, std::filesystem::path>> ordered;
    for (const auto &file : files) {
        std::smatch match; const auto stem = file.stem().string();
        if (!std::regex_match(stem, match, numbered) || (!first && match[1] != prefix))
            throw Error(t("The folder must contain a single consecutively numbered sequence."));
        prefix = match[1]; first = false;
        ordered.emplace_back(std::stoll(match[2]), file);
    }
    std::sort(ordered.begin(), ordered.end());
    // macOS lista mal algunas carpetas grandes por SMB: repite páginas del directorio y se salta otras (visto con una
    // toma de ~9500 frames: 10 000 entradas, solo 2000 nombres distintos). Si el listado repite un fichero o se queda
    // corto (existe el frame siguiente al último), la secuencia se rehace pidiendo cada número por su nombre.
    const auto nameFor = [&](long long number) {
        std::smatch match; const auto stem = ordered.front().second.stem().string();
        std::regex_match(stem, match, numbered);
        auto digits = std::to_string(number);
        if (digits.size() < std::size_t(match[2].length())) digits.insert(0, match[2].length() - digits.size(), '0');
        return folder / (prefix + digits + ordered.front().second.extension().string());
    };
    // También ante cualquier hueco: antes de rellenar con negro se confirma que el frame no existe de verdad.
    bool suspicious = false;
    for (std::size_t i = 1; i < ordered.size(); ++i) suspicious = suspicious || ordered[i].first - ordered[i - 1].first != 1;
    if (suspicious || std::filesystem::exists(nameFor(ordered.back().first + 1), ec)) {
        std::vector<std::pair<long long, std::filesystem::path>> probed;
        for (long long number = ordered.front().first, misses = 0; misses <= MaxMissingRun; ++number) {
            const auto path = nameFor(number);
            if (std::filesystem::is_regular_file(path, ec)) { probed.emplace_back(number, path); misses = 0; } else ++misses;
        }
        ordered = std::move(probed);
    }
    // Huecos de hasta MaxMissingRun frames: posiciones vacías que la conversión rellena con negro (conserva duración,
    // timecode y sincronía). Un hueco mayor o un número repetido indican otra cosa (tomas mezcladas): error.
    files.clear();
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (i) {
            const long long gap = ordered[i].first - ordered[i - 1].first;
            if (gap < 1 || gap - 1 > MaxMissingRun) throw Error(t("The DNG sequence has missing frames or duplicate numbers."));
            for (long long k = 1; k < gap; ++k) files.emplace_back();
        }
        files.push_back(ordered[i].second);
    }
    return files;
}

Route routeFor(const Dng &dng) {
    auto name = dng.cameraName();
    name.erase(0, name.find_first_not_of(" \t")); name.erase(name.find_last_not_of(" \t\0") + 1);
    auto upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](char c) { return char(std::toupper((unsigned char)c)); });
    if (upper.find("SIGMA FP") != std::string::npos) return Route::Va;
    return name == "Blackmagic Pocket Cinema Camera" ? Route::Og : Route::Pocket;
}

std::string profileFor(Route route) { return route == Route::Og ? "pocket-og" : ""; }

SourceInfo sourceInfo(const Dng &dng, const OgModel &og) {
    SourceInfo info;
    info.route = routeFor(dng);
    if (const auto crop = dng.numeric(50720); crop && crop->size() >= 2) info.defaultCrop = {int((*crop)[0]), int((*crop)[1])};
    if (info.route == Route::Og) {
        const auto shot = ogSourceInfo(dng, og);
        info.gain = 1; info.iso = shot.iso; info.kelvin = shot.kelvin; info.tint = shot.tint; info.profile = profileFor(info.route);
        return info;
    }
    const auto baseline = dng.numeric(50730);
    const double ev = baseline && !baseline->empty() ? (*baseline)[0] : 0.;
    if (info.route == Route::Va) {
        if (!baseline) throw Error("The Sigma DNG does not declare BaselineExposure.");
        if (!dng.numeric(50719) || !dng.numeric(50720)) throw Error("source must declare DefaultCropOrigin and DefaultCropSize");
        info.gain = std::pow(2., ev - 3.);   // la identidad fp L / ISO 800 espera la exposición de ISO 800
        const auto [kelvin, tint] = dng.asShot();
        info.kelvin = int(std::nearbyint(kelvin)); info.tint = int(std::nearbyint(tint));
        info.iso = 800;
    } else {
        info.gain = std::pow(2., ev);   // WB horneado: el clip debe decir 5600/10
        info.iso = 1250;
    }
    if (const auto iso = dng.numeric(34855); iso && !iso->empty() && (*iso)[0]) info.sourceIso = int((*iso)[0]);
    return info;
}

Engine::Engine(const std::filesystem::path &root_)
    : root(root_), og(loadOgModel(root_ / "model/pocket_og_cinemadng_signal.json")), codec(root_),
      tpl(readTemplate(root_ / "model/cam/orig.braw")) {
    va = videoAssistMap(root);
    ogMap = ogSignalMap(og);
}

const SignalMap &Engine::map(Route route) const {
    if (route == Route::Va) return *va;
    if (route == Route::Og) return *ogMap;
    std::call_once(pocketOnce, [this] { pocket = pocketMap(root); });
    return *pocket;
}

namespace {
void multiply(RgbImage &image, const Mat3 *matrix, double gain) {
    const float g = float(gain);
    for (std::size_t i = 0; i < image.data.size(); i += 3) {
        float *p = &image.data[i];
        if (matrix) {
            const auto &m = *matrix;
            const double r = p[0], gg = p[1], b = p[2];
            p[0] = float(m[0] * r + m[1] * gg + m[2] * b); p[1] = float(m[3] * r + m[4] * gg + m[5] * b); p[2] = float(m[6] * r + m[7] * gg + m[8] * b);
        }
        p[0] *= g; p[1] *= g; p[2] *= g;
    }
}
}  // namespace

Planes develop(const Engine &engine, const Dng &dng, const SourceInfo &info, int width, int height) {
    // Toda cámara cuyo mapa guarda los canales del sensor por separado va por el demosaico parcial, con el patrón Bayer
    // que traiga el DNG; las demás, por el revelado RGB.
    if (engine.map(info.route).channelCodec())
        return bayerPlanesSized(dng.cameraBayer(), engine.map(info.route), info.gain, width, height);
    RgbImage rgb = dng.cameraRgb();
    if (info.route == Route::Pocket) {
        const auto color = dng.color();
        const Mat3 matrix = cameraTo709(color, color.asShotNeutral);
        multiply(rgb, &matrix, info.gain);
        rgb = cropSensor(rgb, dng);
    } else {
        rgb = cropSensor(rgb, dng);
        if (info.gain != 1) multiply(rgb, nullptr, info.gain);
    }
    rgb = resizeLinear(rgb, width, height);
    return imagePlanes(rgb, engine.map(info.route));
}

std::vector<uint8_t> displayImage(const Dng &dng, int width, int height) {
    const auto color = dng.color();
    const Mat3 matrix = cameraTo709(color, color.asShotNeutral);
    const auto baseline = dng.numeric(50730);
    RgbImage rgb = dng.cameraRgb();
    multiply(rgb, &matrix, std::pow(2., baseline && !baseline->empty() ? (*baseline)[0] : 0.));
    rgb = resizeLinear(cropSensor(rgb, dng), width, height);
    std::vector<uint8_t> out(rgb.data.size());
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = uint8_t(std::nearbyint(srgb(rgb.data[i]) * 255));
    return out;
}

std::vector<uint8_t> displayCamera(const RgbImage &camera, const Dng &dng, double baseline) {
    const auto color = dng.color();
    const Mat3 m = cameraTo709(color, color.asShotNeutral);
    std::vector<uint8_t> out(camera.data.size());
    for (std::size_t i = 0; i < out.size(); i += 3) {
        const double r = camera.data[i], g = camera.data[i + 1], b = camera.data[i + 2];
        for (int c = 0; c < 3; ++c)
            out[i + c] = uint8_t(std::nearbyint(srgb(float((m[3 * c] * r + m[3 * c + 1] * g + m[3 * c + 2] * b) * baseline)) * 255));
    }
    return out;
}

std::vector<uint8_t> ppm(const std::vector<uint8_t> &rgb, int width, int height) {
    const auto header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    std::vector<uint8_t> out(header.begin(), header.end());
    out.insert(out.end(), rgb.begin(), rgb.end());
    return out;
}

std::pair<int, int> fitInside(int width, int height, int maxWidth, int maxHeight) {
    if (width <= maxWidth && height <= maxHeight) return {width, height};
    const double scale = std::min(double(maxWidth) / width, double(maxHeight) / height);
    return {std::max(1, int(std::lround(width * scale))), std::max(1, int(std::lround(height * scale)))};
}

std::pair<int, int> outputSize(int width, int height, std::pair<int, int> source) {
    const auto [sw, sh] = source;
    if (sw < 1 || sh < 1) throw Error(t("The source does not declare a usable size."));
    // Redondeo a par hacia el valor más cercano: el códec trabaja en cuartetos y un lado impar no vale.
    const auto even = [](double v) { const long long n = std::llround(v / 2) * 2; return int(std::max(2LL, n)); };
    if (width == 0 && height == 0) { width = sw; height = sh; }
    // Con un solo lado, el otro sale de la proporción del material. Así «3K» significa 3072 de ancho en
    // cualquier formato (16:9 → 3072×1728; open gate 3:2 → 3072×2048) y el encuadre no se recorta: pedir una
    // pareja fija sí recorta, porque más abajo la señal se recorta centrada a la proporción pedida.
    // El lado deducido se limita al del original: el redondeo a par puede pasarse por un píxel cuando el
    // original tiene un lado impar. Un tamaño pedido a mano que no cabe sigue dando error, no se recorta solo.
    else if (height == 0) height = std::min(even(double(width) * sh / sw), sh - sh % 2);
    else if (width == 0) width = std::min(even(double(height) * sw / sh), sw - sw % 2);
    if (width % 2 || height % 2 || width < 1280 || height < 720 || width > sw || height > sh)
        throw Error(t("Resolution must be even, at least 1280×720, and cannot exceed the source."));
    return {width, height};
}

}  // namespace cdng::worker
