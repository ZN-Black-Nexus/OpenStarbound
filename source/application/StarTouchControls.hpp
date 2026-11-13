#pragma once

#include "StarInputEvent.hpp"
#include "StarList.hpp"
#include "StarMap.hpp"
#include "StarVector.hpp"

#include "SDL3/SDL.h"
#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Star {

// On-screen controls for touch devices. Fingers either press an on-screen
// button (emitting synthetic key events), drive the movement joystick or the
// aim stick, or act as the mouse pointer: the first free finger aims/clicks,
// a second free finger scrolls. The CTRL button opens a small settings panel;
// choices are kept in touchcontrols.cfg in the data directory.
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
    // 0..3 d-pad, 4 jump, 5 interact, 6 tech, 7 inventory, 8 menu, 9 crafting, 10 alt-fire toggle, 11 shift toggle, 12 settings
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
      {"CTRL", {6.0f, 0.8f}, true, false, 0.55f, {}, false},
    };
    m_buttonDown.resize(m_buttons.size(), false);
    m_toggled.resize(m_buttons.size(), false);
  }

  void setScreenSize(Vec2U size) { m_size = size; }
  void setVisible(bool visible) {
    loadSettings();
    m_visible = visible;
    if (!visible)
      m_settingsOpen = false;
  }

  // Returns true if the SDL event was a finger event (and has been handled).
  bool processEvent(SDL_Event const& event, List<InputEvent>& out) {
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION
        && event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED)
      return false;

    auto const& f = event.tfinger;
    SDL_FingerID id = f.fingerID;
    Vec2F pos = Vec2F(f.x * m_size[0], f.y * m_size[1]);

    loadSettings();

    if (m_settingsOpen) {
      if (event.type == SDL_EVENT_FINGER_DOWN)
        settingsTap(pos);
      return true;
    }

    if (event.type == SDL_EVENT_FINGER_DOWN) {
      int button = m_visible ? hitButton(pos) : -1;
      if (button >= 0) {
        m_fingerButton[id] = button;
        pressButton(button, out);
      } else if (m_visible && m_aimStick && !m_aimFinger && (pos - aimCenter()).magnitude() <= aimRadius() * 1.2f) {
        m_aimFinger = id;
        aimUpdate(pos, out);
        if (m_aimFires) {
          m_aimButton = pointerButton();
          out.append(MouseButtonDownEvent{m_aimButton, m_lastPointer});
          m_aimPressed = true;
        }
      } else if (m_visible && m_joystick && !m_joyFinger && pos[0] < m_size[0] * 0.4f && pos[1] > m_size[1] * 0.3f) {
        m_joyFinger = id;
        m_joyAnchor = m_joyPos = pos;
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
      } else if (m_aimFinger && *m_aimFinger == id) {
        aimUpdate(pos, out);
      } else if (m_joyFinger && *m_joyFinger == id) {
        joyUpdate(pos, out);
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
      } else if (m_aimFinger && *m_aimFinger == id) {
        releaseAim(out);
      } else if (m_joyFinger && *m_joyFinger == id) {
        releaseJoy(out);
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
    ImFont* uiFont = ImGui::GetFont();

    if (m_joystick) {
      float r = joyRadius();
      Vec2F base = m_joyFinger ? m_joyAnchor : Vec2F(2.3f * unit(), m_size[1] - 2.3f * unit());
      Vec2F knob = m_joyFinger ? m_joyPos : base;
      ImU32 ring = m_joyFinger ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
      list->AddCircleFilled(ImVec2(base[0], base[1]), r, IM_COL32(0, 0, 0, m_joyFinger ? 70 : 30), 40);
      list->AddCircle(ImVec2(base[0], base[1]), r, ring, 40, 2.0f);
      list->AddCircleFilled(ImVec2(knob[0], knob[1]), r * 0.45f, m_joyFinger ? IM_COL32(255, 255, 255, 130) : IM_COL32(255, 255, 255, 50), 32);
    }

    if (m_aimStick) {
      float r = aimRadius();
      Vec2F base = aimCenter();
      Vec2F knob = base + m_aimVec * (r * 0.6f);
      list->AddCircleFilled(ImVec2(base[0], base[1]), r, IM_COL32(0, 0, 0, 60), 40);
      list->AddCircle(ImVec2(base[0], base[1]), r, IM_COL32(255, 255, 255, 140), 40, 2.0f);
      list->AddCircleFilled(ImVec2(knob[0], knob[1]), r * 0.42f, m_aimFinger ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 70), 32);
      if (!m_aimFinger) {
        float fs = std::max(14.0f, r * 0.3f);
        ImVec2 ts = uiFont->CalcTextSizeA(fs, FLT_MAX, 0.0f, "AIM");
        list->AddText(uiFont, fs, ImVec2(base[0] - ts.x / 2, base[1] - ts.y / 2), IM_COL32(255, 255, 255, 200), "AIM");
      }
    }

    for (size_t i = 0; i < m_buttons.size(); ++i) {
      if (m_joystick && i <= 3)
        continue;
      auto const& b = m_buttons[i];
      Vec2F c = center(i);
      float r = b.radius * unit();
      bool active = m_buttonDown[i] || (b.toggle && m_toggled[i]);
      list->AddCircleFilled(ImVec2(c[0], c[1]), r, active ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 70), 32);
      list->AddCircle(ImVec2(c[0], c[1]), r, IM_COL32(255, 255, 255, 140), 32, 2.0f);
      float fontSize = std::max(14.0f, r * 0.5f);
      ImFont* font = ImGui::GetFont();
      ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, b.label);
      list->AddText(font, fontSize, ImVec2(c[0] - ts.x / 2, c[1] - ts.y / 2), IM_COL32(255, 255, 255, 200), b.label);
    }

    if (m_settingsOpen) {
      float u = unit();
      float fs = u * 0.42f;
      Vec2F origin = settingsOrigin();
      list->AddRectFilled(ImVec2(0, 0), ImVec2(m_size[0], m_size[1]), IM_COL32(0, 0, 0, 120));
      list->AddRectFilled(ImVec2(origin[0] - 0.3f * u, origin[1] - 1.1f * u),
          ImVec2(origin[0] + SettingsWidth * u + 0.3f * u, origin[1] + SettingsRows * SettingsRowHeight * u + 0.2f * u), IM_COL32(20, 24, 32, 235), 0.2f * u);
      list->AddText(uiFont, fs * 1.1f, ImVec2(origin[0], origin[1] - 0.85f * u), IM_COL32(255, 255, 255, 255), "Touch controls");
      char const* labels[SettingsRows] = {
        m_joystick ? "Movement:  JOYSTICK  (tap for buttons)" : "Movement:  BUTTONS  (tap for joystick)",
        m_aimStick ? "Aim stick:  ON" : "Aim stick:  OFF  (tap the screen to aim)",
        m_aimFires ? "Aim stick uses held item:  ON" : "Aim stick uses held item:  OFF  (aim only)",
        "Close"};
      for (int i = 0; i < SettingsRows; ++i) {
        ImVec2 a(origin[0], origin[1] + i * SettingsRowHeight * u);
        ImVec2 bmax(origin[0] + SettingsWidth * u, a.y + (SettingsRowHeight - 0.15f) * u);
        list->AddRectFilled(a, bmax, i == SettingsRows - 1 ? IM_COL32(70, 110, 160, 255) : IM_COL32(50, 58, 72, 255), 0.15f * u);
        ImVec2 ts = uiFont->CalcTextSizeA(fs, FLT_MAX, 0.0f, labels[i]);
        list->AddText(uiFont, fs, ImVec2(a.x + 0.3f * u, (a.y + bmax.y) / 2 - ts.y / 2), IM_COL32(255, 255, 255, 255), labels[i]);
      }
    }
  }

  // releases everything, e.g. when the app loses focus
  void releaseAll(List<InputEvent>& out) {
    for (auto const& p : List<pair<SDL_FingerID, int>>(m_fingerButton.begin(), m_fingerButton.end()))
      releaseButton(p.second, out);
    m_fingerButton.clear();
    releaseAim(out);
    releaseJoy(out);
    if (m_pointerFinger) {
      out.append(MouseButtonUpEvent{m_pointerButtonHeld, m_lastPointer});
      m_pointerFinger.reset();
    }
    m_scrollFinger.reset();
  }

private:
  float unit() const { return std::min(m_size[0], m_size[1]) * 0.1f; }

  Vec2F center(size_t index) const {
    auto const& b = m_buttons[index];
    Vec2F anchor = b.anchor;
    float u = unit();
    // the aim stick takes the bottom right corner; the action buttons move around it
    if (m_aimStick) {
      if (index == 4) anchor = {4.7f, 1.4f};
      else if (index == 5) anchor = {4.4f, 3.4f};
      else if (index == 6) anchor = {2.6f, 4.3f};
      else if (index == 7) anchor = {1.0f, 4.6f};
    }
    float x = b.right ? m_size[0] - anchor[0] * u : anchor[0] * u;
    float y = b.bottom ? m_size[1] - anchor[1] * u : anchor[1] * u;
    return Vec2F(x, y);
  }

  float joyRadius() const { return 1.15f * unit(); }
  float aimRadius() const { return 1.35f * unit(); }
  Vec2F aimCenter() const { return Vec2F(m_size[0] - 2.1f * unit(), m_size[1] - 2.0f * unit()); }

  void joyUpdate(Vec2F pos, List<InputEvent>& out) {
    float r = joyRadius();
    Vec2F d = pos - m_joyAnchor;
    float len = d.magnitude();
    // the base trails the finger so a long drag never leaves it stranded
    if (len > r) {
      m_joyAnchor = pos - d * (r / len);
      d = pos - m_joyAnchor;
    }
    m_joyPos = pos;
    bool want[4] = {d[0] < -0.3f * r, d[0] > 0.3f * r, d[1] < -0.55f * r, d[1] > 0.55f * r};
    setJoyKeys(want, out);
  }

  void setJoyKeys(bool const want[4], List<InputEvent>& out) {
    Key const keys[4] = {Key::A, Key::D, Key::W, Key::S};
    for (int i = 0; i < 4; ++i) {
      if (want[i] == m_joyKeys[i])
        continue;
      m_joyKeys[i] = want[i];
      if (want[i])
        out.append(KeyDownEvent{keys[i], KeyMod::NoMod});
      else
        out.append(KeyUpEvent{keys[i]});
    }
  }

  void releaseJoy(List<InputEvent>& out) {
    bool none[4] = {false, false, false, false};
    setJoyKeys(none, out);
    m_joyFinger.reset();
  }

  // The stick's deflection places the cursor around the middle of the screen,
  // where the player stands, so aiming never needs a finger on the view.
  void aimUpdate(Vec2F pos, List<InputEvent>& out) {
    Vec2F v = (pos - aimCenter()) / aimRadius();
    float len = v.magnitude();
    if (len > 1.0f)
      v = v / len;
    m_aimVec = v;
    Vec2F cursor = Vec2F(m_size[0] * 0.5f, m_size[1] * 0.5f) + v * (m_size[1] * 0.42f);
    pointerMove(cursor, out);
  }

  void releaseAim(List<InputEvent>& out) {
    if (m_aimPressed)
      out.append(MouseButtonUpEvent{m_aimButton, m_lastPointer});
    m_aimPressed = false;
    m_aimVec = Vec2F();
    m_aimFinger.reset();
  }

  static constexpr int SettingsRows = 4;
  static constexpr float SettingsWidth = 10.0f;
  static constexpr float SettingsRowHeight = 1.35f;

  Vec2F settingsOrigin() const {
    float u = unit();
    return Vec2F((m_size[0] - SettingsWidth * u) / 2, (m_size[1] - SettingsRows * SettingsRowHeight * u) / 2 + 0.4f * u);
  }

  void settingsTap(Vec2F pos) {
    float u = unit();
    Vec2F origin = settingsOrigin();
    if (pos[0] < origin[0] || pos[0] > origin[0] + SettingsWidth * u || pos[1] < origin[1])
      return;
    int row = (int)((pos[1] - origin[1]) / (SettingsRowHeight * u));
    if (row == 0)
      m_joystick = !m_joystick;
    else if (row == 1)
      m_aimStick = !m_aimStick;
    else if (row == 2)
      m_aimFires = !m_aimFires;
    else if (row == 3)
      m_settingsOpen = false;
    else
      return;
    saveSettings();
  }

  // relative to the working directory, which is the game data directory
  void loadSettings() {
    if (m_settingsLoaded)
      return;
    m_settingsLoaded = true;
    if (FILE* f = fopen("touchcontrols.cfg", "r")) {
      char key[32];
      int value;
      while (fscanf(f, "%31[^=]=%d\n", key, &value) == 2) {
        if (!strcmp(key, "joystick")) m_joystick = value != 0;
        else if (!strcmp(key, "aimstick")) m_aimStick = value != 0;
        else if (!strcmp(key, "aimfires")) m_aimFires = value != 0;
      }
      fclose(f);
    }
  }

  void saveSettings() const {
    if (FILE* f = fopen("touchcontrols.cfg", "w")) {
      fprintf(f, "joystick=%d\naimstick=%d\naimfires=%d\n", (int)m_joystick, (int)m_aimStick, (int)m_aimFires);
      fclose(f);
    }
  }

  int hitButton(Vec2F pos) const {
    int best = -1;
    float bestDist = 0.0f;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
      if (m_joystick && i <= 3)
        continue;
      auto const& b = m_buttons[i];
      Vec2F c = center(i);
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
    if (index == SettingsIndex) {
      m_settingsOpen = true;
      return;
    }
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
  static constexpr int SettingsIndex = 12;

  bool m_settingsLoaded = false;
  bool m_settingsOpen = false;
  bool m_joystick = true;
  bool m_aimStick = true;
  bool m_aimFires = true;

  Maybe<SDL_FingerID> m_joyFinger;
  Vec2F m_joyAnchor;
  Vec2F m_joyPos;
  bool m_joyKeys[4] = {false, false, false, false};

  Maybe<SDL_FingerID> m_aimFinger;
  Vec2F m_aimVec;
  bool m_aimPressed = false;
  MouseButton m_aimButton = MouseButton::Left;

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
