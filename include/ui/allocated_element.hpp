#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <ftxui/dom/node.hpp>

namespace conway::ui {
// Board geometry must use the box assigned by FTXUI, after surrounding chrome
// has consumed its natural height. This presentation-only leaf does not own focus.
class AllocatedElement final : public ftxui::Node {
public:
    using Builder = std::function<ftxui::Element(int, int)>;
    explicit AllocatedElement(Builder builder) : builder_(std::move(builder)) {}
    void ComputeRequirement() override {
        requirement_ = {};
        requirement_.min_x = requirement_.min_y = 1;
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
    }
    void SetBox(ftxui::Box box) override {
        box_ = box;
        children_.clear();
        const int width = box.x_max - box.x_min + 1;
        const int height = box.y_max - box.y_min + 1;
        if (width <= 0 || height <= 0) return;
        auto child = builder_(width, height);
        child->ComputeRequirement();
        child->SetBox(box);
        children_.push_back(std::move(child));
    }
private:
    Builder builder_;
};
inline ftxui::Element allocatedElement(AllocatedElement::Builder builder) {
    return std::make_shared<AllocatedElement>(std::move(builder));
}
}  // namespace conway::ui
