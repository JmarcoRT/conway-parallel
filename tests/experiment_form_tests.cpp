#include "app/experiment_form.hpp"
#include <iostream>
#include <limits>

int main() {
    using namespace conway::app;
    int failures = 0;
    const auto check = [&](bool condition, const char* label) {
        if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
    };
    auto draft = standardExperimentForm(false, 37, false);
    const auto original = draft;
    auto validate = [&](bool scaling = false, const InputData* loaded = nullptr) {
        return validateExperimentForm(draft, scaling, loaded);
    };
    auto valid = validate();
    check(valid.config && valid.config->generated->width == 1000
        && valid.config->generated->height == 1000 && valid.config->generated->density == 3000
        && valid.config->generated->seed == 42 && valid.config->comparison.generations == 20
        && valid.config->comparison.warmups == 1 && valid.config->comparison.repetitions == 5
        && valid.config->comparison.requestedThreads == 37
        && valid.config->comparison.order == MeasurementOrder::AlternatingSequentialFirst,
        "comparison defaults match contracts without board allocation");
    check(draft.values == original.values && draft.source == original.source, "validation preserves draft");
    auto rejected = [&](ExperimentField field, const std::string& text, bool scaling = false) {
        draft = standardExperimentForm(scaling, 2, false);
        draft.values[static_cast<std::size_t>(field)] = text;
        const auto result = validate(scaling);
        check(!result.config && !result.errors[static_cast<std::size_t>(field)].empty(), "invalid field has local error");
    };
    for (auto field : {ExperimentField::Width, ExperimentField::Height, ExperimentField::Seed,
                       ExperimentField::Generations, ExperimentField::Warmups,
                       ExperimentField::Repetitions, ExperimentField::Threads})
        for (const auto& text : {"", "-1", "+1", "1x", "184467440737095516160"}) rejected(field, text);
    for (auto field : {ExperimentField::Width, ExperimentField::Height, ExperimentField::Generations,
                       ExperimentField::Repetitions, ExperimentField::Threads}) rejected(field, "0");
    for (const auto& text : {"-1", "100.01", "30.001", "nan", "inf", "30.", "1e2", "1.2.3", ""})
        rejected(ExperimentField::Density, text);
    rejected(ExperimentField::Seed, "4294967296");
    for (auto field : {ExperimentField::Generations, ExperimentField::Warmups,
                       ExperimentField::Repetitions, ExperimentField::Threads})
        rejected(field, std::to_string(static_cast<unsigned long long>(std::numeric_limits<int>::max()) + 1));
    for (const auto& text : {"", "1,1", "1, 2, 1", "0,2", "-1,2", "1,", ",1", "1,,2", "1 2", "2147483648"})
        rejected(ExperimentField::Threads, text, true);
    for (const auto& [text, expected] : std::initializer_list<std::pair<std::string, int>>{
             {"0", 0}, {"100", 10000}, {"30.25", 3025}, {"30,25", 3025},
             {" 0.01 ", 1}, {"30.000", 3000}, {"99.9900", 9999}}) {
        draft = original;
        draft.values[2] = text;
        const auto result = validate();
        check(result.config && result.config->generated->density == expected, "exact percentage conversion");
    }
    draft = original;
    draft.values[0] = draft.values[1] = "1";
    draft.values[3] = "0";
    draft.values[5] = "0";
    draft.values[7] = std::to_string(std::numeric_limits<int>::max());
    valid = validate();
    check(valid.config && valid.config->generated->seed == 0 && valid.config->comparison.warmups == 0,
          "minimum board, seed zero, no warmups, oversubscribed threads permitted");
    draft.values[3] = "4294967295";
    check(validate().config.has_value(), "maximum uint32 seed");
    for (auto dimensions : {std::pair{std::numeric_limits<std::size_t>::max(), std::size_t{2}},
                            std::pair{std::size_t{2}, static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())},
                            std::pair{std::size_t{100000}, std::size_t{100000}}}) {
        draft = original;
        draft.values[0] = std::to_string(dimensions.first);
        draft.values[1] = std::to_string(dimensions.second);
        check(!validate().config, "overflow, signed index or generator budget rejected without board allocation");
    }
    InputData loaded{2, 1, {0, 1}};
    draft = standardExperimentForm(false, 4, true);
    draft.values[0] = draft.values[1] = draft.values[2] = draft.values[3] = "invalid inactive value";
    valid = validate(false, &loaded);
    check(valid.config && !valid.config->generated && loaded.cells == std::vector<std::uint8_t>{0,1},
          "loaded input borrowed, inactive generation fields ignored");
    check(!validate().config, "loaded selection unavailable without current input");
    loaded.cells[0] = 2;
    check(!validate(false, &loaded).config, "existing input validator rejects non-binary cells");
    draft = standardExperimentForm(true, 99, false);
    valid = validate(true);
    check(valid.config && valid.config->scaling.requestedThreads == std::vector<std::int64_t>{1,2,4,8}
          && valid.config->scaling.order == ScalingOrder::RotateLeft, "scaling defaults and policy");
    draft.values[7] = "8, 1, 3";
    check(validate(true).config->scaling.requestedThreads == std::vector<std::int64_t>{8,1,3}, "thread list order preserved");
    draft.values[6] = "2147483647";
    check(!validate(true).config && !validate(true).errors[6].empty(), "scaling records budget enforced");
    draft = standardExperimentForm(true, 1, false);
    draft.values[0] = draft.values[1] = "8000"; // Generator fits (3 buffers), scaling does not (5 buffers).
    check(!validate(true).config && !validate(true).errors[0].empty(), "scaling total board payload enforced");
    std::cout << (failures ? "FAIL" : "PASS") << ": experimental form validation without board allocation\n";
    return failures ? 1 : 0;
}
