#include "app/pattern_preview.hpp"

#include "input/input.hpp"

#include <exception>
#include <algorithm>

namespace conway::app {

void CatalogPreview::close() {
    if(session_) {session_->pause();session_->close();retiring_.push_back(std::move(session_));}
}
void CatalogPreview::poll() {
    if(session_)session_->poll();
    for(auto& old:retiring_)old->poll();
    std::erase_if(retiring_,[](const auto& old){return old->closed();});
}
void CatalogPreview::selectValidated(PatternEntry entry,InputData input) {
    // Construct before replacing the candidate; failed validation preserves it.
    auto next=std::make_unique<SimulationSession>(input,Engine::Sequential,defaultMPIOptions(),notify_,true);
    close();initial_=std::move(input);entry_=std::move(entry);session_=std::move(next);error_.clear();
}
void CatalogPreview::select(const PatternEntry& entry) {
    try {
        if(!entry.valid())throw std::runtime_error(entry.error);
        selectValidated(entry,loadInput(entry.path));
    } catch(const std::exception& error) {close();initial_.reset();entry_=entry;error_=error.what();}
}

PatternPreview buildPatternPreview(const PatternEntry& entry) {
    PatternPreview preview;
    if (!entry.valid()) {
        preview.error = "No se puede previsualizar " + entry.name + ": " +
            entry.error;
        return preview;
    }

    try {
        const InputData input = loadInput(entry.path);
        preview.current.emplace(input);
        preview.next.emplace(*preview.current);
        preview.next->step();
    } catch (const std::exception& error) {
        preview.current.reset();
        preview.next.reset();
        preview.error = "No se pudo generar la vista previa de " + entry.name +
            ": " + error.what();
    }
    return preview;
}

} // namespace conway::app
