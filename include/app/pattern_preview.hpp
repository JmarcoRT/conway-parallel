#pragma once

#include "app/pattern_catalog.hpp"
#include "core/game.hpp"
#include "app/simulation.hpp"

#include <optional>
#include <string>

namespace conway::app {

struct PatternPreview {
    std::optional<Game> current;
    std::optional<Game> next;
    std::string error;
};

// Candidate input never follows the evolving playback snapshot.
class CatalogPreview {
public:
    explicit CatalogPreview(std::function<void()> notify={}) : notify_(std::move(notify)) {}
    void select(const PatternEntry&);
    void selectValidated(PatternEntry,InputData);
    void poll();
    void close();
    bool drained() const {return !session_ && retiring_.empty();}
    SimulationSession* session() const {return session_.get();}
    const std::optional<InputData>& initial() const {return initial_;}
    const std::optional<PatternEntry>& entry() const {return entry_;}
    const std::string& error() const {return error_;}
private:
    std::function<void()> notify_;
    std::optional<InputData> initial_;
    std::optional<PatternEntry> entry_;
    std::unique_ptr<SimulationSession> session_;
    std::vector<std::unique_ptr<SimulationSession>> retiring_;
    std::string error_;
};

PatternPreview buildPatternPreview(const PatternEntry& entry);

} // namespace conway::app
