#pragma once

#include "StarInputEvent.hpp"
#include "StarList.hpp"
#include "StarMap.hpp"
#include "StarVector.hpp"

#include "SDL3/SDL.h"
#include "imgui.h"

#include <cfloat>
#include <cmath>

namespace Star {

// On-screen controls for touch devices. Fingers either press an on-screen
// button (emitting synthetic key events), or act as the mouse pointer: the
// first free finger aims/clicks, a second free finger scrolls.
class TouchControls {
public:
  struct Button {
    char const* label;
    Vec2F anchor;    // in button units from the screen edge
    bool right;      // anchor from the right edge instead of the left
    bool bottom;     // anchor from the bottom edge instead of the top
    float radius;    // in button units
    Maybe<Key> key;
    bool toggle;
  };

  TouchControls() {
    // 0..3 d-pad, 4 jump, 5 interact, 6 tech, 7 inventory, 8 menu, 9 crafting, 10 alt-fire toggle, 11 shift toggle
    m_buttons = {
      {"<",   {1.2f, 1.7f}, false, true,  0.80f, Key::A, false},
      {">",   {3.5f, 1.7f}, false, true,  0.80f, Key::D, false},
      {"^",   {2.35f, 3.3f}, false, true, 0.75f, Key::W, false},
      {"v",   {2.35f, 1.7f}, false, true, 0.55f, Key::S, false},
      {"JUMP", {1.8f, 1.8f}, true, true,  1.00f, Key::Space, false},
      {"USE", {4.2f, 1.2f}, true, true,   0.70f, Key::E, false},
      {"TECH", {3.9f, 3.0f}, true, true,  0.65f, Key::F, false},
      {"INV", {1.0f, 4.1f}, true, true,   0.60f, Key::I, false},
      {"MENU", {0.8f, 0.8f}, true, false, 0.55f, Key::Escape, false},
      {"CRAFT", {2.1f, 0.8f}, true, false, 0.55f, Key::C, false},
      {"ALT", {3.4f, 0.8f}, true, false,  0.55f, {}, true},
      {"SHIFT", {4.7f, 0.8f}, true, false, 0.55f, Key::LShift, true},
    };
    m_buttonDown.resize(m_buttons.size(), false);
    m_toggled.resize(m_buttons.size(), false);
  }

  void setScreenSize(Vec2U size) { m_size = size; }
  void setVisible(bool visible) { m_visible = visible; }

  // Returns true if the SDL event was a finger event (and has been handled).
  bool processEvent(SDL_Event const& event, List<InputEvent>& out) {
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION
        && event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED)
      return false;

    auto const& f = event.tfinger;
    SDL_FingerID id = f.fingerID;
    Vec2F pos = Vec2F(f.x * m_size[0], f.y * m_size[1]);

    if (event.type == SDL_EVENT_FINGER_DOWN) {
      int button = m_visible ? hitButton(pos) : -1;
      if (button >= 0) {
        m_fingerButton[id] = button;
        pressButton(button, out);
      } else if (!m_pointerFinger) {
        m_pointerFinger = id;
        pointerMove(pos, out);
        out.append(MouseButtonDownEvent{pointerButton(), flip(pos)});
        m_pointerButtonHeld = pointerButton();
      } else if (!m_scrollFinger) {
        m_scrollFinger = id;
        m_scrollAccum = 0.0f;
      }
    } else if (event.type == SDL_EVENT_FINGER_MOTION) {
      auto it = m_fingerButton.find(id);
      if (it != m_fingerButton.end()) {
        // sliding between d-pad buttons switches direction
        int button = hitButton(pos);
        if (button != it->second && button >= 0 && button <= 3 && it->second <= 3) {
          releaseButton(it->second, out);
          it->second = button;
          pressButton(button, out);
        }
      } else if (m_pointerFinger && *m_pointerFinger == id) {
        pointerMove(pos, out);
      } else if (m_scrollFinger && *m_scrollFinger == id) {
        m_scrollAccum += f.dy * m_size[1];
        float const step = 48.0f;
        while (std::fabs(m_scrollAccum) >= step) {
          bool down = m_scrollAccum > 0;
          out.append(MouseWheelEvent{down ? MouseWheel::Down : MouseWheel::Up, flip(pos)});
          m_scrollAccum += down ? -step : step;
        }
      }
    } else {
      auto it = m_fingerButton.find(id);
      if (it != m_fingerButton.end()) {
        releaseButton(it->second, out);
        m_fingerButton.erase(it);
      } else if (m_pointerFinger && *m_pointerFinger == id) {
        pointerMove(pos, out);
        out.append(MouseButtonUpEvent{m_pointerButtonHeld, flip(pos)});
        m_pointerFinger.reset();
      } else if (m_scrollFinger && *m_scrollFinger == id) {
        m_scrollFinger.reset();
      }
    }
    return true;
  }

  void draw(ImDrawList* list) const {
    if (!m_visible)
      return;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
      auto const& b = m_buttons[i];
      Vec2F c = center(b);
      float r = b.radius * unit();
      bool active = m_buttonDown[i] || (b.toggle && m_toggled[i]);
      list->AddCircleFilled(ImVec2(c[0], c[1]), r, active ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 70), 32);
      list->AddCircle(ImVec2(c[0], c[1]), r, IM_COL32(255, 255, 255, 140), 32, 2.0f);
      float fontSize = std::max(14.0f, r * 0.5f);
      ImFont* font = ImGui::GetFont();
      ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, b.label);
      list->AddText(font, fontSize, ImVec2(c[0] - ts.x / 2, c[1] - ts.y / 2), IM_COL32(255, 255, 255, 200), b.label);
    }
  }

  // releases everything, e.g. when the app loses focus
  void releaseAll(List<InputEvent>& out) {
    for (auto const& p : List<pair<SDL_FingerID, int>>(m_fingerButton.begin(), m_fingerButton.end()))
      releaseButton(p.second, out);
    m_fingerButton.clear();
    if (m_pointerFinger) {
      out.append(MouseButtonUpEvent{m_pointerButtonHeld, m_lastPointer});
      m_pointerFinger.reset();
    }
    m_scrollFinger.reset();
  }

private:
  float unit() const { return std::min(m_size[0], m_size[1]) * 0.1f; }

  Vec2F center(Button const& b) const {
    float u = unit();
    float x = b.right ? m_size[0] - b.anchor[0] * u : b.anchor[0] * u;
    float y = b.bottom ? m_size[1] - b.anchor[1] * u : b.anchor[1] * u;
    return Vec2F(x, y);
  }

  int hitButton(Vec2F pos) const {
    int best = -1;
    float bestDist = 0.0f;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
      auto const& b = m_buttons[i];
      Vec2F c = center(b);
      float dist = (pos - c).magnitude();
      float r = b.radius * unit() * 1.15f;
      if (dist <= r && (best < 0 || dist < bestDist)) {
        best = (int)i;
        bestDist = dist;
      }
    }
    return best;
  }

  Vec2F flip(Vec2F pos) const { return Vec2F(pos[0], (float)m_size[1] - pos[1]); }

  MouseButton pointerButton() const { return m_toggled[AltFireIndex] ? MouseButton::Right : MouseButton::Left; }

  void pointerMove(Vec2F pos, List<InputEvent>& out) {
    Vec2F flipped = flip(pos);
    out.append(MouseMoveEvent{flipped - m_lastPointer, flipped});
    m_lastPointer = flipped;
  }

  void pressButton(int index, List<InputEvent>& out) {
    auto const& b = m_buttons[index];
    if (b.toggle) {
      m_toggled[index] = !m_toggled[index];
      if (b.key) {
        if (m_toggled[index])
          out.append(KeyDownEvent{*b.key, KeyMod::NoMod});
        else
          out.append(KeyUpEvent{*b.key});
      }
      return;
    }
    m_buttonDown[index] = true;
    if (b.key)
      out.append(KeyDownEvent{*b.key, KeyMod::NoMod});
  }

  void releaseButton(int index, List<InputEvent>& out) {
    auto const& b = m_buttons[index];
    if (b.toggle)
      return;
    if (m_buttonDown[index]) {
      m_buttonDown[index] = false;
      if (b.key)
        out.append(KeyUpEvent{*b.key});
    }
  }

  static constexpr size_t AltFireIndex = 10;

  List<Button> m_buttons;
  List<bool> m_buttonDown;
  List<bool> m_toggled;
  HashMap<SDL_FingerID, int> m_fingerButton;
  Maybe<SDL_FingerID> m_pointerFinger;
  Maybe<SDL_FingerID> m_scrollFinger;
  MouseButton m_pointerButtonHeld = MouseButton::Left;
  Vec2F m_lastPointer = Vec2F();
  float m_scrollAccum = 0.0f;
  Vec2U m_size = Vec2U(1280, 720);
  bool m_visible = true;
};

}
