#ifndef IMAGE_PAGE_HPP
#define IMAGE_PAGE_HPP

#include <string_view>

namespace txthighlight::assets {

// The raster image annotation frontend, inlined so the binary is self-contained.
// It loads the original image into a canvas, fetches
// GET /api/annotations/image, and POSTs every edit to /api/annotations/image.
// The image API reference and its CSS are inserted where the API_DOCS markers
// are.
//
// All interaction goes through one state machine: `transition(event, payload)`
// validates the event against an enum, mutates `state`, then re-renders. No
// handler edits the DOM or the annotation list on its own.
inline constexpr std::string_view image_page =
    R"TXTHTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Image Annotator</title>
  <script>
    (() => {
      const path = new URLSearchParams(window.location.search).get('path') || '';
      document.title = (path.split('/').pop() || '') + ' - Image Annotator';
    })();
  </script>
  <style>
    :root {
      --gap: 0.5rem;
      --radius: 0.625rem;
      --border: #d5d5dc;
      --surface: #f5f5f8;
      --accent: #0a6cff;
      --muted: #6b6b74;
    }

    *, *::before, *::after { box-sizing: border-box; }

    html {
      -webkit-text-size-adjust: 100%;
      text-size-adjust: 100%;
    }

    body {
      margin: 0;
      font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
      font-size: clamp(0.9375rem, 0.9rem + 0.2vw, 1rem);
      line-height: 1.5;
      color: #17171a;
      background: #f2f2f5;
    }

    button {
      min-height: 2.5rem;
      padding: 0 0.75rem;
      border: 1px solid var(--border);
      border-radius: var(--radius);
      background: #fff;
      color: inherit;
      font: inherit;
      font-weight: 500;
      cursor: pointer;
      touch-action: manipulation;
    }

    button:hover:not(:disabled) { background: #eaeaef; }

    button[aria-pressed="true"] {
      background: var(--accent);
      border-color: var(--accent);
      color: #fff;
    }

    button:disabled {
      background: #f4f4f7;
      border-color: #e2e2e8;
      color: #85858f;
      cursor: default;
    }

    :is(button, input, textarea):focus-visible {
      outline: 2px solid var(--accent);
      outline-offset: 2px;
    }

    header#toolbar {
      position: sticky;
      top: 0;
      z-index: 10;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: var(--gap);
      padding: calc(0.5rem + env(safe-area-inset-top))
        max(0.5rem, env(safe-area-inset-right)) 0.5rem
        max(0.5rem, env(safe-area-inset-left));
      background: rgba(255, 255, 255, 0.97);
      border-bottom: 1px solid var(--border);
    }

    .group {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 0.375rem;
    }

    .group + .group {
      padding-left: 0.5rem;
      border-left: 1px solid var(--border);
    }

    .field {
      display: flex;
      align-items: center;
      gap: 0.375rem;
      font-size: 0.8125rem;
      color: var(--muted);
      white-space: nowrap;
    }

    input[type="color"] {
      width: 2.25rem;
      height: 2.25rem;
      padding: 0;
      border: 1px solid var(--border);
      border-radius: 0.375rem;
      background: #fff;
      cursor: pointer;
    }

    input[type="number"] {
      width: 4rem;
      min-height: 2.25rem;
      padding: 0 0.375rem;
      border: 1px solid var(--border);
      border-radius: 0.375rem;
      font: inherit;
      font-size: 0.875rem;
      background: #fff;
    }

    output#zoom,
    output#status {
      color: var(--muted);
      font-size: 0.8125rem;
    }

    output#zoom {
      min-width: 3.25rem;
      text-align: center;
      font-variant-numeric: tabular-nums;
    }

    output#status {
      flex: 1 1 18rem;
      min-width: 0;
    }

    main {
      padding: 0.75rem max(0.5rem, env(safe-area-inset-left)) 2rem
        max(0.5rem, env(safe-area-inset-right));
    }

    figure#stage {
      margin: 0 auto;
      max-width: 100%;
    }

    /* The scrollable viewport is what makes zoom reachable on touch devices:
       zoomed-in content pans by scrolling, and by dragging with two fingers. */
    #viewport {
      max-width: 100%;
      max-height: calc(100vh - 10rem);
      overflow: auto;
      border-radius: 0.375rem;
      background: #fff;
      box-shadow: 0 0.25rem 1.5rem rgba(0, 0, 0, 0.08);
      touch-action: none;
    }

    canvas#canvas {
      display: block;
      touch-action: none;
      cursor: crosshair;
    }

    canvas#canvas[data-active-tool="select"] { cursor: grab; }
    canvas#canvas.panning { cursor: grabbing; }
    canvas#canvas[data-active-tool="popup_note"] { cursor: cell; }

    figcaption {
      margin-top: 0.5rem;
      color: var(--muted);
      font-size: 0.8125rem;
      text-align: center;
      overflow-wrap: anywhere;
    }

    dialog#noteModal {
      width: min(30rem, calc(100vw - 2rem));
      max-height: 85vh;
      padding: 0;
      border: 1px solid var(--border);
      border-radius: 0.875rem;
      box-shadow: 0 1.5rem 4rem rgba(0, 0, 0, 0.28);
      color: inherit;
      overflow: auto;
    }

    dialog#noteModal::backdrop { background: rgba(0, 0, 0, 0.45); }

    dialog#noteModal form {
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
      padding: clamp(1rem, 2.5vw, 1.5rem);
      padding-bottom: calc(clamp(1rem, 2.5vw, 1.5rem) + env(safe-area-inset-bottom));
    }

    dialog#noteModal h2 {
      margin: 0;
      font-size: 1.0625rem;
    }

    .note-field {
      display: block;
      font-size: 0.8125rem;
      font-weight: 600;
    }

    textarea#noteText {
      width: 100%;
      min-height: 7rem;
      margin-top: 0.375rem;
      padding: 0.625rem;
      border: 1px solid var(--border);
      border-radius: var(--radius);
      font: inherit;
      font-size: 1rem; /* 16px keeps iOS from zooming on focus */
      resize: vertical;
    }

    .coordinate-row {
      display: flex;
      gap: 0.75rem;
    }

    .coordinate-row > * {
      flex: 1 1 0;
      min-width: 0;
    }

    .coordinate-row input {
      width: 100%;
      margin-top: 0.375rem;
    }

    .dialog-actions {
      display: flex;
      gap: 0.625rem;
      justify-content: flex-end;
    }

    .dialog-actions button { min-width: 7.5rem; }

    .dialog-actions .primary {
      background: var(--accent);
      border-color: var(--accent);
      color: #fff;
    }

    .dialog-actions .primary:hover:not(:disabled) { background: #0a5ee0; }

    .api-docs-wrap {
      max-width: 68ch;
      margin: 0 auto 2rem;
      padding: 0.75rem max(0.5rem, env(safe-area-inset-left)) 0
        max(0.5rem, env(safe-area-inset-right));
      background: #fff;
      border-radius: var(--radius);
    }

    .api-docs-wrap > summary {
      min-height: 2.75rem;
      padding: 0.5rem 0;
      font-weight: 600;
      cursor: pointer;
    }

    /* API_DOCS_STYLE */

    @media (max-width: 48rem) {
      .group + .group {
        padding-left: 0;
        border-left: 0;
      }

      .group[role="group"] {
        flex: 1 1 100%;
      }

      .group[role="group"] button {
        flex: 1 1 0;
        min-width: 0;
        padding: 0 0.25rem;
        font-size: 0.8125rem;
      }

      output#status { flex-basis: 100%; }

      dialog#noteModal {
        width: 100%;
        max-width: 100%;
        max-height: 88vh;
        margin: 0;
        inset: auto 0 0;
        border-radius: 1.125rem 1.125rem 0 0;
        border-bottom: none;
      }

      .dialog-actions { flex-direction: column-reverse; }
      .dialog-actions button { width: 100%; min-width: 0; min-height: 3rem; }
    }
  </style>
</head>
<body>
  <header id="toolbar">
    <div class="group" role="group" aria-label="History">
      <button type="button" id="undoBtn" disabled>Undo</button>
      <button type="button" id="redoBtn" disabled>Redo</button>
    </div>

    <div class="group" role="group" aria-label="Tools">
      <button type="button" data-tool="select" aria-pressed="true">Select</button>
      <button type="button" data-tool="rectangle" aria-pressed="false">Rectangle</button>
      <button type="button" data-tool="ellipse" aria-pressed="false">Ellipse</button>
      <button type="button" data-tool="line_segment" aria-pressed="false">Line</button>
      <button type="button" data-tool="freehand" aria-pressed="false">Freehand</button>
      <button type="button" data-tool="popup_note" aria-pressed="false">Note</button>
    </div>

    <div class="group" role="group" aria-label="Line style">
      <label class="field" for="colorInput">Color
        <input type="color" id="colorInput" value="#ff3b30" />
      </label>
      <label class="field" for="widthInput">Width
        <input type="number" id="widthInput" value="3" min="1" max="64" step="1" />
      </label>
    </div>

    <div class="group" role="group" aria-label="Zoom">
      <button type="button" id="zoomOutBtn" aria-label="Zoom out">−</button>
      <output id="zoom">100%</output>
      <button type="button" id="zoomInBtn" aria-label="Zoom in">+</button>
      <button type="button" id="zoomFitBtn">Fit</button>
    </div>

    <div class="group" role="group" aria-label="Selected annotation">
      <button type="button" id="modifyBtn" disabled>Modify</button>
      <button type="button" id="deleteBtn" disabled>Delete</button>
    </div>

    <output id="status" aria-live="polite">Loading image…</output>
  </header>

  <main>
    <figure id="stage">
      <div id="viewport">
        <canvas id="canvas" data-active-tool="select" aria-label="Image annotation canvas"></canvas>
      </div>
      <figcaption id="caption">Annotations are stored beside the image; the image file is never modified.</figcaption>
    </figure>
  </main>

  <details class="api-docs-wrap">
    <summary>REST API for scripts and agents</summary>
    <!-- API_DOCS -->
  </details>

  <dialog id="noteModal" aria-labelledby="noteTitle">
    <form method="dialog">
      <h2 id="noteTitle">Popup note</h2>
      <label class="note-field" for="noteText">Text
        <textarea id="noteText" placeholder="What should this spot say?"></textarea>
      </label>
      <div class="coordinate-row">
        <label class="note-field" for="noteX">x (px)
          <input type="number" id="noteX" min="0" step="1" />
        </label>
        <label class="note-field" for="noteY">y (px)
          <input type="number" id="noteY" min="0" step="1" />
        </label>
      </div>
      <div class="dialog-actions">
        <button type="button" id="cancelNote">Cancel</button>
        <button type="button" id="saveNote" class="primary">Save note</button>
      </div>
    </form>
  </dialog>

  <script>
    // ---------------------------------------------------------------------
    // Enums
    //
    // ES6 has no `enum` keyword, so each closed set of values is a frozen
    // object and every assignment is validated. Values outside the set throw
    // immediately instead of flowing silently through the state machine.
    // ---------------------------------------------------------------------
    function createEnum(name, members) {
      const enumeration = {};
      for (const member of members) {
        enumeration[member] = member;
      }
      // Defined before freezing so it stays non-enumerable and outside values().
      Object.defineProperty(enumeration, 'name', { value: name });
      return Object.freeze(enumeration);
    }

    function assertEnum(enumeration, value) {
      if (!Object.values(enumeration).includes(value)) {
        throw new RangeError(`${enumeration.name}: unknown value ${JSON.stringify(value)}`);
      }
      return value;
    }

    const Mode = createEnum('Mode', ['idle', 'drafting', 'selected', 'note_dialog']);

    const Tool = createEnum('Tool', [
      'select',
      'rectangle',
      'ellipse',
      'line_segment',
      'freehand',
      'popup_note',
    ]);

    const AnnotationType = createEnum('AnnotationType', [
      'rectangle',
      'ellipse',
      'line_segment',
      'freehand',
      'popup_note',
    ]);

    const Event = createEnum('Event', [
      'image_loaded',
      'image_failed',
      'annotations_updated',
      'tool_changed',
      'style_changed',
      'pointer_down',
      'pointer_move',
      'pointer_up',
      'pointer_cancel',
      'gesture_started',
      'gesture_moved',
      'gesture_ended',
      'zoom_changed',
      'note_requested',
      'note_saved',
      'note_dialog_closed',
      'clear_active',
      'history_applied',
    ]);

    const Action = createEnum('Action', [
      'add_rectangle',
      'add_ellipse',
      'add_line_segment',
      'add_freehand',
      'add_popup_note',
      'update_popup_note',
      'remove_id',
    ]);

    const AddActionFor = Object.freeze({
      [AnnotationType.rectangle]: Action.add_rectangle,
      [AnnotationType.ellipse]: Action.add_ellipse,
      [AnnotationType.line_segment]: Action.add_line_segment,
      [AnnotationType.freehand]: Action.add_freehand,
      [AnnotationType.popup_note]: Action.add_popup_note,
    });

    // Style bounds: the UI never sends a colour or width the server would reject.
    const DefaultStyle = Object.freeze({ lineColor: '#ff3b30', lineWidth: 3 });
    const minLineWidth = 1;
    const maxLineWidth = 64;
    const minZoom = 0.05;
    const maxZoom = 16;
    const zoomStep = 1.25;
    const hexColorPattern = /^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/;

    function normalizeLineColor(candidate) {
      return hexColorPattern.test(String(candidate)) ? String(candidate) : DefaultStyle.lineColor;
    }

    function normalizeLineWidth(candidate) {
      const value = Number(candidate);
      if (!Number.isFinite(value) || value <= 0) {
        return DefaultStyle.lineWidth;
      }
      return Math.min(maxLineWidth, Math.max(minLineWidth, value));
    }

    // ?path= carries an already percent-encoded URI path (same form as the
    // request path), so fetch it verbatim; re-encoding would double-escape
    // non-ASCII names into %25.. and 404.
    const documentPath = new URLSearchParams(window.location.search).get('path') || '/';
    const imageUrl = documentPath;
    const annotationsApiPath = `/api/annotations/image?path=${encodeURIComponent(documentPath)}`;

    const canvasEl = document.getElementById('canvas');
    const viewportEl = document.getElementById('viewport');
    const captionEl = document.getElementById('caption');
    const statusEl = document.getElementById('status');
    const undoBtnEl = document.getElementById('undoBtn');
    const redoBtnEl = document.getElementById('redoBtn');
    const modifyBtnEl = document.getElementById('modifyBtn');
    const deleteBtnEl = document.getElementById('deleteBtn');
    const colorInputEl = document.getElementById('colorInput');
    const widthInputEl = document.getElementById('widthInput');
    const zoomEl = document.getElementById('zoom');
    const zoomInBtnEl = document.getElementById('zoomInBtn');
    const zoomOutBtnEl = document.getElementById('zoomOutBtn');
    const zoomFitBtnEl = document.getElementById('zoomFitBtn');
    // Scoped to the toolbar: the canvas carries `data-active-tool` instead, so it
    // can never be mistaken for a tool button.
    const toolButtonsEl = Array.from(document.querySelectorAll('#toolbar [data-tool]'));
    const noteModalEl = document.getElementById('noteModal');
    const noteTextEl = document.getElementById('noteText');
    const noteXEl = document.getElementById('noteX');
    const noteYEl = document.getElementById('noteY');
    const saveNoteEl = document.getElementById('saveNote');
    const cancelNoteEl = document.getElementById('cancelNote');

    const context = canvasEl.getContext('2d');
    const image = new Image();

    // Layout of the popup-note bubbles drawn by the last render, so hit testing
    // and drawing cannot disagree about where a note is.
    const noteLayout = new Map();

    const state = {
      mode: Mode.idle,
      tool: Tool.select,
      lineColor: normalizeLineColor(colorInputEl.value),
      lineWidth: normalizeLineWidth(widthInputEl.value),
      annotations: [],
      selectedId: null,
      draft: null,
      pending: [],          // annotations drawn locally until the server confirms them
      hidden: new Set(),    // ids removed locally until the server confirms them
      pointers: new Map(),  // live pointers, in client coordinates
      gesture: null,        // two-finger pinch/pan baseline
      pan: null,            // one-finger drag baseline in select mode
      zoom: 1,
      noteTarget: null,
      noteEditing: false,
      imageLoaded: false,
      imageWidth: 0,
      imageHeight: 0,
      undoStack: [],
      redoStack: [],
      error: '',
    };

    let pendingCounter = 0;
    const defaultHint = 'Pick a tool, then drag on the image.';

    function setMode(next) {
      state.mode = assertEnum(Mode, next);
    }

    // The controls normalize user input; the state machine refuses anything else.
    function assertLineColor(candidate) {
      const color = String(candidate);
      if (!hexColorPattern.test(color)) {
        throw new RangeError(`lineColor: invalid colour ${JSON.stringify(candidate)}`);
      }
      return color;
    }

    function assertLineWidth(candidate) {
      const width = Number(candidate);
      if (!Number.isFinite(width) || width < minLineWidth || width > maxLineWidth) {
        throw new RangeError(`lineWidth: out of range ${JSON.stringify(candidate)}`);
      }
      return width;
    }

    function clamp(value, low, high) {
      return Math.min(high, Math.max(low, value));
    }

    // Image pixels per CSS pixel (canvas backing store / displayed width). A
    // distance measured on screen therefore becomes image pixels by
    // multiplying with it — never dividing.
    function selectionScale() {
      const rect = canvasEl.getBoundingClientRect();
      return rect.width > 0 ? canvasEl.width / rect.width : 1;
    }

    function screenPixels(distance, scale) {
      return distance * scale;
    }

    function imagePoint(clientX, clientY) {
      const rect = canvasEl.getBoundingClientRect();
      return {
        x: clamp(((clientX - rect.left) * canvasEl.width) / rect.width, 0, state.imageWidth),
        y: clamp(((clientY - rect.top) * canvasEl.height) / rect.height, 0, state.imageHeight),
      };
    }

    function pointerMidpoint() {
      const points = [...state.pointers.values()];
      const [a, b] = points;
      return {
        x: (a.x + b.x) / 2,
        y: (a.y + b.y) / 2,
        distance: Math.hypot(a.x - b.x, a.y - b.y) || 1,
      };
    }

    function selectedAnnotation() {
      return state.annotations.find((item) => item.id === state.selectedId) || null;
    }

    function visibleAnnotations() {
      return state.annotations.filter((annotation) => !state.hidden.has(annotation.id));
    }

    // ---------------------------------------------------------------
    // Zoom
    // ---------------------------------------------------------------

    function applyZoom(next) {
      state.zoom = clamp(Number(next) || 1, minZoom, maxZoom);
      canvasEl.style.width = `${state.imageWidth * state.zoom}px`;
      canvasEl.style.height = `${state.imageHeight * state.zoom}px`;
      zoomEl.textContent = `${Math.round(state.zoom * 100)}%`;
      return state.zoom;
    }

    function fitZoom() {
      if (!state.imageLoaded) {
        return 1;
      }
      const available = viewportEl.clientWidth || state.imageWidth;
      return clamp(Math.min(1, available / state.imageWidth), minZoom, maxZoom);
    }

    // Scroll so that an image point sits under a viewport-relative position.
    function centerOn(imageX, imageY, clientX, clientY) {
      const viewportRect = viewportEl.getBoundingClientRect();
      viewportEl.scrollLeft = imageX * state.zoom - (clientX - viewportRect.left);
      viewportEl.scrollTop = imageY * state.zoom - (clientY - viewportRect.top);
    }

    function viewportCentreFocal() {
      const rect = canvasEl.getBoundingClientRect();
      const viewportRect = viewportEl.getBoundingClientRect();
      const clientX = viewportRect.left + viewportRect.width / 2;
      const clientY = viewportRect.top + viewportRect.height / 2;
      return {
        x: clamp(((clientX - rect.left) / rect.width) * state.imageWidth, 0, state.imageWidth),
        y: clamp(((clientY - rect.top) / rect.height) * state.imageHeight, 0, state.imageHeight),
        clientX,
        clientY,
      };
    }

    // ---------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------

    function applyStroke(annotation, scale) {
      context.strokeStyle = annotation.line_color;
      context.lineWidth = Math.max(annotation.line_width, screenPixels(1, scale));
      context.lineJoin = 'round';
      context.lineCap = 'round';
      context.setLineDash([]);
    }

    function tracePath(annotation) {
      context.beginPath();
      switch (annotation.type) {
        case AnnotationType.rectangle:
          context.rect(annotation.x, annotation.y, annotation.width, annotation.height);
          break;
        case AnnotationType.ellipse:
          context.ellipse(annotation.cx, annotation.cy, annotation.rx, annotation.ry, 0, 0, Math.PI * 2);
          break;
        case AnnotationType.line_segment:
          context.moveTo(annotation.x1, annotation.y1);
          context.lineTo(annotation.x2, annotation.y2);
          break;
        case AnnotationType.freehand:
          annotation.points.forEach((point, index) => {
            if (index === 0) {
              context.moveTo(point.x, point.y);
            } else {
              context.lineTo(point.x, point.y);
            }
          });
          break;
        default:
          break;
      }
    }

    // Selection emphasis is drawn *underneath* the shape: a soft halo, then a
    // dashed collar, then the shape's own stroke on top. The collar shows only
    // as a ring just outside the outline, so the annotation keeps its colour
    // while the selection is unmistakable.
    function drawSelectionRing(annotation, scale) {
      const ownWidth = Math.max(annotation.line_width, screenPixels(1, scale));
      const collar = screenPixels(3, scale);
      context.save();
      context.lineJoin = 'round';
      context.lineCap = 'round';

      context.strokeStyle = 'rgba(10, 108, 255, 0.22)';
      context.lineWidth = ownWidth + screenPixels(14, scale);
      context.setLineDash([]);
      tracePath(annotation);
      context.stroke();

      context.strokeStyle = 'rgba(10, 108, 255, 0.95)';
      context.lineWidth = ownWidth + collar * 2;
      context.setLineDash([screenPixels(5, scale), screenPixels(4, scale)]);
      tracePath(annotation);
      context.stroke();

      context.restore();
    }

    function drawPopupNote(annotation, scale) {
      const markerRadius = screenPixels(8, scale);
      const fontSize = screenPixels(13, scale);
      const padding = screenPixels(6, scale);
      const offset = markerRadius + padding;

      context.save();
      context.font = `${fontSize}px system-ui, -apple-system, "Segoe UI", Roboto, sans-serif`;
      const lines = String(annotation.text ?? '').split('\n');
      const textWidth = lines.reduce(
        (widest, line) => Math.max(widest, context.measureText(line || ' ').width),
        0,
      );
      const lineHeight = fontSize * 1.35;
      const box = {
        x: annotation.x + offset,
        y: annotation.y - offset - lines.length * lineHeight - 2 * padding,
        width: textWidth + 2 * padding,
        height: lines.length * lineHeight + 2 * padding,
      };

      context.fillStyle = '#fffbe6';
      context.strokeStyle = '#c9a227';
      context.lineWidth = screenPixels(1, scale);
      context.beginPath();
      context.roundRect(box.x, box.y, box.width, box.height, screenPixels(4, scale));
      context.fill();
      context.stroke();

      context.fillStyle = '#3b2f00';
      context.textBaseline = 'top';
      lines.forEach((line, index) => {
        context.fillText(line, box.x + padding, box.y + padding + index * lineHeight);
      });

      context.fillStyle = '#ffd24d';
      context.strokeStyle = '#8a6d00';
      context.lineWidth = screenPixels(1.5, scale);
      context.beginPath();
      context.arc(annotation.x, annotation.y, markerRadius, 0, Math.PI * 2);
      context.fill();
      context.stroke();
      context.restore();

      // Selection marker sits outside the bubble, so the note keeps its look.
      if (state.selectedId === annotation.id) {
        const pad = screenPixels(4, scale);
        context.save();
        context.fillStyle = 'rgba(10, 108, 255, 0.22)';
        context.beginPath();
        context.arc(annotation.x, annotation.y, markerRadius + screenPixels(6, scale), 0, Math.PI * 2);
        context.fill();
        context.strokeStyle = 'rgba(10, 108, 255, 0.85)';
        context.lineWidth = screenPixels(2, scale);
        context.setLineDash([screenPixels(6, scale), screenPixels(4, scale)]);
        context.beginPath();
        context.roundRect(box.x - pad, box.y - pad, box.width + 2 * pad, box.height + 2 * pad, screenPixels(6, scale));
        context.stroke();
        context.restore();
      }

      noteLayout.set(annotation.id, { markerRadius, box });
    }

    function drawAnnotation(annotation, scale) {
      if (annotation.type === AnnotationType.popup_note) {
        drawPopupNote(annotation, scale);
        return;
      }
      if (state.selectedId === annotation.id) {
        drawSelectionRing(annotation, scale);
      }
      context.save();
      applyStroke(annotation, scale);
      tracePath(annotation);
      context.stroke();
      context.restore();
    }

    function draftAsAnnotation(draft) {
      const stroke = { line_color: state.lineColor, line_width: state.lineWidth };
      switch (draft.type) {
        case AnnotationType.rectangle:
          return {
            type: AnnotationType.rectangle,
            x: Math.min(draft.start.x, draft.current.x),
            y: Math.min(draft.start.y, draft.current.y),
            width: Math.abs(draft.current.x - draft.start.x),
            height: Math.abs(draft.current.y - draft.start.y),
            ...stroke,
          };
        case AnnotationType.ellipse:
          return {
            type: AnnotationType.ellipse,
            cx: (draft.start.x + draft.current.x) / 2,
            cy: (draft.start.y + draft.current.y) / 2,
            rx: Math.abs(draft.current.x - draft.start.x) / 2,
            ry: Math.abs(draft.current.y - draft.start.y) / 2,
            ...stroke,
          };
        case AnnotationType.line_segment:
          return {
            type: AnnotationType.line_segment,
            x1: draft.start.x,
            y1: draft.start.y,
            x2: draft.current.x,
            y2: draft.current.y,
            ...stroke,
          };
        case AnnotationType.freehand:
          return {
            type: AnnotationType.freehand,
            points: draft.points.length >= 2 ? draft.points : [...draft.points, draft.current],
            ...stroke,
          };
        default:
          return null;
      }
    }

    function drawDraft(scale) {
      if (!state.draft) {
        return;
      }
      const preview = draftAsAnnotation(state.draft);
      if (!preview) {
        return;
      }
      context.save();
      context.globalAlpha = 0.75;
      applyStroke(preview, scale);
      tracePath(preview);
      context.stroke();
      context.restore();
    }

    function renderCanvas() {
      if (!state.imageLoaded) {
        return;
      }
      const scale = selectionScale();
      noteLayout.clear();
      context.clearRect(0, 0, canvasEl.width, canvasEl.height);
      context.drawImage(image, 0, 0, canvasEl.width, canvasEl.height);
      for (const annotation of visibleAnnotations()) {
        drawAnnotation(annotation, scale);
      }
      // Shapes are drawn the moment they are committed, not when the server
      // answers, so nothing flickers between the two lists.
      for (const entry of state.pending) {
        drawAnnotation(entry.annotation, scale);
      }
      drawDraft(scale);
    }

    // ---------------------------------------------------------------
    // Hit testing
    // ---------------------------------------------------------------

    function distanceToSegment(point, from, to) {
      const dx = to.x - from.x;
      const dy = to.y - from.y;
      const lengthSquared = dx * dx + dy * dy;
      if (lengthSquared === 0) {
        return Math.hypot(point.x - from.x, point.y - from.y);
      }
      const t = clamp(((point.x - from.x) * dx + (point.y - from.y) * dy) / lengthSquared, 0, 1);
      return Math.hypot(point.x - (from.x + t * dx), point.y - (from.y + t * dy));
    }

    function hitsAnnotation(annotation, point, scale) {
      if (annotation.type === AnnotationType.popup_note) {
        const layout = noteLayout.get(annotation.id);
        const markerRadius = layout ? layout.markerRadius : screenPixels(8, scale);
        if (
          Math.hypot(point.x - annotation.x, point.y - annotation.y) <=
          markerRadius + screenPixels(4, scale)
        ) {
          return true;
        }
        const box = layout?.box;
        return Boolean(
          box &&
            point.x >= box.x &&
            point.x <= box.x + box.width &&
            point.y >= box.y &&
            point.y <= box.y + box.height,
        );
      }

      // At least 9 CSS px of slack, plus half the stroke: comfortable on touch
      // screens and independent of the current zoom.
      const tolerance = Math.max(annotation.line_width / 2 + screenPixels(6, scale), screenPixels(9, scale));
      switch (annotation.type) {
        case AnnotationType.rectangle: {
          const nearX = Math.min(
            Math.abs(point.x - annotation.x),
            Math.abs(point.x - (annotation.x + annotation.width)),
          );
          const nearY = Math.min(
            Math.abs(point.y - annotation.y),
            Math.abs(point.y - (annotation.y + annotation.height)),
          );
          const withinX =
            point.x >= annotation.x - tolerance &&
            point.x <= annotation.x + annotation.width + tolerance;
          const withinY =
            point.y >= annotation.y - tolerance &&
            point.y <= annotation.y + annotation.height + tolerance;
          return (nearX <= tolerance && withinY) || (nearY <= tolerance && withinX);
        }
        case AnnotationType.ellipse: {
          const rx = annotation.rx + tolerance;
          const ry = annotation.ry + tolerance;
          const innerRx = Math.max(annotation.rx - tolerance, 0);
          const innerRy = Math.max(annotation.ry - tolerance, 0);
          const outer =
            ((point.x - annotation.cx) ** 2) / (rx * rx) +
            ((point.y - annotation.cy) ** 2) / (ry * ry);
          if (outer > 1) {
            return false;
          }
          if (innerRx === 0 || innerRy === 0) {
            return true;
          }
          const inner =
            ((point.x - annotation.cx) ** 2) / (innerRx * innerRx) +
            ((point.y - annotation.cy) ** 2) / (innerRy * innerRy);
          return inner >= 1;
        }
        case AnnotationType.line_segment:
          return (
            distanceToSegment(
              point,
              { x: annotation.x1, y: annotation.y1 },
              { x: annotation.x2, y: annotation.y2 },
            ) <= tolerance
          );
        case AnnotationType.freehand:
          return annotation.points.some((candidate, index) => {
            if (index === 0) {
              return false;
            }
            return distanceToSegment(point, annotation.points[index - 1], candidate) <= tolerance;
          });
        default:
          return false;
      }
    }

    function hitTest(point) {
      const scale = selectionScale();
      const annotations = visibleAnnotations();
      for (let index = annotations.length - 1; index >= 0; index -= 1) {
        if (hitsAnnotation(annotations[index], point, scale)) {
          return annotations[index];
        }
      }
      return null;
    }

    // ---------------------------------------------------------------
    // REST helpers
    // ---------------------------------------------------------------

    function addPayloadFor(annotation) {
      const action = AddActionFor[annotation.type];
      switch (annotation.type) {
        case AnnotationType.rectangle:
          return {
            action,
            x: annotation.x,
            y: annotation.y,
            width: annotation.width,
            height: annotation.height,
            line_color: annotation.line_color,
            line_width: annotation.line_width,
          };
        case AnnotationType.ellipse:
          return {
            action,
            cx: annotation.cx,
            cy: annotation.cy,
            rx: annotation.rx,
            ry: annotation.ry,
            line_color: annotation.line_color,
            line_width: annotation.line_width,
          };
        case AnnotationType.line_segment:
          return {
            action,
            x1: annotation.x1,
            y1: annotation.y1,
            x2: annotation.x2,
            y2: annotation.y2,
            line_color: annotation.line_color,
            line_width: annotation.line_width,
          };
        case AnnotationType.freehand:
          return {
            action,
            points: annotation.points.map((point) => ({ x: point.x, y: point.y })),
            line_color: annotation.line_color,
            line_width: annotation.line_width,
          };
        case AnnotationType.popup_note:
          return { action, x: annotation.x, y: annotation.y, text: annotation.text ?? '' };
        default:
          throw new RangeError(`No add action for ${annotation.type}`);
      }
    }

    async function mutate(payload) {
      assertEnum(Action, payload.action);
      const response = await fetch(annotationsApiPath, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.error || `HTTP ${response.status}`);
      }
      transition(Event.annotations_updated, { annotations: data.annotations });
      return data.annotations;
    }

    function recordHistory(entry) {
      if (typeof entry.undo !== 'function' || typeof entry.redo !== 'function') {
        throw new TypeError('History entries need undo and redo functions');
      }
      state.undoStack.push(entry);
      if (state.undoStack.length > 200) {
        state.undoStack.shift();
      }
      state.redoStack.length = 0;
    }

    // Draws the shape at once, then persists it and records how to undo it.
    async function createAnnotation(preview) {
      const payload = addPayloadFor(preview);
      const localId = (pendingCounter += 1);
      state.pending.push({ localId, annotation: preview });
      render();
      try {
        const annotations = await mutate(payload);
        const created = annotations[annotations.length - 1];
        const entry = {
          annotationId: created.id,
          undo: async () => {
            await mutate({ action: Action.remove_id, id: entry.annotationId });
            state.selectedId = null;
          },
          redo: async () => {
            const restored = await mutate(payload);
            entry.annotationId = restored[restored.length - 1].id;
          },
        };
        recordHistory(entry);
        return created;
      } finally {
        state.pending = state.pending.filter((item) => item.localId !== localId);
        render();
      }
    }

    async function removeAnnotation(annotation) {
      const payload = addPayloadFor(annotation);
      state.hidden.add(annotation.id);
      state.selectedId = null;
      render();
      try {
        await mutate({ action: Action.remove_id, id: annotation.id });
        const entry = {
          annotationId: annotation.id,
          undo: async () => {
            const restored = await mutate(payload);
            entry.annotationId = restored[restored.length - 1].id;
          },
          redo: async () => {
            await mutate({ action: Action.remove_id, id: entry.annotationId });
          },
        };
        recordHistory(entry);
      } catch (error) {
        state.hidden.delete(annotation.id);
        render();
        throw error;
      }
    }

    async function updateNote(annotationId, changes) {
      const before = selectedAnnotation();
      await mutate({ action: Action.update_popup_note, id: annotationId, ...changes });
      const after = state.annotations.find((item) => item.id === annotationId);
      if (!before || !after) {
        return;
      }
      const entry = {
        annotationId,
        undo: async () => {
          await mutate({
            action: Action.update_popup_note,
            id: entry.annotationId,
            text: before.text,
            x: before.x,
            y: before.y,
          });
        },
        redo: async () => {
          await mutate({
            action: Action.update_popup_note,
            id: entry.annotationId,
            text: after.text,
            x: after.x,
            y: after.y,
          });
        },
      };
      recordHistory(entry);
    }

    // ---------------------------------------------------------------
    // Controls
    // ---------------------------------------------------------------

    function hintText() {
      const selected = selectedAnnotation();
      if (state.error) {
        return state.error;
      }
      if (!state.imageLoaded) {
        return 'Loading image…';
      }
      if (state.mode === Mode.note_dialog) {
        return state.noteEditing
          ? 'Editing the selected note. Save or cancel when done.'
          : 'Placing a new note. Save or cancel when done.';
      }
      if (state.gesture) {
        return 'Pinch or drag with two fingers to zoom and pan.';
      }
      if (state.mode === Mode.drafting) {
        return 'Release to finish the shape.';
      }
      if (selected) {
        return selected.type === AnnotationType.popup_note
          ? 'Popup note selected. Modify edits it, Delete removes it.'
          : `${selected.type.replace('_', ' ')} selected. Delete removes it.`;
      }
      switch (state.tool) {
        case Tool.select:
          return 'Tap a shape or note to select it; drag to pan; two fingers to zoom.';
        case Tool.rectangle:
          return 'Drag to draw a rectangle.';
        case Tool.ellipse:
          return 'Drag to draw an ellipse.';
        case Tool.line_segment:
          return 'Drag to draw a line segment.';
        case Tool.freehand:
          return 'Drag to draw freehand.';
        case Tool.popup_note:
          return 'Click where the popup note should point.';
        default:
          return defaultHint;
      }
    }

    function renderControls() {
      for (const button of toolButtonsEl) {
        button.setAttribute('aria-pressed', String(button.dataset.tool === state.tool));
      }
      canvasEl.dataset.activeTool = state.tool;
      undoBtnEl.disabled = state.undoStack.length === 0;
      redoBtnEl.disabled = state.redoStack.length === 0;
      const selected = selectedAnnotation();
      deleteBtnEl.disabled = !selected;
      modifyBtnEl.disabled = !selected || selected.type !== AnnotationType.popup_note;
      statusEl.textContent = hintText();
    }

    function render() {
      renderCanvas();
      renderControls();
    }

    // ---------------------------------------------------------------
    // The state machine
    // ---------------------------------------------------------------

    function transition(event, payload = {}) {
      assertEnum(Event, event);
      switch (event) {
        case Event.image_loaded:
          state.imageLoaded = true;
          state.imageWidth = payload.width;
          state.imageHeight = payload.height;
          canvasEl.width = payload.width;
          canvasEl.height = payload.height;
          state.annotations = payload.annotations || [];
          state.error = '';
          setMode(Mode.idle);
          applyZoom(fitZoom());
          captionEl.textContent = `Annotations are stored beside the image; the image file is never modified. (${payload.width} × ${payload.height} px)`;
          render();
          return;

        case Event.image_failed:
          state.imageLoaded = false;
          state.error = payload.message || 'Could not load the image.';
          renderControls();
          return;

        case Event.annotations_updated:
          state.annotations = payload.annotations || [];
          for (const id of [...state.hidden]) {
            if (!state.annotations.some((annotation) => annotation.id === id)) {
              state.hidden.delete(id);
            }
          }
          if (state.selectedId && !selectedAnnotation()) {
            state.selectedId = null;
            if (state.mode === Mode.selected) {
              setMode(Mode.idle);
            }
          }
          render();
          return;

        case Event.tool_changed:
          state.tool = assertEnum(Tool, payload.tool);
          state.draft = null;
          state.selectedId = null;
          setMode(Mode.idle);
          render();
          return;

        case Event.style_changed:
          state.lineColor = assertLineColor(payload.lineColor);
          state.lineWidth = assertLineWidth(payload.lineWidth);
          renderControls();
          return;

        case Event.zoom_changed: {
          if (!state.imageLoaded) {
            return;
          }
          const focal = viewportCentreFocal();
          applyZoom(payload.zoom);
          centerOn(focal.x, focal.y, focal.clientX, focal.clientY);
          render();
          return;
        }

        case Event.gesture_started: {
          if (!state.imageLoaded) {
            return;
          }
          const { x, y, distance } = pointerMidpoint();
          const rect = canvasEl.getBoundingClientRect();
          state.draft = null;
          state.pan = null;
          if (state.mode === Mode.drafting) {
            setMode(Mode.idle);
          }
          state.gesture = {
            startDistance: distance,
            startZoom: state.zoom,
            focalX: ((x - rect.left) / rect.width) * state.imageWidth,
            focalY: ((y - rect.top) / rect.height) * state.imageHeight,
          };
          render();
          return;
        }

        case Event.gesture_moved: {
          if (!state.gesture || state.pointers.size < 2) {
            return;
          }
          const { x, y, distance } = pointerMidpoint();
          applyZoom(state.gesture.startZoom * (distance / state.gesture.startDistance));
          centerOn(state.gesture.focalX, state.gesture.focalY, x, y);
          renderCanvas();
          return;
        }

        case Event.gesture_ended:
          state.gesture = null;
          render();
          return;

        case Event.pointer_down: {
          if (!state.imageLoaded || state.mode === Mode.note_dialog || state.gesture) {
            return;
          }
          const point = payload.point;
          if (state.tool === Tool.select) {
            // Pan with one finger and select on tap: the two are told apart on
            // release by whether the pointer moved.
            state.pan = {
              startClientX: payload.client.x,
              startClientY: payload.client.y,
              startScrollLeft: viewportEl.scrollLeft,
              startScrollTop: viewportEl.scrollTop,
              moved: false,
            };
            return;
          }
          if (state.tool === Tool.popup_note) {
            state.noteTarget = { id: null, x: point.x, y: point.y, text: '' };
            state.noteEditing = false;
            setMode(Mode.note_dialog);
            openNoteDialog();
            return;
          }
          state.selectedId = null;
          state.draft = { type: state.tool, start: point, current: point, points: [point] };
          setMode(Mode.drafting);
          render();
          return;
        }

        case Event.pointer_move:
          if (state.pan && state.pointers.size < 2) {
            const dx = payload.client.x - state.pan.startClientX;
            const dy = payload.client.y - state.pan.startClientY;
            if (!state.pan.moved && Math.hypot(dx, dy) > 4) {
              state.pan.moved = true;
            }
            if (state.pan.moved) {
              viewportEl.scrollLeft = state.pan.startScrollLeft - dx;
              viewportEl.scrollTop = state.pan.startScrollTop - dy;
              canvasEl.classList.add('panning');
            }
            return;
          }
          if (state.mode !== Mode.drafting || !state.draft) {
            return;
          }
          state.draft.current = payload.point;
          if (state.draft.type === AnnotationType.freehand) {
            const last = state.draft.points[state.draft.points.length - 1];
            if (Math.hypot(payload.point.x - last.x, payload.point.y - last.y) >= 1) {
              state.draft.points.push(payload.point);
            }
          }
          renderCanvas();
          return;

        case Event.pointer_up: {
          if (state.pan) {
            const moved = state.pan.moved;
            state.pan = null;
            canvasEl.classList.remove('panning');
            if (!moved) {
              const hit = hitTest(payload.point);
              state.selectedId = hit ? hit.id : null;
              setMode(hit ? Mode.selected : Mode.idle);
            }
            render();
            return;
          }
          if (state.mode !== Mode.drafting || !state.draft) {
            return;
          }
          const draft = state.draft;
          state.draft = null;
          setMode(Mode.idle);
          const preview = draftAsAnnotation(draft);
          if (!isCommittable(preview)) {
            render();
            return;
          }
          // The committed shape goes straight into the pending list, so it is
          // already on screen before the request leaves the browser.
          createAnnotation(preview).catch(reportError);
          // Drawing is done: hand the pointer back to the selection tool.
          transition(Event.tool_changed, { tool: Tool.select });
          return;
        }

        case Event.pointer_cancel:
          state.draft = null;
          state.pan = null;
          canvasEl.classList.remove('panning');
          setMode(Mode.idle);
          render();
          return;

        case Event.note_requested: {
          const selected = selectedAnnotation();
          if (!selected || selected.type !== AnnotationType.popup_note) {
            return;
          }
          state.noteTarget = { id: selected.id, x: selected.x, y: selected.y, text: selected.text };
          state.noteEditing = true;
          setMode(Mode.note_dialog);
          openNoteDialog();
          return;
        }

        case Event.note_saved: {
          const target = state.noteTarget;
          const editing = state.noteEditing;
          state.noteTarget = null;
          state.noteEditing = false;
          setMode(Mode.idle);
          if (!target) {
            render();
            return;
          }
          const changes = { text: payload.text, x: payload.x, y: payload.y };
          if (editing && target.id) {
            updateNote(target.id, changes).catch(reportError);
            return;
          }
          createAnnotation({
            type: AnnotationType.popup_note,
            x: changes.x,
            y: changes.y,
            text: changes.text,
          }).catch(reportError);
          transition(Event.tool_changed, { tool: Tool.select });
          return;
        }

        case Event.note_dialog_closed:
          state.noteTarget = null;
          state.noteEditing = false;
          setMode(Mode.idle);
          render();
          return;

        case Event.clear_active:
          state.draft = null;
          state.pan = null;
          state.selectedId = null;
          setMode(Mode.idle);
          render();
          return;

        case Event.history_applied:
          state.selectedId = null;
          state.draft = null;
          setMode(Mode.idle);
          render();
          return;
      }
    }

    function isCommittable(annotation) {
      if (!annotation) {
        return false;
      }
      switch (annotation.type) {
        case AnnotationType.rectangle:
          return annotation.width >= 1 && annotation.height >= 1;
        case AnnotationType.ellipse:
          return annotation.rx >= 0.5 && annotation.ry >= 0.5;
        case AnnotationType.line_segment:
          return Math.hypot(annotation.x2 - annotation.x1, annotation.y2 - annotation.y1) >= 1;
        case AnnotationType.freehand:
          return annotation.points.length >= 2;
        default:
          return false;
      }
    }

    function reportError(error) {
      console.error(error);
      state.error = error.message;
      renderControls();
      window.alert(error.message);
    }

    // ---------------------------------------------------------------
    // Pointer plumbing: one pointer draws, two pointers zoom and pan
    // ---------------------------------------------------------------

    function beginGesture() {
      transition(Event.gesture_started);
    }

    canvasEl.addEventListener('pointerdown', (event) => {
      if (event.button !== undefined && event.button !== 0 && event.pointerType === 'mouse') {
        return;
      }
      event.preventDefault();
      canvasEl.setPointerCapture(event.pointerId);
      state.pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
      if (state.pointers.size >= 2) {
        beginGesture();
        return;
      }
      transition(Event.pointer_down, {
        point: imagePoint(event.clientX, event.clientY),
        client: { x: event.clientX, y: event.clientY },
      });
    });

    canvasEl.addEventListener('pointermove', (event) => {
      if (!state.pointers.has(event.pointerId)) {
        return;
      }
      event.preventDefault();
      state.pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
      if (state.gesture) {
        transition(Event.gesture_moved);
        return;
      }
      if (state.mode === Mode.drafting || state.pan) {
        transition(Event.pointer_move, {
          point: imagePoint(event.clientX, event.clientY),
          client: { x: event.clientX, y: event.clientY },
        });
      }
    });

    function endPointer(event, cancelled) {
      if (!state.pointers.has(event.pointerId)) {
        return;
      }
      state.pointers.delete(event.pointerId);
      if (canvasEl.hasPointerCapture(event.pointerId)) {
        canvasEl.releasePointerCapture(event.pointerId);
      }
      if (state.gesture) {
        if (state.pointers.size < 2) {
          transition(Event.gesture_ended);
        }
        return;
      }
      if (cancelled) {
        transition(Event.pointer_cancel);
        return;
      }
      transition(Event.pointer_up, {
        point: imagePoint(event.clientX, event.clientY),
        client: { x: event.clientX, y: event.clientY },
      });
    }

    canvasEl.addEventListener('pointerup', (event) => endPointer(event, false));
    canvasEl.addEventListener('pointercancel', (event) => endPointer(event, true));

    // Wheel zoom, with Ctrl (or a trackpad pinch) to keep page scrolling intact.
    viewportEl.addEventListener(
      'wheel',
      (event) => {
        if (!event.ctrlKey || !state.imageLoaded) {
          return;
        }
        event.preventDefault();
        transition(Event.zoom_changed, { zoom: state.zoom * (event.deltaY < 0 ? zoomStep : 1 / zoomStep) });
      },
      { passive: false },
    );

    for (const button of toolButtonsEl) {
      button.addEventListener('click', () => {
        transition(Event.tool_changed, { tool: button.dataset.tool });
      });
    }

    colorInputEl.addEventListener('input', () => {
      transition(Event.style_changed, {
        lineColor: normalizeLineColor(colorInputEl.value),
        lineWidth: state.lineWidth,
      });
    });

    widthInputEl.addEventListener('input', () => {
      const value = normalizeLineWidth(widthInputEl.value);
      widthInputEl.value = String(value);
      transition(Event.style_changed, { lineColor: state.lineColor, lineWidth: value });
    });

    zoomInBtnEl.addEventListener('click', () => {
      transition(Event.zoom_changed, { zoom: state.zoom * zoomStep });
    });

    zoomOutBtnEl.addEventListener('click', () => {
      transition(Event.zoom_changed, { zoom: state.zoom / zoomStep });
    });

    zoomFitBtnEl.addEventListener('click', () => {
      transition(Event.zoom_changed, { zoom: fitZoom() });
    });

    undoBtnEl.addEventListener('click', async () => {
      const entry = state.undoStack.pop();
      if (!entry) {
        return;
      }
      try {
        await entry.undo();
        state.redoStack.push(entry);
        transition(Event.history_applied);
      } catch (error) {
        state.undoStack.push(entry);
        reportError(error);
      }
    });

    redoBtnEl.addEventListener('click', async () => {
      const entry = state.redoStack.pop();
      if (!entry) {
        return;
      }
      try {
        await entry.redo();
        state.undoStack.push(entry);
        transition(Event.history_applied);
      } catch (error) {
        state.redoStack.push(entry);
        reportError(error);
      }
    });

    deleteBtnEl.addEventListener('click', async () => {
      const selected = selectedAnnotation();
      if (!selected) {
        return;
      }
      try {
        await removeAnnotation(selected);
        transition(Event.history_applied);
      } catch (error) {
        reportError(error);
      }
    });

    modifyBtnEl.addEventListener('click', () => {
      transition(Event.note_requested);
    });

    saveNoteEl.addEventListener('click', () => {
      if (state.mode !== Mode.note_dialog) {
        return;
      }
      const x = clamp(Number(noteXEl.value) || 0, 0, state.imageWidth);
      const y = clamp(Number(noteYEl.value) || 0, 0, state.imageHeight);
      // Leave note_dialog first, so the close handler cannot fire twice.
      transition(Event.note_saved, { text: noteTextEl.value, x, y });
      noteModalEl.close();
    });

    cancelNoteEl.addEventListener('click', () => {
      noteModalEl.close();
    });

    noteModalEl.addEventListener('close', () => {
      if (state.mode === Mode.note_dialog) {
        transition(Event.note_dialog_closed);
      }
    });

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && state.mode !== Mode.note_dialog) {
        transition(Event.clear_active);
      }
    });

    window.addEventListener('resize', () => {
      if (state.imageLoaded) {
        renderCanvas();
      }
    });

    function openNoteDialog() {
      const target = state.noteTarget;
      if (!target) {
        return;
      }
      noteTextEl.value = target.text ?? '';
      noteXEl.value = String(Math.round(target.x));
      noteYEl.value = String(Math.round(target.y));
      if (!noteModalEl.open) {
        noteModalEl.showModal();
      }
      window.setTimeout(() => noteTextEl.focus(), 0);
    }

    async function loadPage() {
      const annotationsResponse = await fetch(annotationsApiPath);
      const annotationsData = await annotationsResponse.json();
      if (!annotationsResponse.ok) {
        throw new Error(annotationsData.error || `HTTP ${annotationsResponse.status}`);
      }
      await new Promise((resolve, reject) => {
        image.addEventListener('load', resolve, { once: true });
        image.addEventListener('error', () => reject(new Error('Could not load the image.')), {
          once: true,
        });
        image.src = imageUrl;
      });
      transition(Event.image_loaded, {
        width: image.naturalWidth,
        height: image.naturalHeight,
        annotations: annotationsData.annotations,
      });
    }

    renderControls();

    loadPage().catch((error) => {
      console.error(error);
      transition(Event.image_failed, { message: error.message });
      window.alert(error.message);
    });
  </script>
</body>
</html>)TXTHTML";

} // namespace txthighlight::assets

#endif // IMAGE_PAGE_HPP
