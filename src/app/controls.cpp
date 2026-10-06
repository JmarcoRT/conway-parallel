#include "app/controls.hpp"

#include <csignal>
#include <unistd.h>
#include <termios.h>

#include <iostream>

namespace conway::app::controls {

namespace {

termios g_originalTermios{};
bool g_rawEnabled = false;

void restoreFromSignal(int sig) {
    disableRawMode();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

std::string trim(const std::string& text) {
    const char* spaces = " \t\r\n";
    const auto begin = text.find_first_not_of(spaces);
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(spaces);
    return text.substr(begin, end - begin + 1);
}

}

bool isInteractive() {
    return isatty(STDIN_FILENO) == 1;
}

void enableRawMode() {
    if (!isInteractive() || g_rawEnabled) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &g_originalTermios) != 0) {
        return;
    }

    termios raw = g_originalTermios;
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return;
    }

    g_rawEnabled = true;

    std::signal(SIGINT, restoreFromSignal);
    std::signal(SIGTERM, restoreFromSignal);
}

void disableRawMode() {
    if (!g_rawEnabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &g_originalTermios);
    g_rawEnabled = false;

    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
}

std::optional<KeyPress> readKey() {
    if (!isInteractive()) {
        return std::nullopt;
    }

    enableRawMode();

    unsigned char byte = 0;
    if (::read(STDIN_FILENO, &byte, 1) != 1) {
        return std::nullopt;
    }

    if (byte == 0x1B) {
        unsigned char marker = 0;
        if (::read(STDIN_FILENO, &marker, 1) != 1 || (marker != '[' && marker != 'O')) {
            return KeyPress{Key::Char, '\0'};
        }

        unsigned char code = 0;
        if (::read(STDIN_FILENO, &code, 1) != 1) {
            return KeyPress{Key::Char, '\0'};
        }

        switch (code) {
            case 'A':
                return KeyPress{Key::Up, '\0'};
            case 'B':
                return KeyPress{Key::Down, '\0'};
            default:
                return KeyPress{Key::Char, '\0'};
        }
    }

    if (byte == '\r' || byte == '\n') {
        return KeyPress{Key::Enter, '\0'};
    }

    return KeyPress{Key::Char, static_cast<char>(byte)};
}

std::optional<std::string> readLine(const std::string& prompt) {
    disableRawMode();

    if (!prompt.empty()) {
        std::cout << prompt << std::flush;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }

    return trim(line);
}

}
