#include "ui/responsive_layout.hpp"
#include <cassert>
int main() {
    using namespace conway::ui;
    assert(roomyTerminal(120,40));
    assert(!roomyTerminal(119,40));
    assert(!roomyTerminal(160,39));
    assert(usableTerminal(40,18));
    assert(!usableTerminal(39,18));
    assert(!usableTerminal(80,17));
    assert(pairedSimulation(80,24,7,3));
    assert(!pairedSimulation(100,30,31,17));
    assert(!pairedSimulation(120,24,31,17));
    BoardViewport view;
    view.move(-1,-1);
    assert(view.row == 0 && view.column == 0);
    view.move(1,1);
    view.clamp(17,31,5,20);
    assert(view.row == 1 && view.column == 1);
    view.row=100; view.column=100;
    view.clamp(17,31,5,20);
    assert(view.row == 12 && view.column == 11);
    view.clamp(17,31,17,31);
    assert(view.row == 0 && view.column == 0);
}
