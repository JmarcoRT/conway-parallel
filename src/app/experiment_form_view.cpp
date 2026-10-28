#include "app/experiment_form_view.hpp"
#include "app/experiment_form.hpp"
#include "app/controls.hpp"
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

namespace conway::app {
namespace {
using namespace ftxui;
constexpr auto idx(ExperimentField field) {return static_cast<std::size_t>(field);}
class ExperimentFormView final : public ComponentBase {
public:
    ExperimentFormView(bool scaling,std::function<const InputData*()> input,
        std::function<std::string()> name,std::function<int()> threads,
        std::function<Element(Element)> style,std::function<std::string(bool,ExperimentFormConfig)> submit)
        :scaling_(scaling),input_(std::move(input)),name_(std::move(name)),threads_(std::move(threads)),
         style_(std::move(style)),submit_(std::move(submit)) {
        MenuOption menu=MenuOption::Vertical();
        menu.entries_option.transform=[this](const EntryState& entry) {
            auto row=text(std::string(entry.active?"(*) ":"( ) ")+entry.label);
            return entry.focused?style_(row):row;
        };
        menu.on_change=[this]{errors_={};message_.clear();};
        source_=Menu(&sources_,&draft_.source,menu);
        parallel_=Menu(&engines_,&draft_.parallelEngine,menu);
        CheckboxOption check=CheckboxOption::Simple();
        check.transform=[this](const EntryState& entry) {
            auto row=text(std::string(entry.state?"[x] ":"[ ] ")+entry.label);
            return entry.focused?style_(row):row;
        };
        check.on_change=[this]{errors_={};message_.clear();};
        sequential_=Checkbox("Secuencial",&draft_.sequential,check);
        openmp_=Checkbox("OpenMP",&draft_.openmp,check);
        mpi_=Checkbox("MPI",&draft_.mpi,check);
        participants_=scaling_?parallel_:Container::Vertical({sequential_,openmp_,mpi_});
        const std::array<std::string,8> labels{"Ancho","Alto","Densidad (%)","Semilla","Generaciones","Calentamientos","Repeticiones","Hilos OpenMP"};
        Components generated,measurement;
        for(std::size_t i=0;i<8;++i) {
            fields_[i]=makeField(i,labels[i]);
            if(i<4) generated.push_back(fields_[i]);
            else if(i<7) measurement.push_back(fields_[i]);
        }
        fields_[idx(ExperimentField::Processes)]=makeField(idx(ExperimentField::Processes),"Procesos MPI");
        measurement.push_back(Maybe(fields_[7],[this]{return scaling_ || draft_.openmp;}));
        measurement.push_back(Maybe(fields_[idx(ExperimentField::Processes)],[this]{return !scaling_ && draft_.mpi;}));
        generated_=Maybe(Container::Vertical(generated),[this]{return draft_.source==1;});
        measurement_=Container::Vertical(measurement);
        ButtonOption buttons=ButtonOption::Simple();
        buttons.transform=[this](const EntryState& entry) {
            auto row=text("[ "+entry.label+" ]");return entry.focused?style_(row):row;
        };
        restore_=Button("Restaurar valores estándar",[this]{restore();},buttons);
        execute_=Button("Ejecutar experimento",[this] {
            auto validation=validateExperimentForm(draft_,scaling_,input_());errors_=validation.errors;
            message_=validation.config?submit_(scaling_,*validation.config):"Revisa los campos señalados.";
            if(!validation.config) {
                if(!errors_[idx(ExperimentField::Source)].empty()) source_->TakeFocus();
                else if(!errors_[idx(ExperimentField::Participants)].empty()) (scaling_?parallel_:sequential_)->TakeFocus();
                else for(auto i:{0u,1u,2u,3u,4u,5u,6u,7u,9u}) if(!errors_[i].empty()) {fields_[i]->TakeFocus();break;}
            }
        },buttons);
        root_=Container::Vertical({source_,generated_,participants_,measurement_,restore_,execute_});Add(root_);
    }
    bool OnEvent(Event event) override {
        if(event==Event::Tab || event==Event::TabReverse) {
            Components order{source_};
            if(draft_.source==1) for(std::size_t i=0;i<4;++i) order.push_back(fields_[i]);
            if(scaling_) order.push_back(parallel_);
            else {order.push_back(sequential_);order.push_back(openmp_);order.push_back(mpi_);}
            for(std::size_t i=4;i<7;++i) order.push_back(fields_[i]);
            if(scaling_ || draft_.openmp) order.push_back(fields_[7]);
            if(!scaling_ && draft_.mpi) order.push_back(fields_[idx(ExperimentField::Processes)]);
            order.push_back(restore_);order.push_back(execute_);
            std::size_t position=0;
            for(std::size_t i=0;i<order.size();++i) if(order[i]->Focused()) position=i;
            position=event==Event::Tab?(position+1)%order.size():(position+order.size()-1)%order.size();
            order[position]->TakeFocus();return true;
        }
        if((source_->Focused() || (scaling_ && parallel_->Focused())) &&
           (event==Event::Home || event==Event::End || event==Event::ArrowUp || event==Event::ArrowDown)) {
            (source_->Focused()?source_:parallel_)->OnEvent(event);return true;
        }
        return root_->OnEvent(event);
    }
    Element Render() override {
        if(!initialized_) {restore();initialized_=true;}
        const auto* loaded=input_();const auto name=loaded?name_():std::string{};
        const auto width=loaded?loaded->width:0, height=loaded?loaded->height:0;
        if(loaded!=previousInput_ || name!=previousName_ || width!=previousWidth_ || height!=previousHeight_) {
            errors_={};message_.clear();previousInput_=loaded;previousName_=name;
            previousWidth_=width;previousHeight_=height;
        }
        sources_[0]=loaded?"Patrón cargado":"Patrón cargado (no disponible)";
        Elements entry{text("ENTRADA")|bold,source_->Render()};
        if(draft_.source==0) entry.push_back(paragraph(loaded?
            name+" · "+std::to_string(loaded->width)+" × "+std::to_string(loaded->height)+" (ancho × alto)":
            "Sin patrón cargado: esta opción no está disponible.")|dim);
        if(!errors_[idx(ExperimentField::Source)].empty()) entry.push_back(paragraph(errors_[idx(ExperimentField::Source)])|color(Color::Red));
        entry.push_back(generated_->Render());
        Elements measure{text(scaling_?"MOTOR PARALELO":"PARTICIPANTES · Espacio selecciona")|bold,participants_->Render()};
        if(!errors_[idx(ExperimentField::Participants)].empty()) measure.push_back(paragraph(errors_[idx(ExperimentField::Participants)])|color(Color::Red));
        if(!scaling_ && !draft_.sequential) measure.push_back(paragraph("Sin Secuencial: speedup y eficiencia respecto de él serán N/D.")|dim);
        measure.push_back(text("MEDICIÓN")|bold);measure.push_back(measurement_->Render());
        if((scaling_ && draft_.parallelEngine==1) || (!scaling_ && draft_.mpi))
            measure.push_back(paragraph("MPI: calentamientos por sesión nueva antes de cada muestra; sin sobresuscripción.")|dim);
        auto a=vbox(std::move(entry)),b=vbox(std::move(measure));
        const int columns=static_cast<int>(controls::terminalSize().columns);
        const int leftWidth=(columns-3)/2;
        auto groups=columns>=100?hbox({a|size(WIDTH,EQUAL,leftWidth),separator(),
            b|size(WIDTH,EQUAL,columns-3-leftWidth)}):vbox({a,separator(),b});
        return vbox({groups,separator(),restore_->Render(),execute_->Render(),paragraph(message_)|dim})|yframe|flex;
    }
private:
    Component makeField(std::size_t i,std::string label) {
        InputOption option;option.multiline=false;option.transform=[](InputState state){return state.element;};
        option.on_change=[this,i]{errors_[i].clear();message_.clear();};
        auto input=Input(&draft_.values[i],option);
        return Renderer(input,[this,input,i,label] {
            const auto caption=i==7 && scaling_?(draft_.parallelEngine==1?"Lista de procesos":"Lista de hilos"):label;
            auto row=hbox({text(caption+": ")|size(WIDTH,EQUAL,19),input->Render()|flex});
            if(input->Focused()) row=style_(row);
            Elements rows{row};if(!errors_[i].empty()) rows.push_back(paragraph(errors_[i])|color(Color::Red));
            return vbox(std::move(rows));
        });
    }
    void restore() {draft_=standardExperimentForm(scaling_,threads_(),input_()!=nullptr);errors_={};message_.clear();}
    bool scaling_,initialized_{false};
    std::function<const InputData*()> input_;std::function<std::string()> name_;std::function<int()> threads_;
    std::function<Element(Element)> style_;std::function<std::string(bool,ExperimentFormConfig)> submit_;
    const InputData* previousInput_{nullptr};std::size_t previousWidth_{},previousHeight_{};
    std::string previousName_,message_;
    ExperimentFormDraft draft_;std::array<std::string,kExperimentFieldCount> errors_;
    std::vector<std::string> sources_{"Patrón cargado","Tablero generado"},engines_{"OpenMP","MPI"};
    std::array<Component,kExperimentFieldCount> fields_;
    Component source_,parallel_,sequential_,openmp_,mpi_,participants_,generated_,measurement_,restore_,execute_,root_;
};
}
ftxui::Component makeExperimentFormView(bool scaling,std::function<const InputData*()> input,
    std::function<std::string()> patternName,std::function<int()> currentThreads,
    std::function<ftxui::Element(ftxui::Element)> focusedStyle,std::function<std::string(bool,ExperimentFormConfig)> submit) {
    return ftxui::Make<ExperimentFormView>(scaling,std::move(input),std::move(patternName),std::move(currentThreads),std::move(focusedStyle),std::move(submit));
}
}
