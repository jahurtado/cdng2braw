#include "worker.hpp"

#include <simdjson.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <thread>
#include <cstring>
#include <iostream>

namespace cdng::worker {

namespace {

// unsigned char: con char con signo, un byte >= 0x80 llega negativo a tolower y es comportamiento indefinido.
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(std::tolower((unsigned char)c)); });
    return s;
}

// El comparador guarda el frame revelado mientras no cambien carpeta, frame o tamaño (noise_cache en Python).
struct NoiseCache {
    std::string folder; int index = -1; std::pair<int, int> size{};
    Route route = Route::Va;
    // El original se revela siempre a su resolución nativa; el resultado, a la de salida (`size`).
    std::pair<int, int> nativeSize{};
    Planes native, before;
    Planes original, filtered;
    double luma = -1, chroma = -1;
    // Resultado comprimido (filtrado → codificado → decodificado) para el ajuste de compresión `compression`.
    std::string compression;
    Planes decoded;
    int q = 0;
    std::size_t bytes = 0;
    std::vector<uint8_t> overview; int overviewWidth = 0, overviewHeight = 0;
};

// Audio de la carpeta: descripción para la app y si la conversión lo incluirá (un único WAV PCM a 48 kHz).
std::pair<std::string, bool> audioDescription(const std::filesystem::path &folder) {
    std::vector<std::filesystem::path> wavs;
    for (const auto &entry : std::filesystem::directory_iterator(folder))
        if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".wav") wavs.push_back(entry.path());
    std::sort(wavs.begin(), wavs.end()); wavs.erase(std::unique(wavs.begin(), wavs.end()), wavs.end());   // SMB repite entradas (ver sequence)
    if (wavs.empty()) return {t("No WAV"), false};
    if (wavs.size() > 1) return {format(t("{count} WAV files · not included (ambiguous)"), {{"count", std::to_string(wavs.size())}}), false};
    // Frecuencia y profundidad del bloque fmt de la cabecera (lo que da el módulo wave de Python).
    FILE *file = std::fopen(wavs.front().string().c_str(), "rb");
    if (!file) return {t("WAV present"), false};
    uint8_t header[4096] = {};
    const std::size_t n = std::fread(header, 1, sizeof header, file);
    std::fclose(file);
    if (n < 12 || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return {t("WAV present"), false};
    for (std::size_t at = 12; at + 8 <= n;) {
        const uint32_t size = uint32_t(header[at + 4]) | uint32_t(header[at + 5]) << 8 | uint32_t(header[at + 6]) << 16 | uint32_t(header[at + 7]) << 24;
        if (!std::memcmp(header + at, "fmt ", 4) && at + 24 <= n) {
            const uint32_t rate = uint32_t(header[at + 12]) | uint32_t(header[at + 13]) << 8 | uint32_t(header[at + 14]) << 16 | uint32_t(header[at + 15]) << 24;
            const int bits = header[at + 22] | header[at + 23] << 8;
            char text[64]; std::snprintf(text, sizeof text, "WAV %g kHz · %d bit", rate / 1000., (bits + 7) / 8 * 8);
            const bool ok = rate == 48000 && (bits == 16 || bits == 24 || bits == 32);
            return {format(t(ok ? "{description} · included in the BRAW" : "{description} · not included (48 kHz only)"), {{"description", text}}), ok};
        }
        at += 8 + size + (size & 1);
    }
    return {t("WAV present"), false};
}

std::string routeName(Route route) { return route == Route::Va ? "va" : route == Route::Og ? "og" : "pocket"; }

// Datos de una toma para la lista del lote: solo etiquetas del primer frame existente (lectura parcial del DNG).
std::string scanClip(const std::filesystem::path &folder, std::vector<std::filesystem::path> dngs, const Engine &engine) {
    const auto dngCount = dngs.size();
    json::Object clip;
    clip.str("path", folder.string()).str("name", folder.filename().string());
    try {
        const auto slots = sequence(folder, std::move(dngs));
        const auto missing = std::count_if(slots.begin(), slots.end(), [](const auto &p) { return p.empty(); });
        const Dng dng(slots.front(), true);
        const auto info = sourceInfo(dng, engine.og);
        const auto rate = dng.frameRate();
        if (!rate) throw Error(t("The DNG does not declare a FrameRate."));
        const auto [width, height] = info.defaultCrop.value_or(dng.rawSize());
        const auto audio = audioDescription(folder);
        const double fps = double(rate->num) / rate->den;
        clip.integer("frames", (long long)slots.size()).integer("missing", (long long)missing).str("camera", t(dng.cameraName()))
            .str("route", routeName(info.route)).raw("profile", info.profile.empty() ? "null" : json::quote(info.profile))
            .integer("width", width).integer("height", height).num("fps", fps).num("duration", slots.size() / fps)
            .str("audio", audio.first).boolean("audio_include", audio.second).boolean("compatible", true).str("compatibility", "");
    } catch (const std::exception &error) {
        clip.integer("frames", (long long)dngCount).boolean("compatible", false).str("compatibility", t(error.what()));
    }
    return clip.text();
}

// Lote: cada carpeta bajo `root` (incluida ella misma) que contenga DNG es una toma. Se exploran varias a la vez
// (en red, el tiempo se va en esperar al disco) y cada una se anuncia con «scan_clip» en cuanto está; al final,
// «scan» con todas en orden. Devuelve el array JSON ordenado por ruta.
std::string scanClips(const std::filesystem::path &root, const Engine &engine, long long id) {
    if (!std::filesystem::is_directory(root)) throw Error(t("Select a folder containing CinemaDNG takes."));
    // Una sola pasada por carpeta: a la vez se cuentan sus DNG y se anotan sus subcarpetas (por SMB, listar una toma
    // de miles de frames es lo que cuesta). Los nombres se deduplican: SMB puede repetir entradas (ver sequence).
    std::vector<std::pair<std::filesystem::path, std::vector<std::filesystem::path>>> takes;
    std::vector<std::filesystem::path> pending{root};
    std::error_code ec;
    while (!pending.empty()) {
        const auto folder = pending.back(); pending.pop_back();
        std::set<std::string> dngs, subfolders;
        for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            const auto name = it->path().filename().string();
            if (it->is_directory(ec)) { if (!name.empty() && name[0] != '.') subfolders.insert(name); }   // ocultas y temporales fuera
            else if (isDngFrame(it->path())) dngs.insert(name);
        }
        if (!dngs.empty()) {
            std::vector<std::filesystem::path> paths;
            for (const auto &name : dngs) paths.push_back(folder / name);
            takes.emplace_back(folder, std::move(paths));
        }
        for (const auto &name : subfolders) pending.push_back(folder / name);
    }
    std::sort(takes.begin(), takes.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::string> results(takes.size());
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (std::size_t i; (i = next++) < takes.size();) {
            results[i] = scanClip(takes[i].first, takes[i].second, engine);
            emit("scan_clip", json::Object().integer("id", id).integer("index", (long long)i).integer("total", (long long)takes.size()).raw("clip", results[i]));
        }
    };
    std::vector<std::thread> threads;
    for (unsigned k = 0; k < std::min<std::size_t>(6, takes.size()); ++k) threads.emplace_back(work);
    for (auto &thread : threads) thread.join();
    std::string out = "[";
    for (const auto &r : results) out += (out.size() > 1 ? ", " : "") + r;
    return out + "]";
}

std::string imageField(const std::vector<uint8_t> &rgb, int width, int height) { return json::base64(ppm(rgb, width, height)); }

}  // namespace

int previewServer() {
    const auto root = resourceRoot();
    checkResources(root);
    // El comparador de ruido rinde un solo fotograma, así que aquí sí interesa repartirlo entre los núcleos de la
    // máquina (al convertir no: allí el reparto es por fotograma). El resultado no cambia con el número de hilos.
    // CDNG_NR_THREADS lo fuerza, para medir o para dejar la máquina libre.
    int threads = int(std::thread::hardware_concurrency());
    if (const char *env = std::getenv("CDNG_NR_THREADS")) {
        const int wanted = std::atoi(env);
        if (wanted > 0) threads = wanted;
    }
    setNoiseThreads(threads > 0 ? threads : 1);
    const Engine engine(root);
    std::string currentFolder;
    std::vector<std::filesystem::path> files;
    NoiseCache noise;
    std::string line;
    simdjson::ondemand::parser parser;
    while (std::getline(std::cin, line)) {
        long long id = -1;
        try {
            simdjson::padded_string padded(line);
            auto document = parser.iterate(padded);
            simdjson::ondemand::object request = document.get_object();
            auto get = [&](const char *key, double fallback) {
                double d; int64_t i;
                if (!request.find_field_unordered(key).get_double().get(d)) return d;
                if (!request.find_field_unordered(key).get_int64().get(i)) return double(i);
                return fallback;
            };
            auto getText = [&](const char *key) {
                std::string_view v;
                return request.find_field_unordered(key).get_string().get(v) ? std::string() : std::string(v);
            };
            id = (long long)get("id", -1);
            if (getText("action") == "scan") {
                emit("scan", json::Object().integer("id", id).raw("clips", scanClips(std::filesystem::weakly_canonical(getText("input")), engine, id)));
                continue;
            }
            const auto folder = std::filesystem::weakly_canonical(getText("input")).string();
            if (folder != currentFolder) { files = sequence(folder); currentFolder = folder; }
            const int index = int(get("frame", 0));
            if (index < 0 || index >= int(files.size())) throw Error(t("Frame is outside the sequence."));
            const auto &path = files[index];
            if (path.empty()) throw Error(t("This frame is missing from the sequence; the conversion fills it with black."));
            const Dng dng(path);
            const auto crop = dng.numeric(50720);
            const auto raw = dng.rawSize();
            const std::pair<int, int> source = crop && crop->size() >= 2 ? std::pair<int, int>{int((*crop)[0]), int((*crop)[1])} : raw;
            // Una toma con un tamaño no admitido se puede seguir inspeccionando: el mínimo es del conversor.
            std::pair<int, int> size = source; std::string geometryError;
            try { size = outputSize(int(get("width", 0)), int(get("height", 0)), source); } catch (const Error &error) { geometryError = error.what(); }
            const auto baseline = dng.numeric(50730);
            const double ev = baseline && !baseline->empty() ? (*baseline)[0] : 0.;

            if (getText("action") == "noise") {
                if (!geometryError.empty()) throw Error(geometryError);
                const double luma = get("nr_luma", 0), chroma = get("nr_chroma", 0);
                for (double v : {luma, chroma})
                    if (!std::isfinite(v) || v < 0 || v > 100) throw Error(t("Noise reduction must be between 0 and 100 % for luma and chroma."));
                // Exposición solo de visualización: se aplica en lineal sobre el resultado ya filtrado, como levantar
                // sombras en Resolve después de la reducción de ruido. No cambia la conversión.
                const double lift = std::clamp(get("exposure", 0), 0., 6.);
                const auto info = sourceInfo(dng, engine.og);
                const auto nativeSize = outputSize(0, 0, source);
                if (noise.folder != folder || noise.index != index) {
                    noise = NoiseCache{};
                    noise.folder = folder; noise.index = index; noise.route = info.route; noise.nativeSize = nativeSize;
                    noise.native = develop(engine, dng, info, nativeSize.first, nativeSize.second);
                    noise.before = noise.native;
                    finishPlanes(noise.before, engine.map(info.route), info.route, 0, 0);
                    std::tie(noise.overviewWidth, noise.overviewHeight) = fitInside(nativeSize.first, nativeSize.second, 960, 540);
                    noise.overview = displayImage(dng, noise.overviewWidth, noise.overviewHeight);
                }
                if (noise.size != size) {
                    noise.size = size; noise.luma = noise.chroma = -1;
                    noise.original = size == nativeSize ? noise.native : develop(engine, dng, info, size.first, size.second);
                }
                if (noise.luma != luma || noise.chroma != chroma) {
                    noise.filtered = noise.original;
                    finishPlanes(noise.filtered, engine.map(info.route), info.route, luma / 100, chroma / 100);
                    noise.luma = luma; noise.chroma = chroma; noise.compression.clear();
                }
                // Compresión: «none», «ratio» (N:1, con el control de tasa de la conversión) o «quality» (q fijo).
                const auto mode = getText("compression");
                const double ratio = get("ratio", 5), quality = get("q", 4);
                std::string setting = mode == "ratio" ? "ratio " + json::number(ratio) : mode == "quality" ? "q " + json::number(quality) : "";
                if (mode == "ratio" && (!std::isfinite(ratio) || ratio < 3 || ratio > 12)) throw Error(t("Unsupported compression: use q 0…24 or ratio 3:1…12:1."));
                if (mode == "quality" && (!std::isfinite(quality) || quality < QMin || quality > QMax)) throw Error(t("Unsupported compression: use q 0…24 or ratio 3:1…12:1."));
                if (!setting.empty() && noise.compression != setting) {
                    const Geometry geometry(size.first, size.second);
                    const auto canvas = canvasPlanes(noise.filtered, geometry);
                    const auto frame = mode == "quality" ? engine.codec.encodeFixed(canvas, geometry, int(quality))
                        : engine.codec.encodeRateControlled(canvas, geometry, std::size_t(double(size.first) * size.second * 12 / 8 / ratio), 8);
                    const auto rebuilt = engine.codec.reconstruct(canvas, frame.q, geometry);
                    noise.decoded = decodedPlanes(rebuilt, geometry);
                    noise.q = frame.q; noise.bytes = frame.size; noise.compression = setting;
                }
                const Planes &result = setting.empty() ? noise.filtered : noise.decoded;
                int x = size.first / 2 - 128, y = size.second / 2 - 128, rw = 256, rh = 256;
                auto roi = request.find_field_unordered("roi").get_array();
                if (!roi.error()) {
                    std::vector<int> values;
                    for (auto v : roi.value_unsafe()) { double d; int64_t i; values.push_back(!v.get_double().get(d) ? int(d) : !v.get_int64().get(i) ? int(i) : 0); }
                    if (values.size() == 4) { x = values[0]; y = values[1]; rw = values[2]; rh = values[3]; }
                }
                // El recorte va en píxeles del original. Si la salida cambia de proporción, la conversión recorta al
                // centro (resizeLinear): el recorte se mantiene dentro de esa zona. En el resultado se toma la misma
                // zona de la escena a su escala; si la salida es menor, su recorte tiene menos píxeles.
                int cx = 0, cy = 0, cw = nativeSize.first, ch = nativeSize.second;
                if ((long long)cw * size.second != (long long)ch * size.first) {
                    if ((long long)cw * size.second > (long long)ch * size.first) { cw = int(std::nearbyint(double(ch) * size.first / size.second)); cx = (nativeSize.first - cw) / 2; }
                    else { ch = int(std::nearbyint(double(cw) * size.second / size.first)); cy = (nativeSize.second - ch) / 2; }
                }
                rw = std::min({std::max(rw, 64), 512, cw}); rh = std::min({std::max(rh, 64), 512, ch});
                x = std::clamp(x, cx, cx + cw - rw); y = std::clamp(y, cy, cy + ch - rh);
                const double sx = double(size.first) / cw, sy = double(size.second) / ch;
                const int ow = std::clamp(int(std::nearbyint(rw * sx)), 1, size.first), oh = std::clamp(int(std::nearbyint(rh * sy)), 1, size.second);
                const int ox = std::clamp(int(std::nearbyint((x - cx) * sx)), 0, size.first - ow), oy = std::clamp(int(std::nearbyint((y - cy) * sy)), 0, size.second - oh);
                const double gain = std::pow(2., ev + lift);
                auto shown = [&](const Planes &planes, int x, int y, int rw, int rh) {
                    const auto camera = planesCropToCamera(planes, x, y, rw, rh, noise.route, root, &engine.og);
                    if (noise.route != Route::Pocket) return displayCamera(camera, dng, gain);
                    std::vector<uint8_t> out(camera.data.size());
                    for (std::size_t i = 0; i < out.size(); ++i) {
                        const float v = std::clamp(camera.data[i] * float(std::pow(2., lift)), 0.f, 1.f);
                        out[i] = uint8_t(std::nearbyint((v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - .055f) * 255));
                    }
                    return out;
                };
                emit("noise", json::Object()
                    .integer("id", id).integer("frame", index).integer("width", size.first).integer("height", size.second)
                    .integer("native_width", nativeSize.first).integer("native_height", nativeSize.second)
                    .raw("roi", "[" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(rw) + ", " + std::to_string(rh) + "]")
                    .raw("result_roi", "[" + std::to_string(ox) + ", " + std::to_string(oy) + ", " + std::to_string(ow) + ", " + std::to_string(oh) + "]")
                    .raw("nr", "[" + json::number(luma) + ", " + json::number(chroma) + "]").num("exposure", lift)
                    .raw("compression", setting.empty() ? "null" : json::Object().integer("q", noise.q).integer("bytes", (long long)noise.bytes)
                        .num("ratio", double(size.first) * size.second * 12 / 8 / double(noise.bytes)).text())
                    .str("before", imageField(shown(noise.before, x, y, rw, rh), rw, rh)).str("after", imageField(shown(result, ox, oy, ow, oh), ow, oh))
                    .str("overview", imageField(noise.overview, noise.overviewWidth, noise.overviewHeight)));
                continue;
            }

            // Metadatos de la toma aunque su calibración no esté admitida.
            bool compatible = true; std::string compatibility;
            std::optional<SourceInfo> info;
            try {
                info = sourceInfo(dng, engine.og);
                if (!dng.frameRate()) throw Error(t("The DNG does not declare a FrameRate."));
                compatibility = t("Frame checked; conversion validates the complete sequence.");
            } catch (const Error &error) {
                compatible = false; compatibility = t(error.what());
            }
            if (!geometryError.empty()) {
                compatible = false;
                compatibility = (info ? std::string() : compatibility + " ") + geometryError;
            }
            const auto [tw, th] = fitInside(size.first, size.second, 960, 640);
            const auto image = displayImage(dng, tw, th);
            const auto white = dng.numeric(50717);
            const double whiteLevel = white && !white->empty() ? (*white)[0] : 65535;
            auto first = [&](uint16_t tag) { const auto v = dng.numeric(tag); return v && !v->empty() ? json::number((*v)[0]) : std::string("null"); };
            const auto audio = audioDescription(folder);
            json::Object event;
            event.integer("id", id).integer("frame", index).integer("total", (long long)files.size())
                .str("name", path.filename().string()).str("camera", t(dng.cameraName()))
                .raw("route", info ? json::quote(routeName(info->route)) : "null")
                .raw("profile", info && !info->profile.empty() ? json::quote(info->profile) : "null")
                .integer("width", source.first).integer("height", source.second)
                .integer("sensor_width", raw.first).integer("sensor_height", raw.second)
                .integer("preview_width", size.first).integer("preview_height", size.second)
                .integer("bits", (long long)std::ceil(std::log2(whiteLevel + 1)))
                .raw("iso", first(34855)).raw("exposure", first(33434)).num("baseline_ev", ev).raw("fps", first(51044))
                .raw("kelvin", info && info->kelvin ? std::to_string(*info->kelvin) : "null")
                .raw("tint", info && info->tint ? std::to_string(*info->tint) : "null")
                .str("audio", audio.first).boolean("audio_include", audio.second).boolean("compatible", compatible).str("compatibility", compatibility)
                .str("jpeg", imageField(image, tw, th));
            emit("preview", std::move(event));
        } catch (const std::exception &error) {
            emit("preview_error", json::Object().integer("id", id).str("message", t(error.what())));
        }
    }
    return 0;
}

}  // namespace cdng::worker
