#ifndef API_DOCS_HPP
#define API_DOCS_HPP

#include <string_view>

namespace txthighlight::assets {

// REST API reference, split by page. Each HTML page embeds only the section
// that documents its own endpoints:
//   directory_api_docs -> directory listing page
//   text_api_docs      -> UTF-8 text annotation page
//   image_api_docs     -> raster image annotation page
//
// Every section contains the marker "<!-- SIDECAR_MODE -->", replaced at render
// time with a sentence about whether sidecar files are enabled (see
// render_sidecar_note() in main.cpp).

// Styles shared by the pages that embed an API reference.
inline constexpr std::string_view api_docs_style = R"TXTCSS(
.api-docs { margin-top: 1.25rem; font-size: 0.9375rem; line-height: 1.55; }
.api-docs h2 { margin: 0 0 0.5rem; font-size: 1.15rem; }
.api-docs h3 { margin: 1rem 0 0.375rem; font-size: 1rem; }
.api-docs :is(p, ul) { margin: 0.5rem 0; }
.api-docs ul { padding-left: 1.25rem; }
.api-docs code { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 0.875em; overflow-wrap: anywhere; }
.api-docs pre { margin: 0.5rem 0 0.875rem; padding: 0.625rem; overflow-x: auto; border: 1px solid var(--border, #d5d5dc); border-radius: var(--radius, 0.625rem); background: var(--surface, #f5f5f8); line-height: 1.45; }
.api-docs pre code { overflow-wrap: normal; }
.api-docs table { width: 100%; margin: 0.5rem 0 0.875rem; border-collapse: collapse; font-size: 0.875rem; }
.api-docs :is(th, td) { padding: 0.3125rem 0.5rem; border: 1px solid var(--border, #d5d5dc); text-align: left; vertical-align: top; }
.api-docs th { background: var(--surface, #f5f5f8); font-weight: 600; }
.api-docs .sidecar-mode { padding: 0.5rem 0.625rem; border: 1px solid var(--border, #d5d5dc); border-radius: var(--radius, 0.625rem); background: var(--surface, #f5f5f8); }
.api-docs .example-note { color: var(--muted, #6b6b74); font-size: 0.875rem; }

@media (max-width: 40rem) {
  .api-docs :is(table, thead, tbody, tr, th, td) { display: block; }
  .api-docs thead { display: none; }
  .api-docs td { border-top: 0; }
  .api-docs td:first-child { border-top: 1px solid var(--border, #d5d5dc); }
}
)TXTCSS";

// ---------------------------------------------------------------------------
// 1. Directory listing page
// ---------------------------------------------------------------------------

inline constexpr std::string_view directory_api_docs =
    R"TXTHTML(<section class="api-docs" aria-labelledby="api-directory-heading">
<h2 id="api-directory-heading">REST API: directory listing</h2>
<p>
  <strong>Base URL</strong> is this origin. Paths are relative to the served
  root, where <code>/</code> is the root.
</p>
<p class="sidecar-mode"><!-- SIDECAR_MODE --></p>

<table>
  <thead><tr><th>Method</th><th>Endpoint</th><th>What it does</th></tr></thead>
  <tbody>
    <tr><td><code>GET</code>, <code>HEAD</code></td><td><code>/</code>, <code>/subdir/</code></td><td>This listing. Each row links to its annotation view and carries a Download link.</td></tr>
    <tr><td><code>GET</code>, <code>HEAD</code></td><td><code>/notes.txt</code>, <code>/photo.png</code></td><td>Download the original file, unchanged. Supports HTTP Range requests.</td></tr>
    <tr><td><code>POST</code></td><td><code>/subdir/</code></td><td>Upload one or more files with <code>multipart/form-data</code>; answers <code>303 See Other</code> back to the directory.</td></tr>
    <tr><td><code>GET</code></td><td><code>/annotate?path=%2Fnotes.txt</code></td><td>Annotation UI for one file: text or image, chosen from the file's content.</td></tr>
  </tbody>
</table>

<pre><code>curl 'http://127.0.0.1:8000/'                       # this listing
curl -O 'http://127.0.0.1:8000/photo.png'           # original file</code></pre>
<p class="example-note">
  A file that is neither UTF-8 text nor a supported raster image still
  downloads; only its annotation view is unavailable.
</p>

<h3>Resumable downloads</h3>
<p>
  File responses carry <code>Accept-Ranges: bytes</code>. A <code>Range</code>
  request returns <code>206 Partial Content</code> with
  <code>Content-Range</code>; an unsatisfiable range returns <code>416</code>
  with <code>Content-Range: bytes */&lt;size&gt;</code>. Downloads are streamed,
  so file size is not limited by memory.
</p>
<pre><code>curl -O -C - 'http://127.0.0.1:8000/large.bin'      # resume automatically
curl -H 'Range: bytes=0-1023' 'http://127.0.0.1:8000/large.bin'
curl -I 'http://127.0.0.1:8000/large.bin'           # size and range support</code></pre>

<h3>Uploads</h3>
<p>
  <code>POST</code> to any directory with <code>multipart/form-data</code>.
  Every part with a filename is streamed straight to disk and renamed into
  place when complete, so a dropped upload never replaces an existing file with
  a truncated one. Only the final path component of a filename is used.
</p>
<pre><code>curl -X POST \
  -F 'file=@/path/to/photo.jpg' \
  -F 'file=@/path/to/document.pdf' \
  'http://127.0.0.1:8000/subdir/'</code></pre>

<h3>Errors</h3>
<p>
  <code>{"error": "message"}</code> with <code>400</code> for a malformed
  multipart body or invalid upload filename, <code>404</code> for a missing
  path, <code>416</code> for an unsatisfiable range, <code>500</code> for a
  filesystem failure, and <code>501</code> for methods other than
  <code>GET</code>, <code>HEAD</code>, and <code>POST</code>.
</p>
</section>)TXTHTML";

// ---------------------------------------------------------------------------
// 2. UTF-8 text annotation page
// ---------------------------------------------------------------------------

inline constexpr std::string_view text_api_docs =
    R"TXTHTML(<section class="api-docs" aria-labelledby="api-text-heading">
<h2 id="api-text-heading">REST API: UTF-8 text annotations</h2>
<p>
  <strong>Base URL</strong> is this origin. Put the file path in the
  <code>path</code> query parameter, URL-encoded: <code>/notes.txt</code>
  becomes <code>%2Fnotes.txt</code>. Paths containing <code>..</code> and
  symlinks pointing outside the root are rejected.
</p>
<p class="sidecar-mode"><!-- SIDECAR_MODE --></p>

<table>
  <thead><tr><th>Method</th><th>Endpoint</th><th>What it does</th></tr></thead>
  <tbody>
    <tr><td><code>GET</code></td><td><code>/api/annotations/text?path=%2Fnotes.txt</code></td><td>Read highlights and comments in document order.</td></tr>
    <tr><td><code>POST</code></td><td><code>/api/annotations/text?path=%2Fnotes.txt</code></td><td>Add, remove, or update highlights and comments.</td></tr>
  </tbody>
</table>

<h3>Reading</h3>
<pre><code>curl 'http://127.0.0.1:8000/api/annotations/text?path=%2Fnotes.txt'

{"kind": "text",
 "annotations": [{"id": "3d7278a2-…", "start": 0, "end": 10,
                  "comment": "Keep this",
                  "created_at": "2026-11-23T13:00:00Z",
                  "updated_at": "2026-11-23T13:00:00Z"}]}</code></pre>

<h3>Writing</h3>
<p>
  Send <code>{"action": …}</code>; ids are generated by the server and the
  response is the full document, as above.
</p>
<table>
  <thead><tr><th><code>action</code></th><th>Fields</th><th>Effect</th></tr></thead>
  <tbody>
    <tr><td><code>add</code></td><td><code>start</code>, <code>end</code>, optional <code>comment</code></td><td>Add a highlight. Overlapping and touching highlights merge; comments are joined with blank lines.</td></tr>
    <tr><td><code>remove_range</code></td><td><code>start</code>, <code>end</code></td><td>Cut every highlight at this range; surviving pieces get new ids.</td></tr>
    <tr><td><code>update_comment</code></td><td><code>id</code>, <code>comment</code></td><td>Replace one comment.</td></tr>
    <tr><td><code>remove_id</code></td><td><code>id</code></td><td>Delete one highlight.</td></tr>
  </tbody>
</table>
<pre><code>curl -X POST 'http://127.0.0.1:8000/api/annotations/text?path=%2Fnotes.txt' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add","start":0,"end":10,"comment":"Keep this"}'

curl -X POST 'http://127.0.0.1:8000/api/annotations/text?path=%2Fnotes.txt' \
  -H 'Content-Type: application/json' \
  -d '{"action":"remove_id","id":"3d7278a2-…"}'</code></pre>

<h3>Fields</h3>
<ul>
  <li><code>start</code>, <code>end</code>: half-open character (Unicode codepoint) offsets, not bytes or UTF-16 units.</li>
  <li><code>comment</code>: free text, may be empty.</li>
  <li><code>created_at</code>, <code>updated_at</code>: ISO-8601 UTC, e.g. <code>2026-11-23T13:00:00Z</code>.</li>
</ul>

<h3>Errors</h3>
<p>
  <code>{"error": "message"}</code> with <code>400</code> for a bad action or
  offset, a non-UTF-8 file, or an unreadable sidecar; <code>404</code> for a
  missing path; <code>501</code> for other methods.
</p>
</section>)TXTHTML";

// ---------------------------------------------------------------------------
// 3. Raster image annotation page
// ---------------------------------------------------------------------------

inline constexpr std::string_view image_api_docs =
    R"TXTHTML(<section class="api-docs" aria-labelledby="api-image-heading">
<h2 id="api-image-heading">REST API: raster image annotations</h2>
<p>
  <strong>Base URL</strong> is this origin. Put the file path in the
  <code>path</code> query parameter, URL-encoded: <code>/photo.png</code>
  becomes <code>%2Fphoto.png</code>. Supported formats are PNG, JPEG, WebP, and
  GIF; the format is detected from the content, not the name.
</p>
<p>
  Coordinates are <strong>image pixels, origin at the top-left corner</strong>,
  <code>+x</code> right and <code>+y</code> down — the OpenCV, PyTorch
  <code>torchvision</code>, COCO, and SVG convention. Screen zoom does not
  affect them.
</p>
<p class="sidecar-mode"><!-- SIDECAR_MODE --></p>

<table>
  <thead><tr><th>Method</th><th>Endpoint</th><th>What it does</th></tr></thead>
  <tbody>
    <tr><td><code>GET</code></td><td><code>/api/annotations/image?path=%2Fphoto.png</code></td><td>Read annotations in paint order.</td></tr>
    <tr><td><code>POST</code></td><td><code>/api/annotations/image?path=%2Fphoto.png</code></td><td>Add, modify, or remove shapes and notes.</td></tr>
  </tbody>
</table>

<h3>Reading</h3>
<pre><code>curl 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png'

{"kind": "image",
 "annotations": [{"id": "a1b2c3d4-…", "type": "rectangle",
                  "x": 120, "y": 80, "width": 240, "height": 160,
                  "line_color": "#ff3b30", "line_width": 3,
                  "created_at": "2026-11-23T13:00:00Z",
                  "updated_at": "2026-11-23T13:00:00Z"},
                 {"id": "e5f6a7b8-…", "type": "popup_note",
                  "x": 300, "y": 150, "text": "Inspect this region",
                  "created_at": "2026-11-23T13:01:00Z",
                  "updated_at": "2026-11-23T13:01:00Z"}]}</code></pre>

<h3>Writing</h3>
<p>
  Send <code>{"action": …}</code>; ids are generated by the server and the
  response is the full document, as above. Later entries are drawn on top.
</p>
<table>
  <thead><tr><th><code>action</code></th><th>Fields</th><th>Effect</th></tr></thead>
  <tbody>
    <tr><td><code>add_rectangle</code></td><td><code>x</code>, <code>y</code>, <code>width</code>, <code>height</code></td><td>Rectangle; width and height must be positive.</td></tr>
    <tr><td><code>add_ellipse</code></td><td><code>cx</code>, <code>cy</code>, <code>rx</code>, <code>ry</code></td><td>Ellipse; both radii must be positive.</td></tr>
    <tr><td><code>add_line_segment</code></td><td><code>x1</code>, <code>y1</code>, <code>x2</code>, <code>y2</code></td><td>Segment; the endpoints must differ.</td></tr>
    <tr><td><code>add_freehand</code></td><td><code>points</code></td><td>Stroke through <code>{"x", "y"}</code> objects, at least two.</td></tr>
    <tr><td><code>add_popup_note</code></td><td><code>x</code>, <code>y</code>, <code>text</code></td><td>Popup note anchored at the point.</td></tr>
    <tr><td><code>update_popup_note</code></td><td><code>id</code>, any of <code>text</code>, <code>x</code>, <code>y</code></td><td>Modify a note; at least one field.</td></tr>
    <tr><td><code>remove_id</code></td><td><code>id</code></td><td>Delete any shape or note.</td></tr>
  </tbody>
</table>
<p>
  The four shape actions also require <code>line_color</code>
  (<code>#RRGGBB</code> or <code>#RRGGBBAA</code>) and <code>line_width</code>
  (positive pixels). All coordinates are non-negative finite image pixels.
</p>
<pre><code>curl -X POST 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add_rectangle","x":120,"y":80,"width":240,"height":160,
       "line_color":"#ff3b30","line_width":3}'

curl -X POST 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add_popup_note","x":300,"y":150,"text":"Inspect this region"}'</code></pre>
<p class="example-note">
  <code>add_ellipse</code> takes <code>cx</code>/<code>cy</code>/<code>rx</code>/<code>ry</code>,
  <code>add_line_segment</code> takes <code>x1</code>/<code>y1</code>/<code>x2</code>/<code>y2</code>,
  and <code>add_freehand</code> takes <code>points</code>; each takes the same
  <code>line_color</code> and <code>line_width</code>.
</p>

<h3>Errors</h3>
<p>
  <code>{"error": "message"}</code> with <code>400</code> for a bad action,
  colour, width, coordinate, or id, a file that is not a supported raster
  image, or an unreadable sidecar; <code>404</code> for a missing path;
  <code>501</code> for other methods.
</p>
</section>)TXTHTML";

} // namespace txthighlight::assets

#endif // API_DOCS_HPP
