#include "app/experiment_form.hpp"
#include "experiment_input_detail.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <string_view>

namespace conway::app {
namespace {
constexpr auto index(ExperimentField field) { return static_cast<std::size_t>(field); }
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
template<class T> bool integer(std::string_view text, T& value) {
    text = trim(text);
    if (text.empty() || text.front() < '0' || text.front() > '9') return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool density(std::string_view text, int& value) {
    text = trim(text);
    const auto point = text.find_first_of(".,");
    auto whole = text.substr(0, point);
    auto fraction = point == text.npos ? std::string_view{} : text.substr(point + 1);
    if (point != text.npos && fraction.empty()) return false;
    while (!fraction.empty() && fraction.back() == '0') fraction.remove_suffix(1);
    unsigned percent = 0, decimals = 0;
    if (!integer(whole, percent) || percent > 100 || fraction.size() > 2) return false;
    if (!fraction.empty() && !integer(fraction, decimals)) return false;
    // No floating-point rounding: one percent is exactly 100 generator units.
    if (fraction.size() == 1) decimals *= 10;
    value = static_cast<int>(percent * 100 + decimals);
    return value <= 10000;
}
} // namespace

ExperimentFormDraft standardExperimentForm(bool scaling, int currentThreads, bool hasPattern) {
    return {hasPattern ? 0 : 1, {"1000", "1000", "30", "42", "20", "1", "5",
                                scaling ? "1, 2, 4, 8" : std::to_string(currentThreads), ""}};
}

ExperimentFormValidation validateExperimentForm(const ExperimentFormDraft& draft, bool scaling,
                                                const InputData* loaded) {
    ExperimentFormValidation result{};
    ExperimentFormConfig parsed{}; // Existing default order policies and scaling budget.
    auto error = [&](ExperimentField field, std::string text) {
        result.errors[index(field)] = std::move(text);
    };
    auto parseInt = [&](ExperimentField field, int& value, bool zeroAllowed = false) {
        if (!integer(draft.values[index(field)], value) || value < (zeroAllowed ? 0 : 1))
            error(field, std::string("Entero entre ") + (zeroAllowed ? "0" : "1") + " y " +
                  std::to_string(std::numeric_limits<int>::max()) + ".");
    };
    parseInt(ExperimentField::Generations, parsed.comparison.generations);
    parseInt(ExperimentField::Warmups, parsed.comparison.warmups, true);
    parseInt(ExperimentField::Repetitions, parsed.comparison.repetitions);
    parsed.scaling.generations = parsed.comparison.generations;
    parsed.scaling.warmups = parsed.comparison.warmups;
    parsed.scaling.repetitions = parsed.comparison.repetitions;
    if (!scaling) {
        parseInt(ExperimentField::Threads, parsed.comparison.requestedThreads);
    } else {
        parsed.scaling.requestedThreads.clear();
        auto remaining = trim(draft.values[index(ExperimentField::Threads)]);
        std::set<int> seen;
        while (!remaining.empty()) {
            const auto comma = remaining.find(',');
            int threads = 0;
            if (!integer(remaining.substr(0, comma), threads) || threads <= 0) {
                error(ExperimentField::Threads, "Lista de enteros positivos separados por comas, hasta " +
                      std::to_string(std::numeric_limits<int>::max()) + ".");
                break;
            }
            if (!seen.insert(threads).second) {
                error(ExperimentField::Threads, "La lista contiene hilos duplicados.");
                break;
            }
            parsed.scaling.requestedThreads.push_back(threads);
            if (comma == remaining.npos) break;
            remaining = trim(remaining.substr(comma + 1));
            if (remaining.empty()) error(ExperimentField::Threads, "Falta un número después de la coma.");
        }
        if (seen.empty()) error(ExperimentField::Threads, "Indica al menos un número de hilos.");
    }
    std::size_t width = 0, height = 0, inputCapacity = 0;
    if (draft.source == 1) {
        input::GenerationConfig generated{};
        if (!integer(draft.values[index(ExperimentField::Width)], generated.width) || !generated.width)
            error(ExperimentField::Width, "Ancho entero positivo representable.");
        if (!integer(draft.values[index(ExperimentField::Height)], generated.height) || !generated.height)
            error(ExperimentField::Height, "Alto entero positivo representable.");
        if (!density(draft.values[index(ExperimentField::Density)], generated.density))
            error(ExperimentField::Density, "Entre 0 y 100 %, precisión exacta de 0,01 %. Use punto o coma.");
        if (!integer(draft.values[index(ExperimentField::Seed)], generated.seed))
            error(ExperimentField::Seed, "Semilla entera entre 0 y 4294967295.");
        if (generated.height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
            error(ExperimentField::Height, "Alto fuera del rango seguro de índices del motor.");
        width = generated.width;
        height = result.errors[index(ExperimentField::Height)].empty() ? generated.height : 0;
        parsed.generated = generated;
    } else if (draft.source == 0 && loaded) {
        try {
            (void)detail::validateInput(*loaded);
            width = loaded->width;
            height = loaded->height;
            inputCapacity = loaded->cells.capacity();
        } catch (const std::exception&) {
            error(ExperimentField::Source, "El patrón cargado no es una entrada válida y segura.");
        }
    } else {
        error(ExperimentField::Source, "Sin patrón cargado. Carga uno desde el catálogo o elige tablero generado.");
    }
    // No public, allocation-free generator/scaling shape validator exists.
    // Reuse checked arithmetic and loaded-input validation. Mirror only the
    // payload contracts of generateInput and scaling validateConfig/execute.
    // Keep these calculations covered by boundary tests; never call executors.
    std::size_t padded = 0;
    if (width && height) {
        try {
            if (height >= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
                throw std::length_error("index");
            const auto count = detail::checkedMultiply(width, height);
            padded = detail::checkedMultiply(detail::checkedAdd(width, 2), detail::checkedAdd(height, 2));
            const auto capacity = std::vector<std::uint8_t>{}.max_size();
            if (count > capacity || padded > capacity) throw std::length_error("capacity");
            if (draft.source == 1) {
                inputCapacity = count;
                if (detail::checkedAdd(count, detail::checkedMultiply(2, padded)) > input::GenerationLimits{}.max_memory_bytes)
                    throw std::length_error("budget");
            }
        } catch (const std::length_error&) {
            padded = 0;
            error(draft.source == 1 ? ExperimentField::Width : ExperimentField::Source,
                  "Dimensiones inseguras: desbordamiento, índices, capacidad o límite de memoria de 256 MiB.");
        }
    }
    if (result.errors[index(ExperimentField::Repetitions)].empty()
        && result.errors[index(ExperimentField::Warmups)].empty()
        && result.errors[index(ExperimentField::Threads)].empty()) {
        try {
            using detail::checkedAdd;
            using detail::checkedMultiply;
            const auto repetitions = static_cast<std::size_t>(parsed.comparison.repetitions);
            const auto warmups = static_cast<std::size_t>(parsed.comparison.warmups);
            const auto pairs = checkedMultiply(repetitions, 2);
            if (!scaling) {
                if (pairs > std::vector<ExperimentSample>{}.max_size()) throw std::length_error("capacity");
                (void)checkedAdd(1, checkedAdd(pairs, checkedMultiply(warmups, 2)));
            } else {
                const auto threads = parsed.scaling.requestedThreads.size();
                if (threads >= static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("index");
                const auto configurations = checkedAdd(threads, 1);
                const auto samples = checkedMultiply(configurations, repetitions);
                (void)checkedAdd(1, checkedAdd(samples, checkedMultiply(configurations, warmups)));
                if (samples > std::vector<ScalingSample>{}.max_size()
                    || threads > std::vector<ScalingPoint>{}.max_size()
                    || pairs > std::vector<ExperimentSample>{}.max_size()) throw std::length_error("capacity");
                auto bytes = checkedAdd(sizeof(ScalingResult), checkedMultiply(samples, sizeof(ScalingSample)));
                bytes = checkedAdd(bytes, checkedMultiply(threads, sizeof(ScalingPoint)));
                bytes = checkedAdd(bytes, checkedMultiply(checkedAdd(parsed.scaling.requestedThreads.capacity(),
                                          checkedMultiply(threads, 2)), sizeof(std::int64_t)));
                bytes = checkedAdd(bytes, checkedMultiply(pairs, sizeof(ExperimentSample)));
                bytes = checkedAdd(bytes, checkedMultiply(checkedMultiply(repetitions, 3), sizeof(double)));
                if (bytes > parsed.scaling.maxMemoryBytes) throw std::length_error("budget");
                if (padded) bytes = checkedAdd(bytes, checkedAdd(inputCapacity, checkedMultiply(4, padded)));
                if (parsed.generated) bytes = checkedAdd(bytes, checkedAdd(input::kGeneratorVersion.size(), 1));
                if (bytes > parsed.scaling.maxMemoryBytes)
                    error(draft.source == 1 ? ExperimentField::Width : ExperimentField::Source,
                          "Entrada y mediciones superan 256 MiB; reduce dimensiones o repeticiones.");
            }
        } catch (const std::length_error&) {
            error(ExperimentField::Repetitions, "Configuración excede la capacidad o el presupuesto de memoria del experimento.");
        }
    }
    if (std::all_of(result.errors.begin(), result.errors.end(), [](const auto& e) { return e.empty(); }))
        result.config = std::move(parsed);
    return result;
}
} // namespace conway::app
