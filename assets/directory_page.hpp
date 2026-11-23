#ifndef DIRECTORY_PAGE_HPP
#define DIRECTORY_PAGE_HPP

#include <string_view>

namespace txthighlight::assets {

// A std::format string: {0} is the listed URI path, {1} the served root
// directory (both HTML-escaped by the caller), {2} zero or more
// "<li>...</li>\n" rows that are already escaped, {3} the annotated-file
// summary, {4} the directory API reference, and {5} its CSS. Literal braces are
// doubled.
inline constexpr std::string_view directory_page =
    R"TXTHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Directory listing for {0}</title>
<style>
:root {{
  --radius: 0.625rem;
  --border: #d5d5dc;
  --surface: #f5f5f8;
  --accent: #0a6cff;
  --muted: #6b6b74;
  --row-pad: 0.625rem;
}}

*,
*::before,
*::after {{
  box-sizing: border-box;
}}

html {{
  -webkit-text-size-adjust: 100%;
  text-size-adjust: 100%;
}}

body {{
  max-width: 56rem;
  margin: 0 auto;
  padding: 1.75rem max(1rem, env(safe-area-inset-left)) 2.5rem
    max(1rem, env(safe-area-inset-right));
  font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  font-size: clamp(1rem, 0.95rem + 0.25vw, 1.0625rem);
  line-height: 1.6;
  color: #17171a;
  background: #fff;
}}

h1 {{
  margin: 0 0 0.25rem;
  font-size: clamp(1.2rem, 1rem + 1vw, 1.5rem);
  font-weight: 650;
  letter-spacing: -0.01em;
  overflow-wrap: anywhere;
}}

header .meta {{
  margin: 0;
  color: var(--muted);
  font-size: 0.875rem;
  overflow-wrap: anywhere;
}}

.legend {{
  display: flex;
  flex-wrap: wrap;
  gap: 0.375rem;
  margin-top: 0.75rem;
}}

hr {{
  border: 0;
  border-top: 1px solid var(--border);
  margin: 1.25rem 0;
}}

ul {{
  list-style: none;
  /* Cancel the row padding so names line up with the heading. */
  margin: 0 calc(-1 * var(--row-pad));
  padding: 0;
}}

/* One file per row, at least 44px tall, with the whole row as hit area. */
.entry {{
  position: relative;
  display: flex;
  align-items: center;
  gap: 0.75rem;
  min-height: 2.75rem;
  padding: 0.25rem var(--row-pad);
  border-radius: var(--radius);
  transition: background-color 0.15s ease;
}}

.entry:hover {{
  background: var(--surface);
}}

.name {{
  flex: 1 1 auto;
  min-width: 0;
  color: inherit;
  font-weight: 500;
  text-decoration: none;
  overflow-wrap: anywhere;
}}

.name::after {{
  content: "";
  position: absolute;
  inset: 0;
  z-index: 1;
  border-radius: inherit;
}}

.name:focus-visible {{
  outline: 2px solid var(--accent);
  outline-offset: 2px;
  border-radius: 0.25rem;
}}

.badge {{
  flex: 0 0 auto;
  padding: 0.125rem 0.5rem;
  border: 1px solid;
  border-radius: 999px;
  font-size: 0.75rem;
  font-weight: 500;
  line-height: 1.5;
  white-space: nowrap;
}}

.badge-dir {{
  background: #eef5ff;
  border-color: #b8d3ff;
  color: #14508f;
}}

.badge-text {{
  background: #eefbf0;
  border-color: #b8e0bd;
  color: #1a6b33;
}}

.badge-image {{
  background: #eef8ff;
  border-color: #b6dcf5;
  color: #14556e;
}}

.badge-annotated {{
  background: #fff7e6;
  border-color: #efcf8a;
  color: #7a4a00;
}}

.badge-sidecar {{
  background: #f7f0ff;
  border-color: #d7c2ff;
  color: #5b2fa6;
}}

.badge-file {{
  background: #f6f6f6;
  border-color: #ddd;
  color: #55555e;
}}

.download {{
  position: relative;
  z-index: 2;
  flex: 0 0 auto;
  padding: 0.125rem 0.5rem;
  border: 1px solid var(--border);
  border-radius: 999px;
  background: #fff;
  color: var(--accent);
  font-size: 0.75rem;
  font-weight: 500;
  line-height: 1.5;
  text-decoration: none;
  white-space: nowrap;
}}

.download:hover {{
  background: var(--surface);
}}

.download:focus-visible {{
  outline: 2px solid var(--accent);
  outline-offset: 2px;
}}

/* A direct answer to "which files here are annotated?" */
.annotation-summary {{
  margin: 0 0 1rem;
  padding: 0.625rem 0.75rem;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: var(--surface);
  font-size: 0.9rem;
}}

.annotation-summary p {{
  margin: 0;
}}

.annotation-summary ul {{
  margin: 0.375rem 0 0;
  padding-left: 1.25rem;
}}

.annotation-summary a {{
  color: var(--accent);
}}

.annotation-summary .count {{
  color: var(--muted);
}}

footer p {{
  margin: 0;
  color: var(--muted);
  font-size: 0.875rem;
}}

.api-docs-wrap {{
  margin-top: 1.25rem;
}}

{5}

@media (max-width: 40rem) {{
  :root {{
    --row-pad: 0.5rem;
  }}

  .entry {{
    min-height: 3rem;
    gap: 0.5rem;
    padding: 0.375rem var(--row-pad);
  }}

  .badge {{
    font-size: 0.6875rem;
  }}
}}
</style>
</head>
<body>
<header>
  <h1>Directory listing for {0}</h1>
  <p class="meta">Serving from {1}</p>
  <nav class="legend" aria-label="File kinds">
    <span class="badge badge-dir">directory</span>
    <span class="badge badge-text">UTF-8 text</span>
    <span class="badge badge-image">raster image</span>
    <span class="badge badge-annotated">annotated &middot; N</span>
    <span class="badge badge-sidecar">.json sidecar</span>
    <span class="badge badge-file">other file</span>
  </nav>
</header>
<main>
<section class="annotation-summary" aria-label="Annotated files">{3}</section>
<hr>
<ul>
{2}</ul>
<hr>
<footer>
<p>Open a file to annotate it, or use <strong>Download</strong> to save the
original. Open a directory to keep browsing. This page is also the entry point
for scripts and agents: the API documentation below covers browsing,
downloading, and opening annotation views.</p>
</footer>
<hr>
<section class="api-docs-wrap">
{4}
</section>
</main>
</body>
</html>)TXTHTML";

} // namespace txthighlight::assets

#endif // DIRECTORY_PAGE_HPP
