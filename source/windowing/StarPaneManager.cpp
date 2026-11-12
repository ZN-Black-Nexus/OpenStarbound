#include "StarPaneManager.hpp"
#include "StarGameTypes.hpp"
#include "StarJsonExtra.hpp"
#include "StarAssets.hpp"
#include "StarRoot.hpp"
#include "StarButtonWidget.hpp"

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
    ScaleScope(GuiContext* c, float scale) : context(c), previous(c->interfaceScale()) { context->setInterfaceScale(scale); }
    ~ScaleScope() { context->setInterfaceScale(previous); }
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

  float distanceToRect(RectI const& rect, Vec2F const& p) {
    float dx = std::max({(float)rect.xMin() - p[0], 0.0f, p[0] - (float)rect.xMax()});
    float dy = std::max({(float)rect.yMin() - p[1], 0.0f, p[1] - (float)rect.yMax()});
    return std::sqrt(dx * dx + dy * dy);
  }
}

float PaneManager::effectiveScale(PanePtr const& pane) const {
  float global = m_context->interfaceScale();
#ifdef STAR_SYSTEM_ANDROID
  for (auto const& layerPair : m_displayedPanes) {
    if (!layerPair.second.contains(pane))
      continue;
    if (layerPair.first == PaneLayer::World || layerPair.first == PaneLayer::Tooltip)
      return global;
    if (layerPair.first == PaneLayer::Hud && !m_touchAssistHud)
      return global;
    Vec2F size = Vec2F(pane->size());
    if (size[0] <= 0 || size[1] <= 0)
      return global;
    Vec2F window = Vec2F(m_context->windowSize());
    // the small tolerance keeps window-sized panes (sized from the current
    // scale, rounded up) from shrinking themselves
    float fit = std::floor(std::min(window[0] / size[0], window[1] / size[1]) + 0.02f);
    return std::min(global, std::max(1.0f, fit));
  }
#else
  (void)pane;
#endif
  return global;
}

Maybe<Vec2F> PaneManager::findSnapTarget(Vec2F pixelPos, float maxPixelDistance) const {
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
      float scale = effectiveScale(pane);
      ScaleScope scope(m_context, scale);
      Vec2F interfacePos = pixelPos / scale;
      Vec2I ipos = Vec2I::floor(interfacePos);
      List<shared_ptr<ButtonWidget>> buttons;
      collectButtons(pane, buttons);
      for (auto const& button : buttons) {
        RectI rect = button->screenBoundRect();
        if (rect.contains(ipos))
          return {};  // already on a button; leave the tap alone
        float dist = distanceToRect(rect, interfacePos) * scale;
        if (dist <= bestDist) {
          bestDist = dist;
          best = Vec2F(rect.center()) * scale;
        }
      }
      if (layer == PaneLayer::ModalWindow) {
        foundModal = true;
        break;
      }
      if (pane->inWindow(ipos))
        break;
    }
    if (foundModal)
      break;
  }
  return best;
}

InputEvent PaneManager::snapTouchEvent(InputEvent const& event) {
#ifdef STAR_SYSTEM_ANDROID
  if (auto down = event.ptr<MouseButtonDownEvent>()) {
    m_touchDown = true;
    m_touchSnapOffset = Vec2F();
    Vec2F pixels = Vec2F(m_context->windowSize());
    float maxDistance = 0.055f * std::min(pixels[0], pixels[1]);
    if (auto target = findSnapTarget(down->mousePosition, maxDistance)) {
      m_touchSnapOffset = *target - down->mousePosition;
      auto result = *down;
      result.mousePosition += m_touchSnapOffset;
      return result;
    }
    return event;
  }
  if (m_touchDown) {
    if (auto move = event.ptr<MouseMoveEvent>()) {
      auto result = *move;
      result.mousePosition += m_touchSnapOffset;
      return result;
    }
    if (auto up = event.ptr<MouseButtonUpEvent>()) {
      auto result = *up;
      result.mousePosition += m_touchSnapOffset;
      m_touchDown = false;
      m_touchSnapOffset = Vec2F();
      return result;
    }
  }
#endif
  return event;
}

bool PaneManager::sendInputEvent(InputEvent const& event) {
  return handleInputEvent(snapTouchEvent(event));
}

bool PaneManager::handleInputEvent(InputEvent const& event) {
  if (event.is<MouseMoveEvent>()) {
    m_tooltipLastMousePos = *m_context->mousePosition(event);

    for (auto const& layerPair : m_displayedPanes) {
      for (auto const& panePair : layerPair.second) {
        if (panePair.first->dragActive()) {
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
    { ScaleScope scope(m_context, effectiveScale(keyCapturePane)); return keyCapturePane->sendEvent(event); }

  bool foundModal = false;
  for (auto& layerPair : m_displayedPanes) {
    for (auto const& panePair : copy(layerPair.second)) {
      bool consumed;
      {
        ScaleScope scope(m_context, effectiveScale(panePair.first));
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
  HashMap<Pane const*, float> newScales;
  for (auto const& layerPair : reverseIterate(m_displayedPanes)) {
    for (auto const& panePair : reverseIterate(layerPair.second)) {
      auto const& pane = panePair.first;
      if (pane->active()) {
        float scale = effectiveScale(pane);
        ScaleScope scope(m_context, scale);
        float lastScale = m_paneScales.value(pane.get(), m_prevInterfaceScale);
        if (scale < globalScale)
          pane->setPosition(Vec2I((windowSize() - pane->size()) / 2));
        else if (scale != lastScale)
          pane->setPosition(calculateNewInterfacePosition(pane, scale / lastScale));

        newScales[pane.get()] = scale;
        pane->setDrawingOffset(calculatePaneOffset(pane));
        pane->render(RectI(Vec2I(), windowSize()));
      }
    }
  }
  m_paneScales = std::move(newScales);

  m_context->resetInterfaceScissorRect();
  m_prevInterfaceScale = globalScale;
}

void PaneManager::update(float dt) {
  auto newTooltipParentPane = getPaneAt(m_tooltipLastMousePos);

  bool updateTooltip = m_tooltipShowTimer.tick(dt) || (m_activeTooltip && (
    vmag(m_tooltipInitialPosition - m_tooltipLastMousePos) > m_tooltipMouseoverRadius
    || m_tooltipParentPane != newTooltipParentPane
    || !m_tooltipParentPane->inWindow(m_tooltipLastMousePos))); 

  if (updateTooltip) {
    if (m_activeTooltip) {
      dismiss(m_activeTooltip);
      m_activeTooltip.reset();
      m_tooltipParentPane.reset();
    }

    m_tooltipShowTimer.reset();
    if (newTooltipParentPane) {
      if (auto tooltip = newTooltipParentPane->createTooltip(m_tooltipLastMousePos)) {
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
      ScaleScope scope(m_context, effectiveScale(panePair.first));
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
