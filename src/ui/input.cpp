module;
#include <ftxui/component/component.hpp>
#include <ftxui/dom/node.hpp>

module pawingo.ui;

import std;

namespace pawingo::ui {
namespace {
/// Preserve the placeholder's layout and painting, but expose a one-cell cursor anchor.
/// FTXUI places the hardware cursor at the focused node's bottom-right corner.
class EmptyInputCursor : public ftxui::Node {
public:
  EmptyInputCursor(ftxui::Element child, bool insert)
    : Node({std::move(child)}),
      shape_(insert ? ftxui::Screen::Cursor::BarBlinking : ftxui::Screen::Cursor::BlockBlinking) {
  }

  void ComputeRequirement() override {
    children_[0]->ComputeRequirement();
    requirement_ = children_[0]->requirement();
    requirement_.focused.enabled = true;
    requirement_.focused.node = this;
    requirement_.focused.box = {0, 0, 0, 0};
    requirement_.focused.cursor_shape = shape_;
  }

  void SetBox(ftxui::Box box) override {
    children_[0]->SetBox(box);
    box.x_max = box.x_min;
    box.y_max = box.y_min;
    Node::SetBox(box);
  }

private:
  ftxui::Screen::Cursor::Shape shape_;
};
} // namespace

ftxui::Component
text_input(std::string *content, std::string placeholder, ftxui::InputOption options) {
  if (!content)
    throw std::invalid_argument("Text input requires borrowed content storage.");
  // Preserve both owned and externally borrowed insert-mode state across the native option copy.
  auto insert = std::make_shared<ftxui::Ref<bool>>(std::move(options.insert));
  options.insert = &(*insert)();
  auto control = ftxui::Input(content, std::move(placeholder), std::move(options));
  return ftxui::Renderer(control, [control, content, insert] {
    auto element = control->Render();
    if (content->empty() && control->Focused())
      return ftxui::Element(std::make_shared<EmptyInputCursor>(std::move(element), (*insert)()));
    return element;
  });
}
} // namespace pawingo::ui
