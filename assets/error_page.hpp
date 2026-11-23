#ifndef ERROR_PAGE_HPP
#define ERROR_PAGE_HPP

#include <string_view>

namespace txthighlight::assets {

// A std::format string; {0} is the already HTML-escaped title and {1} the
// already HTML-escaped message. Literal braces are doubled.
inline constexpr std::string_view error_page =
    R"TXTHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>txthighlight: {0}</title>
<style>
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
  display: flex;
  min-height: 100vh;
  margin: 0;
  padding: max(1rem, env(safe-area-inset-top)) max(1rem, env(safe-area-inset-right))
    max(1rem, env(safe-area-inset-bottom)) max(1rem, env(safe-area-inset-left));
  align-items: center;
  justify-content: center;
  font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  font-size: 1rem;
  line-height: 1.6;
  color: #17171a;
  background: #f5f5f8;
}}

main {{
  width: min(34rem, 100%);
  padding: clamp(1.25rem, 4vw, 2rem);
  border: 1px solid #d5d5dc;
  border-radius: 0.875rem;
  background: #fff;
  box-shadow: 0 1rem 3rem rgba(0, 0, 0, 0.08);
}}

h1 {{
  margin: 0 0 0.5rem;
  font-size: 1.25rem;
  font-weight: 650;
}}

p {{
  margin: 0;
  color: #6b6b74;
  overflow-wrap: anywhere;
}}
</style>
</head>
<body>
<main>
<h1>{0}</h1>
<p class="error">{1}</p>
</main>
</body>
</html>)TXTHTML";

} // namespace txthighlight::assets

#endif // ERROR_PAGE_HPP
