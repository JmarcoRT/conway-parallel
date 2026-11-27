// Acceso a los helpers reales, sin reproducir su implementacion en el test.
#include "../src/app/ftxui_app.cpp"
#include <iostream>
#include <fstream>
#include <iterator>

int main(int argc,char** argv) {
    if(argc>1) {
        using namespace conway::app;using namespace ftxui;
        for(bool colors:{false,true})for(auto [w,h]:{std::pair{80,24},{40,12}}) {
            DidacticFrame f;f.width=f.height=5;f.current_alive_map.assign(25,0);
            f.current_alive_map[7]=f.current_alive_map[12]=f.current_alive_map[17]=1;
            auto screen=Screen::Create(Dimension::Fixed(w),Dimension::Fixed(h));
            Render(screen,renderDidacticBoardPanel(f,computeDidacticBoardGeometry(w,h,5,5),false,colors));
            std::ifstream file(std::string(argv[1])+"/tests/fixtures/board_renderer/original-5-"+std::to_string(w)+(colors?"-color":"-mono")+".ansi",std::ios::binary);
            std::string original((std::istreambuf_iterator<char>(file)),{});
            if(original.empty() || screen.ToString()!=original) {
                std::cerr<<"Shared renderer changed original didactic pixels "<<w<<" colors="<<colors<<'\n';return 1;
            }
        }
    }
    using namespace conway::app;
    using namespace ftxui;
    // Wrapped result paragraphs must retain their tail after a narrow layout.
    // The first Fit screen starts at height 1 before flexbox resolves wrapping.
    for(int width:{37,47,77}) {
        auto row=fitResultRow(paragraph("Sesión original: #0   Calentamientos por sesión: 1 TAIL_SENTINEL"),width);
        auto raster=Screen::Create(Dimension::Fixed(width),Dimension::Fit(row));
        Render(raster,row);
        std::string visible;
        for(int y=0;y<raster.dimy();++y)for(int x=0;x<width;++x)visible+=raster.PixelAt(x,y).character;
        if(visible.find("TAIL_SENTINEL")==std::string::npos) {
            std::cerr<<"Wrapped result row lost its tail at width "<<width<<'\n';return 1;
        }
    }
    // Both MPI and OpenMP call this same primitive. Check pixels, not labels.
    for(bool colors:{false,true})for(bool alive:{false,true})for(bool known:{false,true})for(int width:{1,2,4,8}) {
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
    // Compact viewports must include their border in the allocation and draw
    // exactly the cell width advertised, including monochrome one-column cells.
    for(bool halfBlocks:{false,true})for(auto [width,height]:{std::pair{30,12},{8,5},{40,18},{5,3}}) {
        auto geom=computeDidacticBoardGeometry(width,height,50,21,halfBlocks);
        if(!geom.visible_cols || !geom.visible_rows ||
           geom.board_chars>static_cast<std::size_t>(width) ||
           geom.board_lines>static_cast<std::size_t>(height)) {
            std::cerr<<"Compact viewport exceeds panel budget\n";return 1;
        }
        auto cell=didacticCellElement(true,true,0,geom.cell_chars,false);
        cell->ComputeRequirement();
        if(cell->requirement().min_x!=static_cast<int>(geom.cell_chars)) {
            std::cerr<<"Compact cell exceeds advertised width\n";return 1;
        }
    }
    // Decode every real cell at the bottom/right viewport, including the last
    // odd half-row. An intact label cannot hide a clipped or shifted raster.
    for(bool colors:{false,true})for(auto [width,height]:{std::pair{30,12},{8,5},{40,18},{5,3}}) {
        DidacticFrame frame;frame.width=50;frame.height=21;
        frame.current_alive_map.resize(frame.width*frame.height);
        for(std::size_t r=0;r<frame.height;++r)for(std::size_t c=0;c<frame.width;++c)
            frame.current_alive_map[r*frame.width+c]=(r*3+c*5)%7==0;
        auto geom=computeDidacticBoardGeometry(width,height,frame.width,frame.height,colors);
        geom.row_offset=frame.height-geom.visible_rows;
        geom.col_offset=frame.width-geom.visible_cols;
        auto raster=Screen::Create(Dimension::Fixed(geom.board_chars),Dimension::Fixed(geom.board_lines));
        Render(raster,renderDidacticBoardPanel(frame,geom,false,colors,false));
        for(std::size_t r=0;r<geom.visible_rows;++r)for(std::size_t c=0;c<geom.visible_cols;++c) {
            const auto row=geom.row_offset+r,col=geom.col_offset+c;
            const bool alive=frame.current_alive_map[row*frame.width+col]!=0;
            const int x=1+c*(geom.half_block?1:geom.cell_chars),y=1+(geom.half_block?r/2:r);
            const auto& pixel=raster.PixelAt(x,y);
            if(colors) {
                const auto actual=geom.half_block && r%2==0?pixel.foreground_color:pixel.background_color;
                const auto expected=didacticCellColor(alive,true,-1,((row+col)&1U)!=0U);
                if(actual!=expected || pixel.character!=(geom.half_block?"▀":" ")) {
                    std::cerr<<"Viewport lost or shifted cell "<<row<<','<<col<<'\n';return 1;
                }
            } else if(pixel.character!=" " || pixel.inverted!=alive) {
                std::cerr<<"Monochrome viewport lost cell "<<row<<','<<col<<'\n';return 1;
            }
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
                return didacticBoardBody(is_next ? text("T0") : text(" "),
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
