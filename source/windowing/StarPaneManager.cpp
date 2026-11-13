#include "StarPaneManager.hpp"
#include "StarGameTypes.hpp"
#include "StarJsonExtra.hpp"
#include "StarAssets.hpp"
#include "StarRoot.hpp"
#include "StarButtonWidget.hpp"
#include "StarScrollArea.hpp"
#include "StarTime.hpp"
#include "StarImage.hpp"
#include "StarLogging.hpp"

namespace Star {

EnumMap<PaneLayer> const PaneLayerNames{
  {PaneLayer::Tooltip, "Tooltip"},
  {PaneLayer::ModalWindow, "ModalWindow"},
  {PaneLayer::Window, "Window"},
  {PaneLayer::Hud, "Hud"},
  {PaneLayer::World, "World"}
};

PaneManager::PaneManager()
  : m_context(GuiContext::singletonPtr()), m_prevInterfaceScale(1) {
  auto assets = Root::singleton().assets();
  m_tooltipMouseoverRadius = assets->json("/panes.config:tooltipMouseoverRadius").toFloat();
  m_tooltipMouseOffset = jsonToVec2I(assets->json("/panes.config:tooltipMouseoverOffset"));
  m_tooltipShowTimer = GameTimer(assets->json("/panes.config:tooltipMouseoverTime").toFloat());
}

void PaneManager::displayPane(PaneLayer paneLayer, PanePtr const& pane, DismissCallback onDismiss) {
  if (!m_displayedPanes[paneLayer].insertFront(pane, std::move(onDismiss)).second)
    throw GuiException("Pane displayed twice in PaneManager::displayPane");

  if (!pane->hasDisplayed() && pane->anchor() == PaneAnchor::None)
    pane->setPosition(Vec2I((windowSize() - pane->size()) / 2) + pane->centerOffset()); // center it

  pane->displayed();
}

bool PaneManager::isDisplayed(PanePtr const& pane) const {
  for (auto const& layerPair : m_displayedPanes) {
    if (layerPair.second.contains(pane))
      return true;
  }

  return false;
}

void PaneManager::dismissPane(PanePtr const& pane) {
  if (!dismiss(pane))
    throw GuiException("No such pane in PaneManager::dismissPane");
}

void PaneManager::dismissAllPanes(Set<PaneLayer> const& paneLayers) {
  for (auto const& paneLayer : paneLayers) {
    for (auto const& panePair : copy(m_displayedPanes[paneLayer]))
      dismiss(panePair.first);
  }
}

void PaneManager::dismissAllPanes() {
  for (auto layerPair : copy(m_displayedPanes)) {
    for (auto const& panePair : layerPair.second)
      dismiss(panePair.first);
  }
}

PanePtr PaneManager::topPane(Set<PaneLayer> const& paneLayers) const {
  for (auto const& layerPair : m_displayedPanes) {
    if (paneLayers.contains(layerPair.first) && !layerPair.second.empty())
      return layerPair.second.firstKey();
  }
  return {};
}

PanePtr PaneManager::topPane() const {
  for (auto const& layerPair : m_displayedPanes) {
    if (!layerPair.second.empty())
      return layerPair.second.firstKey();
  }
  return {};
}

void PaneManager::bringToTop(PanePtr const& pane) {
  for (auto& layerPair : m_displayedPanes) {
    if (layerPair.second.contains(pane)) {
      layerPair.second.toFront(pane);
      return;
    }
  }

  throw GuiException("Pane was not displayed in PaneManager::bringToTop");
}

void PaneManager::bringPaneAdjacent(PanePtr const& anchor, PanePtr const& adjacent, int gap) {
  Vec2I centerAdjacent = anchor->position() + (anchor->size() / 2) - (adjacent->size() / 2);
  centerAdjacent = centerAdjacent.piecewiseClamp(Vec2I(), windowSize() - adjacent->size()); // keeps pane inside window

  if (anchor->position()[0] + anchor->size()[0] + gap + adjacent->size()[0] <= windowSize()[0])
    adjacent->setPosition(Vec2I(anchor->position()[0] + anchor->size()[0] + gap, centerAdjacent[1])); // place to the right
  else if (anchor->position()[0] - gap - adjacent->size()[0] >= 0)
    adjacent->setPosition(Vec2I(anchor->position()[0] - gap - adjacent->size()[0], centerAdjacent[1])); // place to the left
  else if (anchor->position()[1] + anchor->size()[1] + gap + adjacent->size()[1] <= windowSize()[1])
    adjacent->setPosition(Vec2I(centerAdjacent[0], anchor->position()[1] + anchor->size()[1] + gap)); // place above
  else if (anchor->position()[1] - gap - adjacent->size()[1] >= 0)
    adjacent->setPosition(Vec2I(centerAdjacent[0], anchor->position()[1] - gap - adjacent->size()[1])); // place below
  else
    adjacent->setPosition(centerAdjacent);

  bringToTop(adjacent);
}

PanePtr PaneManager::getPaneAt(Set<PaneLayer> const& paneLayers, Vec2I const& position) const {
  for (auto const& layerPair : m_displayedPanes) {
    if (!paneLayers.contains(layerPair.first))
      continue;

    for (auto const& panePair : layerPair.second) {
      if (panePair.first->inWindow(position) && panePair.first->active())
        return panePair.first;
    }
  }

  return {};
}

PanePtr PaneManager::getPaneAt(Vec2I const& position) const {
  for (auto const& layerPair : m_displayedPanes) {
    for (auto const& panePair : layerPair.second) {
      if (panePair.first != m_activeTooltip
        && panePair.first->inWindow(position)
        && panePair.first->active())
        return panePair.first;
    }
  }

  return {};
}

List<PanePtr> PaneManager::getAllPanes() {
  List<PanePtr> list;
  for (auto const& layerPair : m_displayedPanes) {
    for (auto const& panePair : layerPair.second) {
      if (panePair.first != m_activeTooltip && panePair.first->active())
        list.append(panePair.first);
    }
  }
  return list;
}

void PaneManager::setBackgroundWidget(WidgetPtr bg) {
  m_backgroundWidget = bg;
}

void PaneManager::dismissWhere(function<bool(PanePtr const&)> func) {
  if (!func)
    return;

  for (auto& layerPair : m_displayedPanes) {
    eraseWhere(layerPair.second, [&](auto& panePair) {
      if (func(panePair.first)) {
        panePair.first->dismissed();
        if (panePair.second)
          panePair.second(panePair.first);
        return true;
      }
      return false;
    });
  }
}

PanePtr PaneManager::keyboardCapturedPane() const {
  for (auto const& layerPair : m_displayedPanes) {
    for (auto const& panePair : layerPair.second) {
      if (panePair.first->keyboardCapturer())
        return panePair.first;
    }
  }

  return {};
}

WidgetPtr PaneManager::keyboardCapturedWidget() const {
  for (auto const& layerPair : m_displayedPanes) {
    for (auto const& panePair : layerPair.second) {
      if (auto capturer = panePair.first->keyboardCapturer())
        return capturer;
    }
  }

  return {};
}

bool PaneManager::keyboardCapturedForTextInput() const {
  if (auto widget = keyboardCapturedWidget())
    return widget->keyboardCaptureMode() == KeyboardCaptureMode::TextInput;
  return false;
}

namespace {
  struct ScaleScope {
    GuiContext* context;
    float previous;
    float previousStretch;
    ScaleScope(GuiContext* c, pair<float, float> const& t) : context(c), previous(c->interfaceScale()), previousStretch(c->interfaceStretch()) {
      context->setInterfaceScale(t.first);
      context->setInterfaceStretch(t.second);
    }
    ~ScaleScope() {
      context->setInterfaceScale(previous);
      context->setInterfaceStretch(previousStretch);
    }
  };

  void collectButtons(WidgetPtr const& widget, List<shared_ptr<ButtonWidget>>& out) {
    if (!widget || !widget->visibility())
      return;
    if (auto button = as<ButtonWidget>(widget)) {
      if (!button->mouseTransparent() && button->interactive() && !button->disabled())
        out.append(button);
    }
    for (size_t i = 0; i < widget->numChildren(); ++i)
      collectButtons(widget->getChildNum(i), out);
  }

  // Deepest scrollable ScrollArea whose content region contains the position.
  shared_ptr<ScrollArea> findScrollArea(WidgetPtr const& widget, Vec2I const& pos) {
    if (!widget || !widget->visibility())
      return {};
    for (size_t i = 0; i < widget->numChildren(); ++i) {
      if (auto found = findScrollArea(widget->getChildNum(i), pos))
        return found;
    }
    if (auto area = as<ScrollArea>(widget)) {
      Vec2I areaSize = area->areaSize();
      Vec2I min = area->screenPosition() + Vec2I(0, area->size()[1] - areaSize[1]);
      Vec2I maxScroll = area->maxScrollPosition();
      if (RectI::withSize(min, areaSize).contains(pos) && (maxScroll[0] > 0 || maxScroll[1] > 0))
        return area;
    }
    return {};
  }

  Maybe<Vec2F> eventPixels(InputEvent const& event) {
    if (auto e = event.ptr<MouseMoveEvent>())
      return e->mousePosition;
    if (auto e = event.ptr<MouseButtonDownEvent>())
      return e->mousePosition;
    if (auto e = event.ptr<MouseButtonUpEvent>())
      return e->mousePosition;
    if (auto e = event.ptr<MouseWheelEvent>())
      return e->mousePosition;
    return {};
  }

  float distanceToRect(RectI const& rect, Vec2F const& p) {
    float dx = std::max({(float)rect.xMin() - p[0], 0.0f, p[0] - (float)rect.xMax()});
    float dy = std::max({(float)rect.yMin() - p[1], 0.0f, p[1] - (float)rect.yMax()});
    return std::sqrt(dx * dx + dy * dy);
  }
}

RectI PaneManager::visibleRect(PanePtr const& pane) const {
  RectI full = RectI::withSize(Vec2I(), pane->size());
  auto cached = m_visibleRects.ptr(pane.get());
  if (cached && cached->first == pane->size())
    return cached->second;

  RectI result = RectI::null();
  try {
    auto assets = Root::singleton().assets();
    auto bg = pane->getBG();
    int y = 0;
    for (auto const& path : {bg.footer, bg.body, bg.header}) {
      if (path.empty())
        continue;
      auto image = assets->image(path);
      int w = image->width(), h = image->height();
      int x0 = w, y0 = h, x1 = 0, y1 = 0;
      for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
          if (image->get(px, py)[3] > 16) {
            x0 = std::min(x0, px); x1 = std::max(x1, px + 1);
            y0 = std::min(y0, py); y1 = std::max(y1, py + 1);
          }
        }
      }
      if (x1 > x0 && y1 > y0)
        result.combine(RectI(x0, y + y0, x1, y + y1));
      y += h;
    }
  } catch (std::exception const&) {
    result = RectI::null();
  }
  if (result.isNull() || result.width() <= 0 || result.height() <= 0)
    result = full;
  m_visibleRects[pane.get()] = {pane->size(), result};
  return result;
}

float PaneManager::effectiveScale(PanePtr const& pane) const {
  return paneFit(pane).scale;
}

int PaneManager::foldLine(PanePtr const& pane) const {
  auto cached = m_foldLines.ptr(pane.get());
  if (cached && cached->first == pane->size())
    return cached->second;

  RectI visible = visibleRect(pane);
  List<Vec2I> spans; // vertical extent of each leaf widget, relative to the pane
  int base = pane->screenPosition()[1];
  function<void(WidgetPtr const&)> collect = [&](WidgetPtr const& widget) {
    if (!widget || !widget->visibility())
      return;
    if (widget->numChildren() == 0) {
      RectI rect = widget->screenBoundRect();
      spans.append(Vec2I(rect.yMin() - base, rect.yMax() - base));
    }
    for (size_t i = 0; i < widget->numChildren(); ++i)
      collect(widget->getChildNum(i));
  };
  for (size_t i = 0; i < pane->numChildren(); ++i)
    collect(pane->getChildNum(i));

  // split near the middle, on the row that cuts through the fewest widgets
  int mid = visible.yMin() + visible.height() / 2;
  int best = mid;
  int bestScore = std::numeric_limits<int>::max();
  for (int y = visible.yMin() + visible.height() * 35 / 100; y <= visible.yMin() + visible.height() * 65 / 100; ++y) {
    int score = std::abs(y - mid);
    for (auto const& span : spans) {
      if (span[0] < y && y < span[1])
        score += 1000;
    }
    if (score < bestScore) {
      bestScore = score;
      best = y;
    }
  }
  m_foldLines[pane.get()] = {pane->size(), best};
  return best;
}

PaneManager::PaneFit PaneManager::paneFit(PanePtr const& pane) const {
  float global = m_globalScale;
  PaneFit result{global, false, 0};
#ifdef STAR_SYSTEM_ANDROID
  for (auto const& layerPair : m_displayedPanes) {
    if (!layerPair.second.contains(pane))
      continue;
    if (!touchLayer(layerPair.first))
      return result;
    RectI visible = visibleRect(pane);
    Vec2F size = Vec2F(visible.size());
    if (size[0] <= 0 || size[1] <= 0)
      return result;
    Vec2F window = Vec2F(m_context->renderer()->screenSize());
    float fit = std::min(window[0] / size[0], window[1] / size[1]);

    if (fit < global * 0.98f && size[1] > size[0] && layerPair.first != PaneLayer::Window) {
      int fold = foldLine(pane);
      float columnHeight = std::max(visible.yMax() - fold, fold - visible.yMin());
      float foldedFit = std::min(window[0] / (size[0] * 2 + 4), window[1] / columnHeight);
      if (foldedFit > fit * 1.25f) {
        result.folded = true;
        result.foldY = fold;
        fit = foldedFit;
      }
    }

    // The tolerance keeps window-sized panes (sized from the current scale,
    // rounded up) from shrinking themselves.
    if (fit >= global * 0.98f) {
      // Room to spare: enlarge menus and windows past the chosen scale so
      // their buttons are finger sized, without zooming the HUD or the world.
      // In-game windows keep to half the width so two fit side by side.
      if (layerPair.first == PaneLayer::Window)
        fit = std::min(fit, 0.5f * window[0] / size[0]);
      float enlarged = std::floor(std::min(global * 2.0f, fit * 0.99f) * 8.0f) / 8.0f;
      result.scale = std::max(global, enlarged);
    } else {
      result.scale = std::max(1.0f, std::floor(fit * 8.0f) / 8.0f);
    }
    return result;
  }
#else
  (void)pane;
#endif
  return result;
}

PaneManager::FoldGeom PaneManager::foldGeom(PanePtr const& pane, int foldY) const {
  RectI visible = visibleRect(pane);
  Vec2I window = windowSize();
  int width = visible.width();
  int gap = 4;
  int topHeight = visible.yMax() - foldY;
  int bottomHeight = foldY - visible.yMin();
  int x0 = (window[0] - (width * 2 + gap)) / 2;
  int x1 = x0 + width + gap;
  int top = (window[1] + std::max(topHeight, bottomHeight)) / 2;
  FoldGeom geom;
  geom.originL = Vec2I(x0 - visible.xMin(), top - visible.yMax());
  geom.regionL = RectI(x0, top - topHeight, x0 + width, top);
  geom.originR = Vec2I(x1 - visible.xMin(), top - foldY);
  geom.regionR = RectI(x1, top - bottomHeight, x1 + width, top);
  return geom;
}

void PaneManager::applyFoldOffset(PanePtr const& pane, PaneFit const& fit, Vec2F pixelPos) const {
  if (!fit.folded)
    return;
  auto geom = foldGeom(pane, fit.foldY);
  Vec2I pos = Vec2I::floor(pixelPos / fit.scale);
  // off both columns: park the pane where nothing can be hit
  Vec2I origin = Vec2I(-100000, -100000);
  if (geom.regionL.contains(pos))
    origin = geom.originL;
  else if (geom.regionR.contains(pos))
    origin = geom.originR;
  pane->setDrawingOffset(origin - pane->relativePosition());
}

pair<float, float> PaneManager::paneTransform(PanePtr const& pane) const {
  float scale = paneFit(pane).scale;
  float stretch = 1.0f;
  return {scale, stretch};
}

bool PaneManager::touchLayer(PaneLayer layer) const {
  return layer == PaneLayer::ModalWindow || layer == PaneLayer::Window || (layer == PaneLayer::Hud && m_touchAssistHud);
}

PanePtr PaneManager::paneAtPixels(Vec2F pixelPos, bool touchLayersOnly) const {
  for (auto const& layerPair : m_displayedPanes) {
    if (touchLayersOnly && !touchLayer(layerPair.first))
      continue;
    for (auto const& panePair : layerPair.second) {
      auto const& pane = panePair.first;
      if (pane == m_activeTooltip || !pane->active())
        continue;
      auto fit = paneFit(pane);
      ScaleScope scope(m_context, {fit.scale, 1.0f});
      Vec2I ipos = Vec2I::floor(pixelPos / fit.scale);
      if (fit.folded) {
        auto geom = foldGeom(pane, fit.foldY);
        if (geom.regionL.contains(ipos) || geom.regionR.contains(ipos))
          return pane;
      } else if (pane->inWindow(ipos)) {
        return pane;
      }
      // a modal window blocks everything beneath it
      if (touchLayersOnly && layerPair.first == PaneLayer::ModalWindow)
        return {};
    }
  }
  return {};
}

Maybe<Vec2F> PaneManager::findSnapTarget(Vec2F pixelPos, float maxPixelDistance, bool* onButton) const {
  Maybe<Vec2F> best;
  float bestDist = maxPixelDistance;
  bool foundModal = false;

  for (auto layer : {PaneLayer::ModalWindow, PaneLayer::Window, PaneLayer::Hud}) {
    if (layer == PaneLayer::Hud && !m_touchAssistHud)
      continue;
    auto layerIt = m_displayedPanes.find(layer);
    if (layerIt == m_displayedPanes.end())
      continue;
    for (auto const& panePair : layerIt->second) {
      auto const& pane = panePair.first;
      if (!pane->active())
        continue;
      auto t = paneTransform(pane);
      float scale = t.first;
      ScaleScope scope(m_context, t);
      Vec2F interfacePos = Vec2F(pixelPos[0] / (scale * t.second), pixelPos[1] / scale);
      Vec2I ipos = Vec2I::floor(interfacePos);
      List<shared_ptr<ButtonWidget>> buttons;
      collectButtons(pane, buttons);
      auto fit = paneFit(pane);
      FoldGeom geom;
      if (fit.folded)
        geom = foldGeom(pane, fit.foldY);
      for (int column = 0; column < (fit.folded ? 2 : 1); ++column)
      for (auto const& button : buttons) {
        if (fit.folded)
          pane->setDrawingOffset((column == 0 ? geom.originL : geom.originR) - pane->relativePosition());
        RectI rect = button->screenBoundRect();
        if (fit.folded && !(column == 0 ? geom.regionL : geom.regionR).contains(rect.center()))
          continue;
        if (rect.contains(ipos)) {
          if (onButton)
            *onButton = true;
          return {};  // already on a button; leave the tap alone
        }
        float dist = distanceToRect(rect, interfacePos) * scale;
        if (dist <= bestDist) {
          bestDist = dist;
          best = Vec2F(rect.center()[0] * scale * t.second, rect.center()[1] * scale);
        }
      }
      if (layer == PaneLayer::ModalWindow) {
        foundModal = true;
        break;
      }
      if (fit.folded ? (geom.regionL.contains(ipos) || geom.regionR.contains(ipos)) : pane->inWindow(ipos))
        break;
    }
    if (foundModal)
      break;
  }
  return best;
}

bool PaneManager::handleTouchEvent(InputEvent const& event) {
  Vec2F pixels = Vec2F(m_context->windowSize());
  float minDim = std::min(pixels[0], pixels[1]);
  float snapDistance = 0.055f * minDim;
  float dragSlop = std::max(12.0f, 0.025f * minDim);

  if (auto down = event.ptr<MouseButtonDownEvent>()) {
    if (down->mouseButton != MouseButton::Left || m_touchPhase != TouchPhase::None)
      return handleInputEvent(event);
    Vec2F pos = Vec2F(down->mousePosition);
    if (!paneAtPixels(pos, true) && !findSnapTarget(pos, snapDistance)) {
      m_touchPhase = TouchPhase::Passthrough;
      return handleInputEvent(event);
    }
    m_touchPhase = TouchPhase::Pending;
    m_touchStart = m_touchLast = pos;
    m_touchStartTime = Time::monotonicTime();
    return true;
  }

  if (auto move = event.ptr<MouseMoveEvent>()) {
    Vec2F pos = Vec2F(move->mousePosition);
    if (m_touchPhase == TouchPhase::Pending) {
      m_touchLast = pos;
      if (vmag(pos - m_touchStart) < dragSlop)
        return true;
      m_touchScrollArea.reset();
      if (auto pane = paneAtPixels(m_touchStart, true)) {
        auto t = paneTransform(pane);
        ScaleScope scope(m_context, t);
        Vec2F ppu = Vec2F(t.first * t.second, t.first);
        applyFoldOffset(pane, paneFit(pane), m_touchStart);
        if (auto area = findScrollArea(pane, Vec2I::floor(Vec2F(m_touchStart[0] / ppu[0], m_touchStart[1] / ppu[1])))) {
          m_touchScrollArea = area;
          m_touchScrollScale = ppu;
          m_touchScrollRemainder = Vec2F();
        }
      }
      if (m_touchScrollArea) {
        m_touchPhase = TouchPhase::Scrolling;
        m_touchLast = m_touchStart;
      } else {
        m_touchPhase = TouchPhase::Dragging;
        handleInputEvent(MouseButtonDownEvent{MouseButton::Left, m_touchStart});
        handleInputEvent(MouseMoveEvent{pos - m_touchStart, pos});
        return true;
      }
    }
    if (m_touchPhase == TouchPhase::Scrolling) {
      // content follows the finger
      Vec2F delta = Vec2F((pos[0] - m_touchLast[0]) / m_touchScrollScale[0], (pos[1] - m_touchLast[1]) / m_touchScrollScale[1]) + m_touchScrollRemainder;
      Vec2I whole = Vec2I(delta);
      m_touchScrollRemainder = delta - Vec2F(whole);
      m_touchLast = pos;
      if (auto area = as<ScrollArea>(m_touchScrollArea))
        area->scrollAreaBy(-whole);
      return true;
    }
    if (m_touchPhase == TouchPhase::Consumed)
      return true;
    return handleInputEvent(event);
  }

  if (auto up = event.ptr<MouseButtonUpEvent>()) {
    if (up->mouseButton != MouseButton::Left)
      return handleInputEvent(event);
    auto phase = m_touchPhase;
    m_touchPhase = TouchPhase::None;
    m_touchScrollArea.reset();
    if (phase == TouchPhase::Pending) {
      // a tap: land it on the nearest button if it only just missed one
      Vec2F pos = m_touchStart;
      if (auto target = findSnapTarget(pos, snapDistance))
        pos = *target;
      Vec2F ipos = pos;
      handleInputEvent(MouseMoveEvent{Vec2F(), ipos});
      handleInputEvent(MouseButtonDownEvent{MouseButton::Left, ipos});
      handleInputEvent(MouseButtonUpEvent{MouseButton::Left, ipos});
      return true;
    }
    if (phase == TouchPhase::Scrolling || phase == TouchPhase::Consumed)
      return true;
    return handleInputEvent(event);
  }

  return handleInputEvent(event);
}

void PaneManager::updateTouch() {
  // long press away from buttons acts as a right click
  if (m_touchPhase != TouchPhase::Pending || Time::monotonicTime() - m_touchStartTime < 0.5)
    return;
  Vec2F pixels = Vec2F(m_context->windowSize());
  bool onButton = false;
  if (findSnapTarget(m_touchStart, 0.055f * std::min(pixels[0], pixels[1]), &onButton) || onButton)
    return;
  m_touchPhase = TouchPhase::Consumed;
  Vec2F ipos = m_touchStart;
  handleInputEvent(MouseMoveEvent{Vec2F(), ipos});
  handleInputEvent(MouseButtonDownEvent{MouseButton::Right, ipos});
  handleInputEvent(MouseButtonUpEvent{MouseButton::Right, ipos});
}

bool PaneManager::sendInputEvent(InputEvent const& event) {
  m_globalScale = m_context->interfaceScale();
#ifdef STAR_SYSTEM_ANDROID
  return handleTouchEvent(event);
#else
  return handleInputEvent(event);
#endif
}

bool PaneManager::handleInputEvent(InputEvent const& event) {
  if (event.is<MouseMoveEvent>()) {
    m_tooltipLastMousePos = *m_context->mousePosition(event);
    m_tooltipLastMousePixels = Vec2F(event.get<MouseMoveEvent>().mousePosition);

    for (auto const& layerPair : m_displayedPanes) {
      for (auto const& panePair : layerPair.second) {
        if (panePair.first->dragActive()) {
          ScaleScope scope(m_context, paneTransform(panePair.first));
          panePair.first->drag(*m_context->mousePosition(event));
          return true;
        }
      }
    }
  }

  if (event.is<MouseButtonDownEvent>()) {
    m_tooltipShowTimer.reset();
    if (m_activeTooltip) {
      dismiss(m_activeTooltip);
      m_activeTooltip.reset();
      m_tooltipParentPane.reset();
      m_tooltipShowTimer.reset();
    }
  }

  if (event.is<MouseButtonUpEvent>()) {
    for (auto const& layerPair : m_displayedPanes) {
      for (auto const& panePair : layerPair.second) {
        if (panePair.first->dragActive()) {
          panePair.first->setDragActive(false, {});
          return true;
        }
      }
    }
  }

  // The gui close event can only be intercepted by a pane that has captured
  // the keyboard otherwise it will always be used to close first before being
  // a normal event. This is so a window can control its own closing if it
  // really needs to (like the keybindings window).
  if (event.is<KeyDownEvent>() && m_context->actions(event).contains(InterfaceAction::GuiClose)) {
    if (auto top = topPane({PaneLayer::ModalWindow, PaneLayer::Window})) {
      dismiss(top);
      return true;
    }
  }

  // If there is a pane that has captured the keyboard, keyboard events will
  // ONLY be sent to it.
  auto keyCapturePane = keyboardCapturedPane();
  if (keyCapturePane && (event.is<KeyDownEvent>() || event.is<KeyUpEvent>() || event.is<TextInputEvent>()))
    { ScaleScope scope(m_context, paneTransform(keyCapturePane)); return keyCapturePane->sendEvent(event); }

  bool foundModal = false;
  for (auto& layerPair : m_displayedPanes) {
    for (auto const& panePair : copy(layerPair.second)) {
      bool consumed;
      {
        auto fit = paneFit(panePair.first);
        ScaleScope scope(m_context, {fit.scale, 1.0f});
        if (auto pixels = eventPixels(event))
          applyFoldOffset(panePair.first, fit, *pixels);
        consumed = panePair.first->sendEvent(event);
      }
      if (consumed) {
        if (event.is<MouseButtonDownEvent>())
          layerPair.second.toFront(panePair.first);
        return true;
      }
      // If any modal windows are shown, Only the first modal window should
      // have a chance to consume the input event and all other panes below it
      // including different layers should ignore it.
      if (layerPair.first == PaneLayer::ModalWindow) {
        foundModal = true;
        break;
      }
    }
    if (foundModal)
      break;
  }

  return false;
}

void PaneManager::render() {
  if (m_backgroundWidget) {
    auto size = m_backgroundWidget->size();
    m_backgroundWidget->setPosition(Vec2I((windowSize()[0] - size[0]) / 2, (windowSize()[1] - size[1]) / 2));
    m_backgroundWidget->render(RectI(Vec2I(), windowSize()));
  }

  float globalScale = m_context->interfaceScale();
  m_globalScale = globalScale;
  auto renderer = m_context->renderer();
  HashMap<Pane const*, pair<float, float>> newScales;
  for (auto const& layerPair : reverseIterate(m_displayedPanes)) {
    for (auto const& panePair : reverseIterate(layerPair.second)) {
      auto const& pane = panePair.first;
      if (pane->active()) {
        auto t = paneTransform(pane);
        float scale = t.first, stretch = t.second;
        auto last = m_paneScales.value(pane.get(), {m_prevInterfaceScale, 1.0f});
        auto fit = paneFit(pane);
        if (fit.folded) {
          ScaleScope scope(m_context, t);
          auto geom = foldGeom(pane, fit.foldY);
          if (t != last)
            Logger::info("PaneManager: pane {}x{} folded at row {} and drawn at scale {} (ui scale {})",
                pane->size()[0], pane->size()[1], fit.foldY, scale, globalScale);
          newScales[pane.get()] = t;
          pane->setDrawingOffset(geom.originL - pane->relativePosition());
          pane->render(geom.regionL);
          pane->setDrawingOffset(geom.originR - pane->relativePosition());
          pane->render(geom.regionR);
          continue;
        }
        bool adapted = scale != globalScale || stretch != 1.0f;
        // where the pane's center was on screen, in pixels, before any change
        Vec2F center = Vec2F(pane->position()) + Vec2F(pane->size()) / 2;
        center = Vec2F(center[0] * last.first * last.second, center[1] * last.first);

        ScaleScope scope(m_context, t);
        if (scale < globalScale) {
          pane->setPosition(Vec2I((windowSize() - pane->size()) / 2));
        } else if (t != last) {
          if (pane->anchor() == PaneAnchor::None && (adapted || last.second != 1.0f))
            pane->setPosition(Vec2I::round(Vec2F(center[0] / (scale * stretch), center[1] / scale) - Vec2F(pane->size()) / 2));
          else if (scale != last.first)
            pane->setPosition(calculateNewInterfacePosition(pane, scale / last.first));
        }
        if (adapted && scale >= globalScale && pane->anchor() == PaneAnchor::None)
          pane->setPosition(pane->position().piecewiseClamp(Vec2I(), Vec2I().piecewiseMax(windowSize() - pane->size())));

        newScales[pane.get()] = t;
        Vec2I drawingOffset = calculatePaneOffset(pane);
        RectI visible = visibleRect(pane);
        // center what is drawn rather than the padded pane
        if (adapted && pane->anchor() == PaneAnchor::Center)
          drawingOffset += pane->size() / 2 - visible.center();
        if (t != last)
          Logger::info("PaneManager: pane {}x{} visible {}x{} at {},{} drawn at scale {} (ui scale {}, screen {}x{})",
              pane->size()[0], pane->size()[1], visible.width(), visible.height(), visible.xMin(), visible.yMin(),
              scale, globalScale, renderer->screenSize()[0], renderer->screenSize()[1]);
        pane->setDrawingOffset(drawingOffset);
        if (stretch != 1.0f)
          renderer->setScreenStretch(stretch);
        pane->render(RectI(Vec2I(), windowSize()));
        if (stretch != 1.0f)
          renderer->setScreenStretch(1.0f);
      }
    }
  }
  m_paneScales = std::move(newScales);

  m_context->resetInterfaceScissorRect();
  m_prevInterfaceScale = globalScale;
}

void PaneManager::update(float dt) {
  m_globalScale = m_context->interfaceScale();
#ifdef STAR_SYSTEM_ANDROID
  updateTouch();
#endif

  // resolved in pixels because panes may be drawn at their own scale
  auto newTooltipParentPane = paneAtPixels(m_tooltipLastMousePixels, false);

  bool updateTooltip = m_tooltipShowTimer.tick(dt) || (m_activeTooltip && (
    vmag(m_tooltipInitialPosition - m_tooltipLastMousePos) > m_tooltipMouseoverRadius
    || m_tooltipParentPane != newTooltipParentPane));

  if (updateTooltip) {
    if (m_activeTooltip) {
      dismiss(m_activeTooltip);
      m_activeTooltip.reset();
      m_tooltipParentPane.reset();
    }

    m_tooltipShowTimer.reset();
    if (newTooltipParentPane) {
      auto parentT = paneTransform(newTooltipParentPane);
      PanePtr tooltip;
      {
        ScaleScope scope(m_context, parentT);
        applyFoldOffset(newTooltipParentPane, paneFit(newTooltipParentPane), m_tooltipLastMousePixels);
        tooltip = newTooltipParentPane->createTooltip(Vec2I::floor(Vec2F(m_tooltipLastMousePixels[0] / (parentT.first * parentT.second), m_tooltipLastMousePixels[1] / parentT.first)));
      }
      if (tooltip) {
        m_activeTooltip = std::move(tooltip);
        m_tooltipParentPane = std::move(newTooltipParentPane);
        m_tooltipInitialPosition = m_tooltipLastMousePos;
        displayPane(PaneLayer::Tooltip, m_activeTooltip);
      }
    }
  }

  if (m_activeTooltip) {
    Vec2I offsetDirection = Vec2I::filled(1);
    Vec2I offsetAdjust = Vec2I();

    if (m_tooltipLastMousePos[0] + m_tooltipMouseOffset[0] + m_activeTooltip->size()[0] > (int)m_context->windowWidth() / m_context->interfaceScale()) {
      offsetDirection[0] = -1;
      offsetAdjust[0] = -m_activeTooltip->size()[0];
    }

    if (m_tooltipLastMousePos[1] + m_tooltipMouseOffset[1] - m_activeTooltip->size()[1] < 0)
      offsetDirection[1] = -1;
    else
      offsetAdjust[1] = -m_activeTooltip->size()[1];

    m_activeTooltip->setPosition(m_tooltipLastMousePos + (offsetAdjust + m_tooltipMouseOffset.piecewiseMultiply(offsetDirection)));
  }

  for (auto const& layerPair : m_displayedPanes) {
    for (auto const& panePair : copy(layerPair.second)) {
      if (panePair.first->isDismissed())
        dismiss(panePair.first);
    }
  }

  for (auto const& layerPair : reverseIterate(m_displayedPanes)) {
    for (auto const& panePair : reverseIterate(layerPair.second)) {
      ScaleScope scope(m_context, paneTransform(panePair.first));
      panePair.first->tick(dt);
      if (panePair.first->active())
        panePair.first->update(dt);
    }
  }
}

Vec2I PaneManager::windowSize() const {
  return Vec2I(m_context->windowInterfaceSize());
}

Vec2I PaneManager::calculatePaneOffset(PanePtr const& pane) const {
  Vec2I size = pane->size();
  switch (pane->anchor()) {
    case PaneAnchor::None:
      return pane->anchorOffset();
    case PaneAnchor::BottomLeft:
      return pane->anchorOffset();
    case PaneAnchor::BottomRight:
      return pane->anchorOffset() + Vec2I{windowSize()[0] - size[0], 0};
    case PaneAnchor::TopLeft:
      return pane->anchorOffset() + Vec2I{0, windowSize()[1] - size[1]};
    case PaneAnchor::TopRight:
      return pane->anchorOffset() + (windowSize() - size);
    case PaneAnchor::CenterTop:
      return pane->anchorOffset() + Vec2I{(windowSize()[0] - size[0]) / 2, windowSize()[1] - size[1]};
    case PaneAnchor::CenterBottom:
      return pane->anchorOffset() + Vec2I{(windowSize()[0] - size[0]) / 2, 0};
    case PaneAnchor::CenterLeft:
      return pane->anchorOffset() + Vec2I{0, (windowSize()[1] - size[1]) / 2};
    case PaneAnchor::CenterRight:
      return pane->anchorOffset() + Vec2I{windowSize()[0] - size[0], (windowSize()[1] - size[1]) / 2};
    case PaneAnchor::Center:
      return pane->anchorOffset() + ((windowSize() - size) / 2);
    default:
      return pane->anchorOffset();
  }
}

Vec2I PaneManager::calculateNewInterfacePosition(PanePtr const& pane, float interfaceScaleRatio) const {
  Vec2F position(pane->relativePosition());
  Vec2F size(pane->size());
  Mat3F scale;
  switch (pane->anchor()) {
    case PaneAnchor::None:
      scale = Mat3F::scaling(interfaceScaleRatio, Vec2F(windowSize()) / 2);
      break;
    case PaneAnchor::BottomLeft:
      scale = Mat3F::scaling(interfaceScaleRatio);
      break;
    case PaneAnchor::BottomRight:
      scale = Mat3F::scaling(interfaceScaleRatio, {size[0], 0});
      break;
    case PaneAnchor::TopLeft:
      scale = Mat3F::scaling(interfaceScaleRatio, {0, size[1]});
      break;
    case PaneAnchor::TopRight:
      scale = Mat3F::scaling(interfaceScaleRatio, size);
      break;
    case PaneAnchor::CenterTop:
      scale = Mat3F::scaling(interfaceScaleRatio, {size[0] / 2, size[1]});
      break;
    case PaneAnchor::CenterBottom:
      scale = Mat3F::scaling(interfaceScaleRatio, {size[0] / 2, 0});
      break;
    case PaneAnchor::CenterLeft:
      scale = Mat3F::scaling(interfaceScaleRatio, {0, size[1] / 2});
      break;
    case PaneAnchor::CenterRight:
      scale = Mat3F::scaling(interfaceScaleRatio, {size[0], size[1] / 2});
      break;
    case PaneAnchor::Center:
      scale = Mat3F::scaling(interfaceScaleRatio, size / 2);
      break;
    default:
      scale = Mat3F::scaling(interfaceScaleRatio, Vec2F(windowSize()) / 2);
  }
  return Vec2I::round((scale * Vec3F(position, 0)).vec2());
}

bool PaneManager::dismiss(PanePtr const& pane) {
  bool dismissed = false;
  for (auto& layerPair : m_displayedPanes) {
    if (auto panePair = layerPair.second.maybeTake(pane)) {
      dismissed = true;
      panePair->first->dismissed();
      if (panePair->second)
        panePair->second(pane);
    }
  }

  return dismissed;
}

}
