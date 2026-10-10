#pragma once

#include <optional>
#include <string>
#include <chrono>

namespace conway::app::controls {

enum class Key {
    Up,
    Down,
    Enter,
    Char
};

struct KeyPress {
    Key type{Key::Char};
    char value{'\0'};
};

struct TimedKeyPress {
    bool ready{false};
    std::optional<KeyPress> key;
};

bool isInteractive();

void enableRawMode();
void disableRawMode();

std::optional<KeyPress> readKey();
TimedKeyPress readKeyFor(std::chrono::milliseconds timeout);

std::optional<std::string> readLine(const std::string& prompt);

}
