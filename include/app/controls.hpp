#pragma once

#include <optional>
#include <string>

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

bool isInteractive();

void enableRawMode();
void disableRawMode();

std::optional<KeyPress> readKey();

std::optional<std::string> readLine(const std::string& prompt);

}
