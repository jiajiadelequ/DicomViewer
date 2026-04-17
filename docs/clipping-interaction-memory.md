# Model Clipping Interaction Memory

Last updated: 2026-04-17

This document records the current interaction decisions for the 3D model clipping box, so later work can keep the same UX intent instead of re-deriving it from code.

## Current interaction goals

The clipping box should behave like a clean, face-driven gizmo:

1. Left-dragging a box face should push that face and resize the clipping volume along that face normal.
2. Left-dragging empty space should not be treated as face scaling and should fall back to the normal view rotation behavior.
3. Hovering a face should visually highlight that face.
4. Dragging a face should keep the active face highlighted.
5. The clipping box should look clean:
   - no spherical handles
   - no dense internal face wires
   - no cursor wires crossing the box

## Why this was refactored

The earlier implementation kept clipping interaction logic directly inside [modelviewwidget.cpp](F:\QtProject\DicomViewer\src\view\modelviewwidget.cpp:1). That worked, but it mixed together:

- Qt event filtering
- VTK widget state correction
- hover/highlight behavior
- manual face-drag overrides
- clipping-box visual styling

That made the view widget too responsible for one specialized interaction.

## Current architecture

The clipping interaction is now owned by a dedicated controller:

- controller: [modelclippingcontroller.h](F:\QtProject\DicomViewer\src\view\modelclippingcontroller.h:1)
- implementation: [modelclippingcontroller.cpp](F:\QtProject\DicomViewer\src\view\modelclippingcontroller.cpp:1)

`ModelViewWidget` is now only the host for:

- enabling and disabling clipping
- forwarding viewport events
- applying clipping planes to model geometry
- scene refresh and status text hosting

Relevant host files:

- [modelviewwidget.h](F:\QtProject\DicomViewer\src\view\modelviewwidget.h:1)
- [modelviewwidget.cpp](F:\QtProject\DicomViewer\src\view\modelviewwidget.cpp:1)

## Important behavioral rule

VTK's default `vtkBoxWidget2` behavior does not directly match the intended UX. In practice, clicking a face may resolve to `Rotating` rather than `MoveF0~MoveF5`.

Because of that, the controller explicitly corrects interaction routing:

- if a press resolves to a real face hit, it is remapped to the corresponding `MoveF0~MoveF5` state
- if no face is hit, the event is not consumed and normal rotation is allowed to proceed

This rule is intentional and should be preserved unless the project later replaces `vtkBoxWidget2` entirely.

## Visual style baseline

The current clipping box visual baseline is:

- handles hidden
- face wires hidden
- cursor wires hidden
- low-opacity idle faces
- stronger selected-face opacity
- stronger selected outline width

These style choices live in [modelclippingcontroller.cpp](F:\QtProject\DicomViewer\src\view\modelclippingcontroller.cpp:1).

## If this area is changed later

Prefer this order of changes:

1. Keep the interaction contract stable.
2. Adjust visuals inside `ModelClippingController`.
3. Only if necessary, adjust face-hit resolution logic inside `ModelClippingController`.
4. Avoid moving clipping interaction logic back into `ModelViewWidget`.

## Open future improvements

Possible next-step improvements that fit the current design:

- separate hover color and active-drag color more clearly
- make inactive faces even lighter while preserving hit area
- add optional edge emphasis without reintroducing visual clutter
- if VTK limitations become too intrusive, replace the current box widget with a fully custom clipping gizmo
