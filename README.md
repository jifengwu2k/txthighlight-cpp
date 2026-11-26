# `txthighlight`

A tiny local web app for annotating **UTF-8 text files** and **raster images**
in the browser. Source files are never modified: annotations live beside them in
typed JSON sidecars, or kept in memory under `--no-sidecar` (existing sidecars
are still read, never written).

- **Text:** select text to highlight it, attach or edit comments, remove
  highlights.
- **Images** (PNG, JPEG, WebP, GIF): rectangles, ellipses, line segments and
  freehand strokes with a chosen colour and width; popup notes; select, delete,
  modify, undo, redo, zoom and pan.
- **Files:** streamed downloads with HTTP Range support (resumable with
  `curl -C -`), and streamed multi-file uploads from any directory page. Large
  files never have to fit in memory on either side.

The server is one `main.cpp`, the pages are inlined from `assets/`, the C++
libraries are vendored header-only under `vendor/`, and image codecs come from
the system. `./build.sh` calls the C++20 compiler directly — no build system.

## Build

```bash
./build.sh              # optimised build -> ./txthighlight
./build.sh --install    # build, then install to $PREFIX/bin or ~/.local/bin
./build.sh --debug      # -O0 -g with ASan/UBSan
./build.sh --clean      # remove the binary
```

### Dependencies

- A **C++20** compiler: GCC 14+, Clang 17+, or Apple Clang 15+. Set `CXX` to
  override the default `c++`.
- Image codecs: **libpng**, **libjpeg**, **libwebp**, **giflib**.

```bash
# Debian / Ubuntu
sudo apt install libpng-dev libjpeg-dev libwebp-dev libgif-dev
# macOS
brew install libpng jpeg-turbo webp giflib
# Fedora / RHEL
sudo dnf install libpng-devel libjpeg-turbo-devel libwebp-devel giflib-devel
# Arch
sudo pacman -S libpng libjpeg-turbo libwebp giflib
# FreeBSD
sudo pkg install png jpeg-turbo webp giflib
# Termux
pkg install libpng libjpeg-turbo libwebp giflib
```

`./build.sh` is POSIX `sh` with unified flags on every Unix: `${CXX:-c++}
-std=c++20 -Wall -Wextra -Ivendor`, `-O2 -DNDEBUG` for release (or `-O0 -g
-fsanitize=address,undefined` for `--debug`), linking
`-pthread -lpng -ljpeg -lwebp -lgif`. No probing, no `-L` flags. UUIDs come
from `stduuid`'s pure-C++ generator, so no system UUID library is needed.

## Usage

```bash
txthighlight                              # serve the current directory on port 8000
txthighlight notes.txt                    # open a UTF-8 text file
txthighlight diagram.png                  # open a raster image
txthighlight --host 0.0.0.0 --port 8080 ./docs
txthighlight --no-sidecar ./notes         # read sidecars, keep changes in memory
```

| Option | Meaning |
|---|---|
| `PATH` | UTF-8 text file, raster image (`png`, `jpg`/`jpeg`, `webp`, `gif`), or directory to serve (default: `.`). |
| `--host HOST` | Interface to bind (default: `localhost`). |
| `--port PORT` | TCP port (default: `8000`). |
| `--no-sidecar` | Read existing `<file>.json` sidecars but never write them; new annotations last for the server's lifetime. |

Open the printed URL. A file path downloads the file; `/annotate?path=...`
opens its annotation UI; a directory renders a listing with Download links.

## Pages

Three HTML pages, each embedding **only** the API reference it needs:

| Page | Route | Documents |
|---|---|---|
| Directory listing | `/`, `/subdir/` | listing, download, annotation-view routes |
| Text annotation | `/annotate?path=%2Fnotes.txt` | `/api/annotations/text` |
| Image annotation | `/annotate?path=%2Fphoto.png` | `/api/annotations/image` |

The page kind is chosen from the file's **content**, never its name.

### Image annotation UI

The image is shown with a vector overlay in **top-left image pixels** (`+x`
right, `+y` down — the OpenCV, `torchvision`, COCO and SVG convention), so
zooming never moves an annotation.

| Control | Behaviour |
|---|---|
| **Select** | Tap a shape or note within 9 CSS px of its outline to select it; drag to pan. |
| **Rectangle**, **Ellipse**, **Line**, **Freehand** | Drag to draw. Colour and width apply to all four. |
| **Note** | Tap a position, type the text, save. |
| **Zoom** `−` `+` **Fit** | Also pinch with two fingers; `Ctrl`+wheel on desktop. |
| **Modify** / **Delete** | Modify edits a selected note's text or position; Delete removes any selected object. |
| **Undo** / **Redo** | Every edit is a `POST`; ids are re-mapped when the server issues new ones. |

Selection is a soft halo plus a dashed collar drawn *underneath* the outline, so
the object keeps its own colour. A finished shape is drawn immediately and then
persisted, and the tool switches back to **Select** — one drag, one shape.

## REST API

**Conventions.** The base URL is the page's origin. Paths are relative to the
served root and URL-encoded in the `path` parameter (`/notes.txt` →
`%2Fnotes.txt`); a missing `path` means `/`. Paths containing `..`, and symlinks
leading outside the root, are rejected.

**Errors.** `{"error": "message"}` with `400` (bad JSON, action, field, or file
kind; unreadable sidecar; malformed multipart upload or upload filename),
`404` (missing path), `416` (unsatisfiable `Range`), `500` (filesystem failure),
and `501` for methods other than `GET`/`HEAD`/`POST`.

**Responses.** Every annotation `GET`/`POST` returns the whole document as
`{"kind": "text"|"image", "annotations": [...]}`; ids are always generated by
the server and never change, except when a highlight is merged or split. An
entry written by hand without an `id` gets one on the first read, and that id is
saved before it is returned, so a later `update_comment` or `remove_id` can
always use it. Uploads answer `303 See Other` with a `Location` back to the
target directory.

### Directory listing

| Method | Endpoint | Purpose |
|---|---|---|
| `GET`, `HEAD` | `/`, `/subdir/` | HTML listing; rows link to `/annotate?path=...` and to a download. |
| `GET`, `HEAD` | `/notes.txt`, `/photo.png` | Download the original file, unchanged. Supports Range requests. |
| `POST` | `/subdir/` | Upload files as `multipart/form-data`; each file part is streamed to disk. |
| `GET` | `/annotate?path=%2Fnotes.txt` | Annotation UI for that file. |

### Resumable downloads

Downloads are streamed from disk with `Accept-Ranges: bytes`. A `Range`
request returns `206 Partial Content` and `Content-Range`; an unsatisfiable
range returns `416` with `Content-Range: bytes */<size>`.

```bash
curl -O 'http://127.0.0.1:8000/photo.png'                 # whole file
curl -O -C - 'http://127.0.0.1:8000/large.bin'            # resume
curl -H 'Range: bytes=0-1023' 'http://127.0.0.1:8000/large.bin'
curl -I 'http://127.0.0.1:8000/large.bin'                 # size and range support
```

### Uploads

`POST` to any directory with `multipart/form-data`. Filenames are reduced to a
single path component, so `filename="../../evil.txt"` cannot escape the
directory. Every part is written to a hidden temporary file as it arrives and
renamed into place when the part ends: a dropped upload never leaves a
truncated file behind, and concurrent listings hide partial uploads.

```bash
curl -X POST \
  -F 'file=@/path/to/photo.jpg' \
  -F 'file=@/path/to/document.pdf' \
  http://127.0.0.1:8000/subdir/
```

The directory page carries the same form with `<input type="file" multiple>`,
and redirects back to the refreshed listing after the upload.

### Text annotations — `/api/annotations/text?path=%2Fnotes.txt`

```bash
curl 'http://127.0.0.1:8000/api/annotations/text?path=%2Fnotes.txt'
```

```json
{
  "kind": "text",
  "annotations": [
    { "id": "3d7278a2-…", "start": 0, "end": 10, "comment": "Keep this",
      "created_at": "2026-11-23T13:00:00Z", "updated_at": "2026-11-23T13:00:00Z" }
  ]
}
```

`POST` takes `{"action": …}`:

| `action` | Fields | Effect |
|---|---|---|
| `add` | `start`, `end`, optional `comment` | Add a highlight. Overlapping and touching highlights merge; comments join with blank lines. |
| `remove_range` | `start`, `end` | Cut every highlight at this range; surviving pieces get new ids. |
| `update_comment` | `id`, `comment` | Replace one comment. |
| `remove_id` | `id` | Delete one highlight. |

```bash
curl -X POST 'http://127.0.0.1:8000/api/annotations/text?path=%2Fnotes.txt' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add","start":0,"end":10,"comment":"Keep this"}'
```

`start` and `end` are half-open **character (codepoint) offsets** into the UTF-8
text — not bytes, not UTF-16 units.

### Image annotations — `/api/annotations/image?path=%2Fphoto.png`

```bash
curl 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png'
```

```json
{
  "kind": "image",
  "annotations": [
    { "id": "a1b2c3d4-…", "type": "rectangle", "x": 120, "y": 80, "width": 240,
      "height": 160, "line_color": "#ff3b30", "line_width": 3,
      "created_at": "2026-11-23T13:00:00Z", "updated_at": "2026-11-23T13:00:00Z" },
    { "id": "e5f6a7b8-…", "type": "popup_note", "x": 300, "y": 150,
      "text": "Inspect this region",
      "created_at": "2026-11-23T13:01:00Z", "updated_at": "2026-11-23T13:01:00Z" }
  ]
}
```

`POST` takes `{"action": …}`; entries are drawn in array order (later on top):

| `action` | Fields | Effect |
|---|---|---|
| `add_rectangle` | `x`, `y`, `width`, `height` | Rectangle; sizes must be positive. |
| `add_ellipse` | `cx`, `cy`, `rx`, `ry` | Ellipse; both radii positive. |
| `add_line_segment` | `x1`, `y1`, `x2`, `y2` | Segment; endpoints must differ. |
| `add_freehand` | `points` | At least two `{"x", "y"}` objects. |
| `add_popup_note` | `x`, `y`, `text` | Note anchored at the point. |
| `update_popup_note` | `id`, any of `text`, `x`, `y` | Modify a note; at least one field. |
| `remove_id` | `id` | Delete any shape or note. |

The four shape actions also require `line_color` (`#RRGGBB` or `#RRGGBBAA`) and
`line_width` (positive image pixels). All coordinates are non-negative finite
image pixels.

```bash
curl -X POST 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add_rectangle","x":120,"y":80,"width":240,"height":160,
       "line_color":"#ff3b30","line_width":3}'

curl -X POST 'http://127.0.0.1:8000/api/annotations/image?path=%2Fphoto.png' \
  -H 'Content-Type: application/json' \
  -d '{"action":"add_popup_note","x":300,"y":150,"text":"Inspect this region"}'
```

## Data format

Each source file gets a sibling `<source>.json` holding exactly the JSON above.

- Written atomically (`<source>.json.tmp` then `rename`).
- Bound to the file name, so moving a directory keeps the pairs together.
- A sidecar of the wrong `kind`, or with a malformed schema, fails with `400`.
- Hand-editable: an entry may leave `id` out, and the server adds one on the
  first read and saves it, so ids stay stable from then on.
- `--no-sidecar` still reads these files, but never writes them: an existing
  sidecar seeds the in-memory state, and later changes last for the run only.

### In-memory representation

The C++ types mirror the JSON field-for-field, so payloads, sidecars and the
`--no-sidecar` memory state share one canonical shape. Shapes are distinct types
held in a `std::variant` — no enum-plus-switch:

```cpp
using ImageAnnotation = std::variant<
    RectangleAnnotation, EllipseAnnotation, LineSegmentAnnotation,
    FreehandAnnotation, PopupNoteAnnotation>;

struct TextAnnotationDocument {
    std::vector<TextAnnotation> annotations;
};  // serialises as {"kind": "text", "annotations": [...]}

struct ImageAnnotationDocument {
    std::vector<ImageAnnotation> annotations;
};  // serialises as {"kind": "image", "annotations": [...]}

using AnnotationDocument = std::variant<TextAnnotationDocument, ImageAnnotationDocument>;
```

| Type | Fields besides `id`, `created_at`, `updated_at` |
|---|---|
| `TextAnnotation` | `start`, `end`, `comment` |
| `RectangleAnnotation` | `x`, `y`, `width`, `height`, `line_color`, `line_width` |
| `EllipseAnnotation` | `cx`, `cy`, `rx`, `ry`, `line_color`, `line_width` |
| `LineSegmentAnnotation` | `x1`, `y1`, `x2`, `y2`, `line_color`, `line_width` |
| `FreehandAnnotation` | `points`, `line_color`, `line_width` |
| `PopupNoteAnnotation` | `x`, `y`, `text` |

With `--no-sidecar`, `AppState` keeps a mutex-protected
`std::map<std::string, AnnotationDocument>` for new annotations. An existing
sidecar is loaded on first access; once a path has an in-memory document, that
document takes precedence over the sidecar. Nothing is written back.

## Third-party packages

Vendored header-only C++, licences kept verbatim in `vendor/licenses/`:

| Package | Version | Licence | Used for |
|---|---|---|---|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.67.0 | MIT | HTTP/1.1 server, routing, thread pool, byte ranges, streaming multipart |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | JSON (`ordered_json`, field order preserved) |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.7.2 | BSD-3-Clause | Command line and `--help` |
| [stduuid](https://github.com/mariusbancila/stduuid) | 1.2.3 | MIT | UUID v4, pure C++ |
| [utfcpp](https://github.com/nemtrif/utfcpp) | 5.0.0 | Boost 1.0 | UTF-8 validation and codepoint offsets |

System image libraries:

| Library | Licence | Used for | `apt` | `brew` |
|---|---|---|---|---|
| [libpng](http://www.libpng.org/pub/png/libpng.html) | libpng (zlib-like) | PNG validation | `libpng-dev` | `libpng` |
| [libjpeg-turbo](https://libjpeg-turbo.org/) | IJG / BSD-3-Clause / zlib | JPEG validation | `libjpeg-dev` | `jpeg-turbo` |
| [libwebp](https://developers.google.com/speed/webp) | BSD-3-Clause | WebP validation | `libwebp-dev` | `webp` |
| [giflib](https://giflib.sourceforge.net/) | MIT | GIF validation | `libgif-dev` | `giflib` |

All are permissively licensed and MIT-compatible. Deliberately not dependencies:
URL encoding, HTML templating, logging, timestamps, filesystem and threading —
the standard library covers them.

## Layout

```
main.cpp                     server and domain logic (one translation unit)
assets/api_docs.hpp          page-scoped API reference sections and their CSS
assets/directory_page.hpp    directory listing page
assets/text_page.hpp         UTF-8 text annotation page
assets/image_page.hpp        raster image annotation page
assets/error_page.hpp        "cannot annotate this file" page
build.sh                     POSIX sh build script
vendor/                      pinned header-only C++ packages
vendor/licenses/             their licence texts
```

`txthighlight` is the build output and is not committed.

## Licence

MIT, see [LICENSE](LICENSE). Vendored licences are in `vendor/licenses/`.
