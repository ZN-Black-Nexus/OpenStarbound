#pragma once

#include "StarPane.hpp"
#include "StarOrderedMap.hpp"
#include "StarMap.hpp"
#include "StarBiMap.hpp"
#include "StarGameTimers.hpp"

namespace Star {

STAR_CLASS(PaneManager);

enum class PaneLayer {
  // A special class of window only meant to be used by PaneManager to display
  // tooltips given by Pane::createTooltip
  Tooltip,
  // A special class of window that is displayed above all other windows and
  // turns off input to other windows and the hud until it is dismissed.
  ModalWindow,
  // Window layer for regular windows that are regularly displayed and
  // dismissed and dragged around.
  Window,
  // The bottom GUI layer, for persistent hud elements that are always or almost
  // always shown.  Not key dismissable.
  Hud,
  // Layer for interface elements which are logically part of the world but
  // handled by GUI panes (such as wires)
  World
};
extern EnumMap<PaneLayer> const PaneLayerNames;


// This class handles a set of panes to be drawn as a collective windowing
// interface.  It is a set of panes on separate distinct layers, where each
// layer contains a z-ordered list of panes to display.
class PaneManager {
public:
  typedef function<void(PanePtr const&)> DismissCallback;

  PaneManager();

  // Display a pane on any given layer.  The pane lifetime in this class is
  // only during display, once dismissed, the pane is forgotten completely.
  void displayPane(PaneLayer paneLayer, PanePtr const& pane, DismissCallback onDismiss = {});

  bool isDisplayed(PanePtr const& pane) const;

  // Dismiss a given displayed pane.  Pane must already be displayed.
  void dismissPane(PanePtr const& pane);

  // Dismisses all panes in the given layers.
  void dismissAllPanes(Set<PaneLayer> const& paneLayers);
  void dismissAllPanes();

  PanePtr topPane(Set<PaneLayer> const& paneLayers) const;
  PanePtr topPane() const;

  // Brign an already displayed pane to the top of its layer.
  void bringToTop(PanePtr const& pane);

  // Position a pane adjacent to an anchor pane in a direction where
  // it will fit on the screen
  void bringPaneAdjacent(PanePtr const& anchor, PanePtr const& adjacent, int gap);

  PanePtr getPaneAt(Set<PaneLayer> const& paneLayers, Vec2I const& position) const;
  PanePtr getPaneAt(Vec2I const& position) const;
  List<PanePtr> getAllPanes();

  void setBackgroundWidget(WidgetPtr bg);

  void dismissWhere(function<bool(PanePtr const&)> func);

  // Returns the pane/widget that has captured the keyboard, if any.
  PanePtr keyboardCapturedPane() const;
  WidgetPtr keyboardCapturedWidget() const;
  // Returns true if the current widget that has captured the keyboard is
  // accepting text input.
  bool keyboardCapturedForTextInput() const;

  bool sendInputEvent(InputEvent const& event);

  void render();
  void update(float dt);

  // Touch assist (tap snapping, scale fitting) normally looks at Window and
  // ModalWindow panes only; menus built from Hud panes (title screen) opt in.
  void setTouchAssistHud(bool enabled) { m_touchAssistHud = enabled; }

private:
  bool handleInputEvent(InputEvent const& event);
  bool handleTouchEvent(InputEvent const& event);
  void updateTouch();
  Maybe<Vec2F> findSnapTarget(Vec2F pixelPos, float maxPixelDistance, bool* onButton = nullptr) const;
  // Topmost pane under a window pixel position, honoring per-pane scale.
  PanePtr paneAtPixels(Vec2F pixelPos, bool touchLayersOnly) const;
  bool touchLayer(PaneLayer layer) const;
  // Gives each control in the pane the largest zoom its neighbors leave room for.
  void assignTouchZoom(PanePtr const& pane) const;
  // The enlarged control under a window pixel position, if any.
  WidgetPtr findZoomedWidget(Vec2F pixelPos, Vec2F& centerPixels) const;
  Vec2F mapTouch(Vec2F pixelPos) const;
  // Interface scale this pane is drawn and hit-tested at. Panes too big to fit
  // the window at the global scale get their own smaller scale (Android only).
  float effectiveScale(PanePtr const& pane) const;
  // The part of a pane that is actually drawn: its background art without
  // transparent padding. Pane sizes include that padding.
  RectI visibleRect(PanePtr const& pane) const;
  // {scale, horizontal stretch} a pane is drawn and hit-tested with
  pair<float, float> paneTransform(PanePtr const& pane) const;

  Vec2I windowSize() const;
  Vec2I calculatePaneOffset(PanePtr const& pane) const;
  Vec2I calculateNewInterfacePosition(PanePtr const& pane, float interfaceScaleRatio) const;
  bool dismiss(PanePtr const& pane);

  GuiContext* m_context;
  float m_prevInterfaceScale;

  HashMap<Pane const*, pair<float, float>> m_paneScales;
  mutable HashMap<Pane const*, pair<Vec2I, RectI>> m_visibleRects;

  // Touch gestures over panes: a press is held back until it resolves into a
  // tap (snapped to the nearest button), a drag, a scroll of the scroll area
  // under the finger, or a long press (right click).
  enum class TouchPhase { None, Pending, Dragging, Scrolling, Consumed, Passthrough };
  bool m_touchAssistHud = false;
  TouchPhase m_touchPhase = TouchPhase::None;
  Vec2F m_touchStart;
  Vec2F m_touchLast;
  double m_touchStartTime = 0.0;
  WidgetPtr m_touchScrollArea;
  Vec2F m_touchScrollScale = Vec2F(1.0f, 1.0f);
  Vec2F m_touchScrollRemainder;
  Vec2F m_tooltipLastMousePixels;
  // enlarged control being touched: positions map back onto its real bounds
  Vec2F m_touchZoomCenter;
  float m_touchZoomK = 1.0f;
  bool m_touchZoomSlider = false;

  // Map of each pane layer, where the 0th pane is the topmost pane in each layer.
  Map<PaneLayer, OrderedMap<PanePtr, DismissCallback>> m_displayedPanes;

  WidgetPtr m_backgroundWidget;

  float m_tooltipMouseoverRadius;
  Vec2I m_tooltipMouseOffset;
  GameTimer m_tooltipShowTimer;
  Vec2I m_tooltipLastMousePos;
  Vec2I m_tooltipInitialPosition;
  PanePtr m_activeTooltip;
  PanePtr m_tooltipParentPane;
};

}
