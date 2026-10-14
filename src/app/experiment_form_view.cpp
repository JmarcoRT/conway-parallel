#include "app/experiment_form_view.hpp"
#include "app/experiment_form.hpp"
#include "app/controls.hpp"
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

namespace conway::app {
namespace {
using namespace ftxui;
class ExperimentFormView final : public ComponentBase {
public:
    ExperimentFormView(bool scaling, std::function<const InputData*()> input,
                       std::function<std::string()> name, std::function<int()> threads,
                       std::function<Element(Element)> style,
                       std::function<std::string(bool, ExperimentFormConfig)> submit)
        : scaling_(scaling), input_(std::move(input)), name_(std::move(name)),
          threads_(std::move(threads)), style_(std::move(style)), submit_(std::move(submit)) {
        MenuOption sourceOption = MenuOption::Vertical();
        sourceOption.focused_entry = &draft_.source;
        sourceOption.entries_option.transform = [this](const EntryState& entry) {
            auto row = text(std::string(entry.active ? "(*) " : "( ) ") + entry.label);
            return entry.focused ? style_(row) : row;
        };
        sourceOption.on_change = [this] { errors_ = {}; message_.clear(); };
        source_ = Menu(&sources_, &draft_.source, sourceOption);
        Components generated;
        const std::array<std::string, 8> labels{
            "Ancho", "Alto", "Densidad (%)", "Semilla", "Generaciones",
            "Calentamientos", "Repeticiones", scaling ? "Lista de hilos" : "Hilos OpenMP"};
        Components measurement;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            InputOption option;
            option.multiline = false;
            option.transform = [](InputState state) { return state.element; };
            option.on_change = [this, i] { errors_[i].clear(); message_.clear(); };
            auto inputField = Input(&draft_.values[i], option);
            auto field = Renderer(inputField, [this, inputField, i, label = labels[i]] {
                auto row = hbox({text(label + ": ") | size(WIDTH, EQUAL, 19),
                                 inputField->Render() | flex});
                if (inputField->Focused()) row = style_(std::move(row));
                Elements rows{std::move(row)};
                if (!errors_[i].empty()) rows.push_back(paragraph(errors_[i]) | color(Color::Red));
                return vbox(std::move(rows));
            });
            (i < 4 ? generated : measurement).push_back(field);
        }
        generated_ = Maybe(Container::Vertical(generated), [this] { return draft_.source == 1; });
        measurement_ = Container::Vertical(measurement);
        ButtonOption buttons = ButtonOption::Simple();
        buttons.transform = [this](const EntryState& entry) {
            auto row = text("[ " + entry.label + " ]");
            return entry.focused ? style_(row) : row;
        };
        restore_ = Button("Restaurar valores estándar", [this] { restore(); }, buttons);
        execute_ = Button("Ejecutar experimento", [this] {
            const auto validation = validateExperimentForm(draft_, scaling_, input_());
            errors_ = validation.errors;
            message_ = validation.config
                ? submit_(scaling_, *validation.config)
                : "Revisa los campos señalados.";
            if (!validation.config) {
                if (!errors_[static_cast<std::size_t>(ExperimentField::Source)].empty()) {
                    source_->TakeFocus();
                } else {
                    for (std::size_t i = 0; i < 8; ++i) {
                        if (!errors_[i].empty()) {
                            fields_[i]->TakeFocus();
                            break;
                        }
                    }
                }
            }
        }, buttons);
        fields_ = {};
        fields_.insert(fields_.end(), generated.begin(), generated.end());
        fields_.insert(fields_.end(), measurement.begin(), measurement.end());
        root_ = Container::Vertical({source_, generated_, measurement_, restore_, execute_});
        Add(root_);
    }
    bool OnEvent(Event event) override {
        if (event == Event::Tab || event == Event::TabReverse) {
            Components order{source_};
            for (std::size_t i = draft_.source == 1 ? 0 : 4; i < fields_.size(); ++i)
                order.push_back(fields_[i]);
            order.push_back(restore_);
            order.push_back(execute_);
            std::size_t position = 0;
            for (std::size_t i = 0; i < order.size(); ++i)
                if (order[i]->Focused()) position = i;
            position = event == Event::Tab ? (position + 1) % order.size()
                                          : (position + order.size() - 1) % order.size();
            order[position]->TakeFocus();
            return true;
        }
        if (source_->Focused() && (event == Event::Home || event == Event::End
            || event == Event::ArrowUp || event == Event::ArrowDown)) {
            source_->OnEvent(event);
            return true; // Mantener el foco en Entrada también en sus extremos.
        }
        return root_->OnEvent(event);
    }
    Element Render() override {
        if (!initialized_) { restore(); initialized_ = true; }
        const auto* loaded = input_();
        const auto name = loaded ? name_() : std::string{};
        const auto width = loaded ? loaded->width : 0;
        const auto height = loaded ? loaded->height : 0;
        if (loaded != previousInput_ || name != previousName_
            || width != previousWidth_ || height != previousHeight_) {
            errors_ = {};
            message_.clear();
            previousInput_ = loaded;
            previousName_ = name;
            previousWidth_ = width;
            previousHeight_ = height;
        }
        sources_[0] = loaded ? "Patrón cargado" : "Patrón cargado (no disponible)";
        Elements entry{text("ENTRADA") | bold, source_->Render()};
        if (draft_.source == 0) {
            entry.push_back(paragraph(loaded
                ? name + " · " + std::to_string(loaded->width) + " × " + std::to_string(loaded->height) + " (ancho × alto)"
                : "Sin patrón cargado: esta opción no está disponible.") | dim);
        }
        const auto& sourceError = errors_[static_cast<std::size_t>(ExperimentField::Source)];
        if (!sourceError.empty()) entry.push_back(paragraph(sourceError) | color(Color::Red));
        entry.push_back(generated_->Render());
        auto inputGroup = vbox(std::move(entry));
        auto measurementGroup = vbox({text("MEDICIÓN") | bold, measurement_->Render()});
        Element groups = controls::terminalSize().columns >= 100
            ? hbox({inputGroup | flex, separator(), measurementGroup | flex})
            : vbox({inputGroup, separator(), measurementGroup});
        return vbox({std::move(groups), separator(), restore_->Render(), execute_->Render(),
                     paragraph(message_) | dim}) | yframe | flex;
    }
private:
    void restore() {
        draft_ = standardExperimentForm(scaling_, threads_(), input_() != nullptr);
        errors_ = {};
        message_.clear();
    }
    bool scaling_, initialized_{false};
    std::function<const InputData*()> input_;
    std::function<std::string()> name_;
    std::function<int()> threads_;
    std::function<Element(Element)> style_;
    std::function<std::string(bool, ExperimentFormConfig)> submit_;
    const InputData* previousInput_{nullptr};
    std::string previousName_;
    std::size_t previousWidth_{0}, previousHeight_{0};
    ExperimentFormDraft draft_;
    std::array<std::string, kExperimentFieldCount> errors_;
    std::string message_;
    std::vector<std::string> sources_{"Patrón cargado", "Tablero generado"};
    Components fields_;
    Component source_, generated_, measurement_, restore_, execute_, root_;
};
} // namespace
ftxui::Component makeExperimentFormView(
    bool scaling, std::function<const InputData*()> input,
    std::function<std::string()> patternName, std::function<int()> currentThreads,
    std::function<ftxui::Element(ftxui::Element)> focusedStyle,
    std::function<std::string(bool, ExperimentFormConfig)> submit) {
    return ftxui::Make<ExperimentFormView>(scaling, std::move(input), std::move(patternName),
                                          std::move(currentThreads), std::move(focusedStyle), std::move(submit));
}
} // namespace conway::app
