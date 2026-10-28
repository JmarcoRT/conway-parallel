// Acceso a los helpers reales, sin reproducir su implementacion en el test.
#include "../src/app/ftxui_app.cpp"
#include <iostream>

int main() {
    using namespace conway::app;
    using namespace ftxui;
    // Both MPI and OpenMP call this same primitive. Check pixels, not labels.
    for(bool colors:{false,true})for(bool alive:{false,true})for(bool known:{false,true})for(int width:{2,4,8}) {
        auto screen=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(1));
        Render(screen,didacticCellElement(alive,known,0,width,colors));
        for(int x=0;x<width;++x) {
            const auto& pixel=screen.PixelAt(x,0);
            if(pixel.character!=" " || pixel.underlined!=!known || (!colors && pixel.inverted!=(known&&alive))) {
                std::cerr<<"Cell interior/state differs from shared didactic style\n";return 1;
            }
            if(colors && known && pixel.background_color!=(alive?Color::RGB(98,232,137):workerColor(0)))return 1;
        }
        if(!known) {
            auto other=Screen::Create(Dimension::Fixed(width),Dimension::Fixed(1));
            Render(other,didacticCellElement(!alive,false,0,width,colors));
            if(screen.ToString()!=other.ToString()) {std::cerr<<"Hidden cell leaks actual value\n";return 1;}
        }
        if(width==2) {
            auto other=Screen::Create(Dimension::Fixed(2),Dimension::Fixed(1));
            Render(other,didacticCellElement(alive,known,0,colors));
            if(screen.ToString()!=other.ToString()) {std::cerr<<"Cell overloads disagree\n";return 1;}
        }
    }
    for (bool compact : {false, true}) {
        for (auto [width, height] : {std::pair{110, 28}, {70, 18}, {40, 9}}) {
            DidacticFrame frame;
            frame.width = frame.height = 5;
            frame.current_alive_map.assign(25, 0);
            frame.next_alive_map.assign(25, 0);
            frame.revealed.assign(25, 0);
            frame.worker_id.assign(25, -1);
            DidacticWorkerView worker;
            worker.first_row = 1;
            worker.end_row = 6;
            frame.workers.push_back(worker);
            const auto geom = computeDidacticBoardGeometry(width / 2 - 2, height - 2, 5, 5);
            Box current, next;
            auto body = [&](bool is_next, Box& box) {
                return didacticBoardBody(is_next ? text("T0: filas 1-5") : text(" "),
                    renderDidacticBoardPanel(frame, geom, is_next, false) | reflect(box));
            };
            auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(compact ? height * 2 : height));
            auto panels = Elements{body(false, current) | flex, body(true, next) | flex};
            Render(screen, compact ? vbox(panels) : hbox(panels));
            if ((!compact && (current.y_min != next.y_min || current.y_max != next.y_max)) ||
                current.y_max - current.y_min != next.y_max - next.y_min ||
                current.x_max - current.x_min != next.x_max - next.x_min) {
                std::cerr << "Desalineacion en " << width << 'x' << height << '\n';
                return 1;
            }
            // Verifica la posicion de la cuadricula, ademas del area asignada.
            std::vector<std::pair<int, int>> corners;
            for (int y = 0; y < screen.dimy(); ++y)
                for (int x = 0; x < screen.dimx(); ++x)
                    if (screen.PixelAt(x, y).character == "┌") corners.emplace_back(x, y);
            if (corners.size() != 2 ||
                (!compact && corners[0].second != corners[1].second) ||
                (compact && corners[0].second - current.y_min !=
                            corners[1].second - next.y_min)) return 1;
        }
    }
    // A deferred board gets the actual box remaining after natural chrome,
    // including its panel's borders/title/separator, on every resize.
    for (auto [width, height] : {std::pair{160,45}, {120,40}, {100,30}, {80,24}}) {
        int allocatedWidth = 0, allocatedHeight = 0;
        auto leaf = conway::ui::allocatedElement([&](int w, int h) {
            allocatedWidth = w;
            allocatedHeight = h;
            return vbox({text("FIRST"), filler(), text("LAST")});
        });
        auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
        Render(screen, vbox({text("HEADER"), panel("BOARD", leaf), text("FOOTER")}));
        if (allocatedWidth != width - 2 || allocatedHeight != height - 6 ||
            screen.PixelAt(1, 4).character != "F" ||
            screen.PixelAt(1, height - 3).character != "L" ||
            screen.PixelAt(0, height - 1).character != "F") return 1;
    }

}
