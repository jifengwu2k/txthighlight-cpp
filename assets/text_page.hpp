#ifndef TEXT_PAGE_HPP
#define TEXT_PAGE_HPP

#include <string_view>

namespace txthighlight::assets {

// The UTF-8 text annotation frontend, inlined so the binary is self-contained.
// It fetches the text file directly, fetches GET /api/annotations/text, and
// POSTs to /api/annotations/text. The text API reference and its CSS are
// inserted where the API_DOCS markers are.
inline constexpr std::string_view text_page =
    R"TXTHTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Text Highlighter</title>
  <script>
    (() => {
      const path = new URLSearchParams(window.location.search).get('path') || '';
      document.title = (path.split('/').pop() || '') + ' - Text Highlighter';
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
      font-size: clamp(1rem, 0.95rem + 0.25vw, 1.125rem);
      line-height: 1.6;
      color: #17171a;
      background: #fff;
    }

    main {
      max-width: 68ch;
      margin: 0 auto;
      padding: 1.5rem max(1rem, env(safe-area-inset-left)) 2rem
        max(1rem, env(safe-area-inset-right));
      white-space: pre-wrap;
      overflow-wrap: break-word;
    }

    mark.highlight {
      background: yellow;
      color: inherit;
      border-radius: 0.125rem;
      scroll-margin-block: 8rem 2rem;
    }

    /* Selection is an outline plus a glow: the highlight keeps its own colour. */
    mark.highlight[aria-selected="true"] {
      outline: 2px solid var(--accent);
      outline-offset: 1px;
      box-shadow: 0 0 0 4px rgba(10, 108, 255, 0.22);
    }

    /* 2.75rem is 44px, the minimum comfortable touch target. */
    button {
      min-height: 2.75rem;
      padding: 0 1.125rem;
      border: 1px solid var(--border);
      border-radius: var(--radius);
      background: var(--surface);
      color: inherit;
      font: inherit;
      font-weight: 500;
      cursor: pointer;
      touch-action: manipulation;
    }

    button:hover:not(:disabled) { background: #eaeaef; }
    button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }

    button:disabled {
      background: #f4f4f7;
      border-color: #e2e2e8;
      color: #6e6e78;
      cursor: default;
    }

    header#topMenu {
      position: fixed;
      inset: 0 0 auto;
      z-index: 10;
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: var(--gap);
      padding: calc(0.625rem + env(safe-area-inset-top))
        max(0.625rem, env(safe-area-inset-right)) 0.625rem
        max(0.625rem, env(safe-area-inset-left));
      background: rgba(255, 255, 255, 0.94);
      border-bottom: 1px solid var(--border);
    }

    header#topMenu button {
      flex: 0 1 11rem;
      min-width: 7.5rem;
    }

    output#hint {
      flex: 1 1 100%;
      min-height: 1.25rem;
      color: var(--muted);
      font-size: 0.8125rem;
      text-align: center;
    }

    /* A card on desktop, a sheet on phones. */
    dialog#commentModal {
      width: min(36rem, calc(100vw - 2rem));
      max-height: 85vh;
      padding: 0;
      border: 1px solid var(--border);
      border-radius: 0.875rem;
      box-shadow: 0 1.5rem 4rem rgba(0, 0, 0, 0.28);
      color: inherit;
      overflow: auto;
    }

    dialog#commentModal::backdrop { background: rgba(0, 0, 0, 0.45); }

    dialog#commentModal form {
      display: flex;
      flex-direction: column;
      gap: 0.875rem;
      padding: clamp(1rem, 2.5vw, 1.5rem);
      padding-bottom: calc(clamp(1rem, 2.5vw, 1.5rem) + env(safe-area-inset-bottom));
    }

    dialog#commentModal h2 {
      margin: 0;
      font-size: 1.0625rem;
    }

    #commentSnippet {
      max-height: 26vh;
      overflow: auto;
      padding: 0.625rem 0.75rem;
      border: 1px solid #e4e4ea;
      border-radius: var(--radius);
      background: var(--surface);
      font-size: 0.9375rem;
      white-space: pre-wrap;
      overflow-wrap: break-word;
    }

    label.comment-field {
      display: block;
      font-size: 0.875rem;
      font-weight: 600;
    }

    textarea#commentBox {
      width: 100%;
      min-height: 9rem;
      max-height: 45vh;
      margin-top: 0.375rem;
      padding: 0.75rem;
      border: 1px solid var(--border);
      border-radius: var(--radius);
      font: inherit;
      font-size: 1rem; /* 16px keeps iOS from zooming on focus */
      resize: vertical;
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
      padding: 0 max(1rem, env(safe-area-inset-left)) 0
        max(1rem, env(safe-area-inset-right));
    }

    .api-docs-wrap > summary {
      min-height: 2.75rem;
      padding: 0.75rem 0;
      font-weight: 600;
      cursor: pointer;
    }

    /* API_DOCS_STYLE */

    @media (max-width: 40rem) {
      header#topMenu button {
        flex: 1 1 0;
        min-width: 0;
        min-height: 3rem;
        padding: 0 0.5rem;
        font-size: 0.9375rem;
      }

      dialog#commentModal {
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
  <header id="topMenu">
    <button type="button" id="highlightBtn" disabled>Highlight</button>
    <button type="button" id="commentBtn" disabled>Comment</button>
    <button type="button" id="removeBtn" disabled>Remove</button>
    <output id="hint" aria-live="polite"></output>
  </header>

  <main id="document"></main>

  <details class="api-docs-wrap">
    <summary>REST API for scripts and agents</summary>
    <!-- API_DOCS -->
  </details>

  <dialog id="commentModal" aria-labelledby="commentTitle">
    <form method="dialog">
      <h2 id="commentTitle">Comment</h2>
      <div id="commentSnippet"></div>
      <label class="comment-field" for="commentBox">Comment
        <textarea id="commentBox" placeholder="Add a note for this highlight…"></textarea>
      </label>
      <div class="dialog-actions">
        <button type="button" id="cancelComment">Cancel</button>
        <button type="button" id="saveComment" class="primary">Save comment</button>
      </div>
    </form>
  </dialog>

  <script>
    // ---------------------------------------------------------------------
    // Enums
    //
    // ES6 has no `enum` keyword, so a closed set of values is a frozen object
    // and every assignment is validated: a value outside the set throws instead
    // of flowing silently through the state machine.
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

    const Mode = createEnum('Mode', [
      'idle',
      'selection_active',
      'selection_menu_active',
      'highlight_active',
      'comment_dialog',
    ]);

    const Event = createEnum('Event', [
      'document_loaded',
      'selection_changed',
      'menu_interaction_started',
      'menu_interaction_ended',
      'highlight_selected',
      'open_comment_dialog',
      'comment_saved',
      'comment_dialog_closed',
      'clear_active',
      'annotations_updated',
    ]);

    const Action = createEnum('Action', ['add', 'remove_range', 'update_comment', 'remove_id']);

    const state = {
      mode: Mode.idle,
      text: '',
      annotations: [],
      fileName: '',
      selection: null,
      highlightId: null,
      commentTargetId: null,
    };

    function setMode(next) {
      state.mode = assertEnum(Mode, next);
    }

    // The sidecar stores *character* (codepoint) offsets while the DOM works in
    // UTF-16 units, so astral characters (emoji, rare CJK) keep their boundaries.
    let cpToU16 = [0];
    let u16ToCp = new Int32Array(1);

    function buildOffsetMaps(text) {
      cpToU16 = [0];
      u16ToCp = new Int32Array(text.length + 1);
      let u16 = 0;
      for (const ch of text) {
        for (let k = 0; k < ch.length; k += 1) {
          u16ToCp[u16 + k] = cpToU16.length - 1;
        }
        u16 += ch.length;
        cpToU16.push(u16);
      }
      u16ToCp[text.length] = cpToU16.length - 1;
    }

    function codepointLength() {
      return cpToU16.length - 1;
    }

    function sliceCodepoints(start, end) {
      const length = codepointLength();
      const from = cpToU16[Math.max(0, Math.min(start, length))];
      const to = cpToU16[Math.max(0, Math.min(end, length))];
      return state.text.slice(from, to);
    }

    const documentPath = new URLSearchParams(window.location.search).get('path') || '/';
    const documentUrl = documentPath.split('/').map(encodeURIComponent).join('/') || '/';
    const annotationsApiPath = `/api/annotations/text?path=${encodeURIComponent(documentPath)}`;

    const docEl = document.getElementById('document');
    const topMenuEl = document.getElementById('topMenu');
    const hintEl = document.getElementById('hint');
    const highlightBtnEl = document.getElementById('highlightBtn');
    const commentBtnEl = document.getElementById('commentBtn');
    const removeBtnEl = document.getElementById('removeBtn');
    const commentModalEl = document.getElementById('commentModal');
    const commentSnippetEl = document.getElementById('commentSnippet');
    const commentBoxEl = document.getElementById('commentBox');
    const saveCommentEl = document.getElementById('saveComment');
    const cancelCommentEl = document.getElementById('cancelComment');

    function showError(message) {
      if (message) {
        window.alert(message);
      }
    }

    function syncTopMenuOffset() {
      document.body.style.paddingTop = `${topMenuEl.offsetHeight}px`;
    }

    function escapeHtml(text) {
      return text
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
    }

    function findAnnotationById(id) {
      return state.annotations.find((item) => item.id === id) || null;
    }

    function renderDocument() {
      const annotations = [...state.annotations].sort((a, b) => a.start - b.start || a.end - b.end);
      const length = codepointLength();
      let cursor = 0;
      let html = '';
      for (const ann of annotations) {
        if (ann.start > cursor) {
          html += escapeHtml(sliceCodepoints(cursor, ann.start));
        }
        const text = escapeHtml(sliceCodepoints(ann.start, ann.end));
        const title = ann.comment ? escapeHtml(ann.comment) : 'Highlight';
        const selected = ann.id === state.highlightId;
        html += `<mark class="highlight" data-id="${ann.id}" title="${title}" aria-selected="${selected}">${text}</mark>`;
        cursor = ann.end;
      }
      if (cursor < length) {
        html += escapeHtml(sliceCodepoints(cursor, length));
      }
      docEl.innerHTML = html || '(empty file)';
    }

    function getTextOffset(container, targetNode, targetOffset) {
      let total = 0;
      const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
      let node;
      while ((node = walker.nextNode())) {
        if (node === targetNode) {
          return total + targetOffset;
        }
        total += node.textContent.length;
      }
      return null;
    }

    function getSelectionOffsets() {
      const selection = window.getSelection();
      if (!selection || selection.rangeCount === 0) {
        return null;
      }
      const range = selection.getRangeAt(0);
      if (range.collapsed || !docEl.contains(range.startContainer) || !docEl.contains(range.endContainer)) {
        return null;
      }
      const start = getTextOffset(docEl, range.startContainer, range.startOffset);
      const end = getTextOffset(docEl, range.endContainer, range.endOffset);
      if (start == null || end == null || start === end) {
        return null;
      }
      const startCp = u16ToCp[start];
      const endCp = u16ToCp[end];
      if (startCp === endCp) {
        return null;
      }
      return startCp < endCp ? { start: startCp, end: endCp } : { start: endCp, end: startCp };
    }

    function openCommentDialog(id) {
      const ann = findAnnotationById(id);
      if (!ann || (commentModalEl.open && commentModalEl.dataset.targetId === id)) {
        return;
      }
      commentModalEl.dataset.targetId = id;
      commentSnippetEl.textContent = sliceCodepoints(ann.start, ann.end);
      commentBoxEl.value = ann.comment || '';
      if (!commentModalEl.open) {
        commentModalEl.showModal();
      }
      window.setTimeout(() => commentBoxEl.focus(), 0);
    }

    function hintText() {
      if (state.mode === Mode.comment_dialog) {
        return 'Editing the comment. Save or cancel when done.';
      }
      if (state.mode === Mode.highlight_active) {
        return 'Highlight selected. Comment edits its note, Remove deletes it.';
      }
      if (state.selection) {
        return 'Text selected. Highlight marks it, Comment marks it and opens a note.';
      }
      return 'Select text to highlight it, or click a highlight to comment on it.';
    }

    function renderUI() {
      const hasSelection = Boolean(state.selection);
      const hasHighlight = Boolean(state.highlightId && findAnnotationById(state.highlightId));

      highlightBtnEl.disabled = !hasSelection;
      commentBtnEl.disabled = !hasSelection && !hasHighlight;
      removeBtnEl.disabled = !hasHighlight;
      hintEl.textContent = hintText();

      if (state.fileName) {
        document.title = state.fileName + ' - Text Highlighter';
      }

      // The markup is only rebuilt when annotations change, so the selection
      // attribute has to be refreshed here on every render.
      for (const element of docEl.querySelectorAll('mark.highlight')) {
        element.setAttribute(
          'aria-selected',
          String(element.dataset.id === state.highlightId),
        );
      }

      if (state.mode === Mode.comment_dialog && state.commentTargetId) {
        openCommentDialog(state.commentTargetId);
      } else if (commentModalEl.open) {
        commentModalEl.close();
      }
    }

    function transition(event, payload = {}) {
      assertEnum(Event, event);
      switch (event) {
        case Event.document_loaded:
          state.text = payload.text || '';
          state.annotations = payload.annotations || [];
          state.fileName = documentPath.split('/').pop() || '';
          buildOffsetMaps(state.text);
          setMode(Mode.idle);
          state.selection = null;
          state.highlightId = null;
          state.commentTargetId = null;
          renderDocument();
          renderUI();
          return;

        case Event.selection_changed:
          if (payload.selection) {
            setMode(Mode.selection_active);
            state.selection = payload.selection;
            state.highlightId = null;
            state.commentTargetId = null;
          } else if (state.mode === Mode.selection_active) {
            setMode(Mode.idle);
            state.selection = null;
          }
          renderUI();
          return;

        case Event.menu_interaction_started:
          if (state.selection || payload.selection) {
            setMode(Mode.selection_menu_active);
            state.selection = state.selection || payload.selection;
            state.highlightId = null;
            state.commentTargetId = null;
          }
          renderUI();
          return;

        case Event.menu_interaction_ended:
          if (state.mode === Mode.selection_menu_active) {
            setMode(state.selection ? Mode.selection_active : Mode.idle);
            renderUI();
          }
          return;

        case Event.highlight_selected:
          if (!payload.id) {
            return;
          }
          setMode(Mode.highlight_active);
          state.highlightId = payload.id;
          state.selection = null;
          state.commentTargetId = null;
          window.getSelection()?.removeAllRanges();
          renderUI();
          return;

        case Event.open_comment_dialog:
          if (!payload.id) {
            return;
          }
          setMode(Mode.comment_dialog);
          state.highlightId = payload.id;
          state.selection = null;
          state.commentTargetId = payload.id;
          renderUI();
          return;

        case Event.comment_saved:
        case Event.comment_dialog_closed:
          if (state.highlightId && findAnnotationById(state.highlightId)) {
            setMode(Mode.highlight_active);
          } else {
            setMode(Mode.idle);
            state.highlightId = null;
          }
          state.commentTargetId = null;
          renderUI();
          return;

        case Event.clear_active:
          setMode(Mode.idle);
          state.selection = null;
          state.highlightId = null;
          state.commentTargetId = null;
          renderUI();
          return;

        case Event.annotations_updated:
          state.annotations = payload.annotations || [];
          renderDocument();
          if (state.highlightId && !findAnnotationById(state.highlightId)) {
            state.highlightId = null;
            if (state.mode !== Mode.selection_active && state.mode !== Mode.selection_menu_active) {
              setMode(Mode.idle);
            }
          }
          if (state.commentTargetId && !findAnnotationById(state.commentTargetId)) {
            state.commentTargetId = null;
            if (state.mode === Mode.comment_dialog) {
              setMode(state.highlightId ? Mode.highlight_active : Mode.idle);
            }
          }
          renderUI();
          return;
      }
    }

    async function mutate(payload) {
      assertEnum(Action, payload.action);
      try {
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
      } catch (error) {
        console.error(error);
        showError(error.message);
        return null;
      }
    }

    async function loadDocument() {
      const [documentResponse, annotationsResponse] = await Promise.all([
        fetch(documentUrl),
        fetch(annotationsApiPath),
      ]);
      const annotationsData = await annotationsResponse.json();
      if (!annotationsResponse.ok) {
        throw new Error(annotationsData.error || `HTTP ${annotationsResponse.status}`);
      }
      if (!documentResponse.ok) {
        let message = `HTTP ${documentResponse.status}`;
        try {
          message = (await documentResponse.json()).error || message;
        } catch {}
        throw new Error(message);
      }
      transition(Event.document_loaded, {
        text: await documentResponse.text(),
        annotations: annotationsData.annotations,
      });
    }

    function findHighlightForSelection(selection, annotations) {
      const matches = annotations
        .filter((ann) => ann.start <= selection.start && ann.end >= selection.end)
        .sort((a, b) => a.end - a.start - (b.end - b.start));
      return matches[0] || null;
    }

    topMenuEl.addEventListener('pointerdown', () => {
      transition(Event.menu_interaction_started, { selection: getSelectionOffsets() });
    });

    topMenuEl.addEventListener('pointerup', () => {
      window.setTimeout(() => transition(Event.menu_interaction_ended), 0);
    });

    topMenuEl.addEventListener('pointercancel', () => transition(Event.menu_interaction_ended));

    saveCommentEl.addEventListener('click', async () => {
      const id = state.commentTargetId || state.highlightId;
      if (!id) return;
      const updated = await mutate({ action: Action.update_comment, id, comment: commentBoxEl.value });
      if (updated) transition(Event.comment_saved);
    });

    cancelCommentEl.addEventListener('click', () => transition(Event.comment_dialog_closed));

    commentModalEl.addEventListener('close', () => {
      if (state.mode === Mode.comment_dialog) {
        transition(Event.comment_dialog_closed);
      }
    });

    highlightBtnEl.addEventListener('click', async () => {
      const selection = state.selection;
      if (!selection) return;
      window.getSelection()?.removeAllRanges();
      transition(Event.clear_active);
      await mutate({ action: Action.add, start: selection.start, end: selection.end });
    });

    commentBtnEl.addEventListener('click', async () => {
      const selection = state.selection;
      if (selection) {
        window.getSelection()?.removeAllRanges();
        transition(Event.clear_active);
        const annotations = await mutate({
          action: Action.add,
          start: selection.start,
          end: selection.end,
        });
        if (!annotations) return;
        const ann = findHighlightForSelection(selection, annotations);
        if (!ann) return;
        transition(Event.open_comment_dialog, { id: ann.id });
        return;
      }

      const id = state.highlightId;
      if (!id) return;
      transition(Event.open_comment_dialog, { id });
    });

    removeBtnEl.addEventListener('click', async () => {
      const id = state.highlightId;
      if (!id) return;
      transition(Event.clear_active);
      await mutate({ action: Action.remove_id, id });
    });

    docEl.addEventListener('click', (event) => {
      const highlight = event.target.closest('.highlight');
      if (highlight) {
        transition(Event.highlight_selected, { id: highlight.dataset.id });
        return;
      }
      if (!getSelectionOffsets()) {
        transition(Event.clear_active);
      }
    });

    document.addEventListener('selectionchange', () => {
      if (state.mode === Mode.comment_dialog || document.activeElement === commentBoxEl) {
        return;
      }
      const selection = getSelectionOffsets();
      if (selection) {
        transition(Event.selection_changed, { selection });
        window.requestAnimationFrame(renderUI);
      } else if (state.mode !== Mode.selection_menu_active) {
        transition(Event.selection_changed, { selection: null });
      }
    });

    document.addEventListener('pointerup', () => {
      if (state.mode === Mode.comment_dialog || document.activeElement === commentBoxEl) {
        return;
      }
      window.setTimeout(() => {
        const selection = getSelectionOffsets();
        if (selection) {
          transition(Event.selection_changed, { selection });
        }
      }, 0);
    });

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') {
        window.getSelection()?.removeAllRanges();
        transition(Event.clear_active);
      }
    });

    document.addEventListener('click', (event) => {
      if (event.target.closest('#topMenu') || event.target.closest('.highlight')) {
        return;
      }
      if (
        !getSelectionOffsets() &&
        (state.mode === Mode.highlight_active ||
          state.mode === Mode.selection_active ||
          state.mode === Mode.selection_menu_active)
      ) {
        transition(Event.clear_active);
      }
    });

    window.addEventListener('resize', syncTopMenuOffset);
    window.requestAnimationFrame(syncTopMenuOffset);

    loadDocument().catch((error) => {
      console.error(error);
      showError(error.message);
      docEl.textContent = 'Failed to load file.';
    });
  </script>
</body>
</html>)TXTHTML";

} // namespace txthighlight::assets

#endif // TEXT_PAGE_HPP
