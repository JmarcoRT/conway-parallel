#include "app/controls.hpp"

#include <csignal>
#include <cerrno>
#include <poll.h>
#include <system_error>
#include <unistd.h>
#include <termios.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>

namespace conway::app::controls {

namespace {

termios g_originalTermios{};
bool g_rawEnabled = false;

bool readByteWithTimeout(unsigned char& byte) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    int result = 0;
    do {
        result = ::poll(&input, 1, 50);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), "No se pudo leer la secuencia de teclado");
    }
    if (result == 0) {
        return false;
    }

    return ::read(STDIN_FILENO, &byte, 1) == 1;
}

std::optional<KeyPress> readKeyByte() {
    unsigned char byte = 0;
    if (::read(STDIN_FILENO, &byte, 1) != 1) {
        return std::nullopt;
    }

    if (byte == 0x1B) {
        unsigned char marker = 0;
        if (!readByteWithTimeout(marker) || (marker != '[' && marker != 'O')) {
            return KeyPress{Key::Char, 27};
        }

        unsigned char code = 0;
        if (!readByteWithTimeout(code)) {
            return KeyPress{Key::Char, 27};
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
    return readKeyByte();
}

TimedKeyPress readKeyFor(std::chrono::milliseconds timeout) {
    if (!isInteractive()) {
        return {};
    }

    enableRawMode();
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const auto timeoutCount = timeout.count();
    const int timeoutMs = static_cast<int>(std::clamp<std::int64_t>(
        timeoutCount, 0, std::numeric_limits<int>::max()));
    int result = 0;
    do {
        result = ::poll(&input, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), "No se pudo esperar entrada del teclado");
    }
    if (result == 0) {
        return {};
    }

    return {true, readKeyByte()};
}

std::optional<std::string> readLine(const std::string& prompt) {
    disableRawMode();

    if (!prompt.empty()) {
        std::cout << prompt << std::flush;
    }

    std::string line;
    while (true) {
        char byte = '\0';
        const ssize_t count = ::read(STDIN_FILENO, &byte, 1);
        if (count == 1) {
            if (byte == '\n') {
                break;
            }
            line += byte;
            continue;
        }
        if (count == 0) {
            if (line.empty()) {
                return std::nullopt;
            }
            break;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "No se pudo leer la entrada");
        }
    }

    return trim(line);
}

}
