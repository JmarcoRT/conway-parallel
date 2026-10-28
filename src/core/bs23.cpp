#include "core/bs23.hpp"

namespace conway::core {
namespace {
template<bool Capture>
std::uint8_t evaluate(const std::uint8_t* current,std::size_t stride,
                      std::size_t row,std::size_t col,CellEvaluation* capture) {
    const std::size_t i=row*stride+col;
    const auto nw=current[i-stride-1], n=current[i-stride], ne=current[i-stride+1];
    const auto w=current[i-1], e=current[i+1];
    const auto sw=current[i+stride-1], s=current[i+stride], se=current[i+stride+1];
    const int neighbors=nw+n+ne+w+e+sw+s+se;
    const auto before=current[i];
    const std::uint8_t after=(neighbors==3 || (before!=0 && neighbors==2))?1:0;
    if constexpr(Capture) *capture={before,{nw,n,ne,w,e,sw,s,se},static_cast<std::uint8_t>(neighbors),after};
    return after;
}
}
std::uint8_t next_cell_state(const std::uint8_t* current,std::size_t stride,std::size_t row,std::size_t col) {
    return evaluate<false>(current,stride,row,col,nullptr);
}
std::uint8_t next_cell_state(const std::uint8_t* current,std::size_t stride,std::size_t row,std::size_t col,CellEvaluation& capture) {
    return evaluate<true>(current,stride,row,col,&capture);
}
} // namespace conway::core
