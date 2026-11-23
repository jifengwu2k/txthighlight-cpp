// txthighlight - a tiny local web app for annotating UTF-8 text files and
// raster images in the browser.
//
// This file is the whole application. Inlined markup lives in assets/ and every
// third-party C++ package is header-only and vendored under vendor/ (see
// README.md). Raster formats are validated with the system libpng, libjpeg,
// libwebp, and giflib.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <gif_lib.h>
#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

#include "CLI11.hpp"
#include "assets/api_docs.hpp"
#include "assets/directory_page.hpp"
#include "assets/error_page.hpp"
#include "assets/image_page.hpp"
#include "assets/text_page.hpp"
#include "httplib.h"
#include "json.hpp"
#include "utf8cpp/utf8.h"
#include "uuid.h"

namespace fs = std::filesystem;
namespace assets = txthighlight::assets;
using json = nlohmann::ordered_json;

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr int http_ok = 200;
constexpr int http_bad_request = 400;
constexpr int http_not_found = 404;
constexpr int http_internal_server_error = 500;

constexpr const char *server_version = "TextHighlighter/1.2";

constexpr std::string_view text_annotations_route = "/api/annotations/text";
constexpr std::string_view image_annotations_route = "/api/annotations/image";

// ---------------------------------------------------------------------------
// Errors
//
// One hierarchy, and each error already knows the status code it maps onto, so
// a request handler needs a single catch clause rather than a list of types.
// ---------------------------------------------------------------------------

class HttpError : public std::runtime_error {
  public:
    HttpError(int status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    [[nodiscard]] int status() const noexcept { return status_; }

  private:
    int status_;
};

struct RequestNotFoundError : HttpError {
    explicit RequestNotFoundError(std::string message)
        : HttpError(http_not_found, std::move(message)) {}
};

struct BadRequestError : HttpError {
    explicit BadRequestError(std::string message)
        : HttpError(http_bad_request, std::move(message)) {}
};

// A file that exists but is neither UTF-8 text nor a supported raster image.
struct UnsupportedFileError : BadRequestError {
    using BadRequestError::BadRequestError;
};

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::string html_escape(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const char character : input) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#x27;"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::optional<unsigned> hex_value(char character) {
    if (character >= '0' && character <= '9') return static_cast<unsigned>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<unsigned>(character - 'a') + 10U;
    if (character >= 'A' && character <= 'F') return static_cast<unsigned>(character - 'A') + 10U;
    return std::nullopt;
}

// "%XX" escapes are decoded, malformed escapes are left alone, "+" means
// space only in query strings, and invalid UTF-8 becomes U+FFFD.
std::string percent_decode(std::string_view input, bool plus_is_space = false) {
    std::string bytes;
    bytes.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%' && index + 2 < input.size()) {
            const auto high = hex_value(input[index + 1]);
            const auto low = index + 2 < input.size() ? hex_value(input[index + 2]) : std::nullopt;
            if (high && low) {
                bytes.push_back(static_cast<char>((*high << 4U) | *low));
                index += 2;
                continue;
            }
        }
        bytes.push_back(plus_is_space && input[index] == '+' ? ' ' : input[index]);
    }
    return utf8::unchecked::replace_invalid(std::move(bytes));
}

// Percent-encode every byte except the RFC 3986 unreserved characters.
std::string percent_encode(std::string_view value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    constexpr auto is_unreserved = [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-' ||
               character == '.' || character == '~';
    };
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        if (is_unreserved(character)) {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(digits[character >> 4U]);
            encoded.push_back(digits[character & 0x0FU]);
        }
    }
    return encoded;
}

std::vector<std::string> split(std::string_view text, char separator) {
    const std::string_view delimiter(&separator, 1);
    std::vector<std::string> parts;
    for (auto part : std::views::split(text, delimiter)) {
        parts.emplace_back(part.begin(), static_cast<std::size_t>(std::ranges::distance(part)));
    }
    return parts;
}

// Splits "/a%20b/../c?x=1" into the raw (still encoded) path and the query.
struct RequestTarget {
    std::string path;
    std::string query;
};

RequestTarget split_request_target(std::string_view target) {
    const auto question_mark = target.find('?');
    if (question_mark == std::string_view::npos) return {std::string(target), {}};
    return {std::string(target.substr(0, question_mark)),
            std::string(target.substr(question_mark + 1))};
}

// Percent-decodes each segment separately, so an encoded slash inside a file
// name stays inside that segment. Empty segments are dropped.
std::vector<std::string> uri_path_components(std::string_view raw_path) {
    std::vector<std::string> components;
    for (auto &segment : split(raw_path, '/')) {
        if (segment.empty()) continue;
        components.push_back(percent_decode(segment));
    }
    return components;
}

std::string uri_path_from_components(const std::vector<std::string> &components,
                                     bool force_directory = false) {
    if (components.empty()) return "/";
    std::string path;
    for (const auto &component : components) {
        path.push_back('/');
        path += percent_encode(component);
    }
    if (force_directory && path.back() != '/') path.push_back('/');
    return path;
}

// A blank query value means "no path", so "?path=" behaves like no path.
std::optional<std::string> first_query_value(std::string_view raw_query,
                                             std::string_view name) {
    for (const auto &pair : split(raw_query, '&')) {
        const auto equals = pair.find('=');
        if (equals == std::string::npos) continue;
        if (percent_decode(std::string_view(pair).substr(0, equals), true) != name) continue;
        auto value = percent_decode(std::string_view(pair).substr(equals + 1), true);
        if (!value.empty()) return value;
    }
    return std::nullopt;
}

std::string_view strip(std::string_view text) {
    constexpr auto is_space = [](char character) {
        return character == ' ' || character == '\t' || character == '\n' ||
               character == '\r' || character == '\f' || character == '\v';
    };
    const auto first = std::ranges::find_if_not(text, is_space);
    const auto last = std::ranges::find_if_not(text | std::views::reverse, is_space).base();
    return first < last ? std::string_view(first, last) : std::string_view{};
}

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

// Mirrors os.path.realpath: resolves symlinks, tolerates non-existent tails.
std::optional<fs::path> resolved_path(const fs::path &candidate) {
    std::error_code error;
    auto resolved = fs::weakly_canonical(candidate, error);
    if (error) return std::nullopt;
    return resolved;
}

bool is_within(const fs::path &root, const fs::path &candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty()) return candidate == root;
    return *relative.begin() != "..";
}

std::string ascii_lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x80 ? static_cast<char>(std::tolower(byte)) : character;
    });
    return text;
}

// A wall-clock instant, stored as an ISO-8601 UTC string
// ("2026-11-18T22:00:00Z") so sidecars stay readable and timezone-explicit.
struct Timestamp {
    std::chrono::sys_seconds value{};

    [[nodiscard]] static Timestamp now() {
        return {std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())};
    }

    auto operator<=>(const Timestamp &) const = default;
};

void to_json(json &output, const Timestamp &timestamp) {
    output = std::format("{:%Y-%m-%dT%H:%M:%SZ}", timestamp.value);
}

void from_json(const json &input, Timestamp &timestamp) {
    // Parsed by hand because std::chrono::parse is missing from some standard
    // libraries still in use (notably the libc++ shipped by Termux).
    const auto text = input.get<std::string>();
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const bool parsed = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month,
                                    &day, &hour, &minute, &second) == 6;
    const std::chrono::year_month_day date{
        std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (!parsed || !date.ok() || hour > 23 || minute > 59 || second > 59) {
        throw BadRequestError("Timestamp must look like 2026-11-18T22:00:00Z: " + input.dump());
    }
    timestamp.value = std::chrono::sys_days{date} + std::chrono::hours{hour} +
                      std::chrono::minutes{minute} + std::chrono::seconds{second};
}

// Offsets are *character* (codepoint) offsets, as documented in the README.
std::int64_t codepoint_length(const std::string &text) {
    return static_cast<std::int64_t>(utf8::unchecked::distance(text.begin(), text.end()));
}

// A per-process random engine keeps UUID v4 generation pure C++, so no system
// UUID library is needed on any platform.
std::string new_uuid() {
    static std::mutex uuid_mutex;
    static std::mt19937 engine{std::random_device{}()};
    std::scoped_lock lock(uuid_mutex);
    uuids::uuid_random_generator generator(engine);
    return uuids::to_string(generator());
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

std::string read_file(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read file: " + path.string());
    std::string content;
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (!error) content.reserve(static_cast<std::size_t>(size));
    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("Cannot read file: " + path.string());
    return content;
}

void write_file(const fs::path &path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write file: " + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("Cannot write file: " + path.string());
}

// Replaces a file in one step, so a crash or a full disk can never leave a
// truncated file behind.
void write_atomically(const fs::path &path, std::string_view content) {
    auto temporary = path;
    temporary += ".tmp";
    write_file(temporary, content);

    std::error_code error;
    if (fs::exists(path, error)) {
        fs::permissions(temporary, fs::status(path, error).permissions(), error);
    }
    fs::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw std::runtime_error("Cannot write file: " + path.string());
    }
}

// ---------------------------------------------------------------------------
// Raster image inspection
//
// Formats are detected from the file's content, never its name, and every file
// is decoded once so a corrupt image is rejected instead of rendered blank.
// ---------------------------------------------------------------------------

struct RasterFormat {
    enum class Kind { png, jpeg, webp, gif };

    Kind kind;

    [[nodiscard]] std::string_view name() const {
        switch (kind) {
        case Kind::png: return "png";
        case Kind::jpeg: return "jpeg";
        case Kind::webp: return "webp";
        case Kind::gif: return "gif";
        }
        return "unknown";
    }
};

std::optional<RasterFormat> sniff_raster_format(std::string_view bytes) {
    static constexpr std::array<char, 8> png_signature{'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    const auto starts_with_bytes = [bytes](std::string_view prefix) {
        return bytes.size() >= prefix.size() && bytes.starts_with(prefix);
    };

    if (bytes.size() >= png_signature.size() &&
        std::equal(png_signature.begin(), png_signature.end(), bytes.begin())) {
        return RasterFormat{RasterFormat::Kind::png};
    }
    if (starts_with_bytes(std::string_view("\xFF\xD8\xFF", 3))) {
        return RasterFormat{RasterFormat::Kind::jpeg};
    }
    if (starts_with_bytes("GIF87a") || starts_with_bytes("GIF89a")) {
        return RasterFormat{RasterFormat::Kind::gif};
    }
    if (starts_with_bytes("RIFF") && bytes.size() >= 12 && bytes.substr(8, 4) == "WEBP") {
        return RasterFormat{RasterFormat::Kind::webp};
    }
    return std::nullopt;
}

// --- libpng ---------------------------------------------------------------

struct PngMemoryReader {
    const std::uint8_t *data;
    std::size_t size;
    std::size_t offset;
};

void png_read_from_memory(png_structp png, png_bytep output, png_size_t count) {
    auto *reader = static_cast<PngMemoryReader *>(png_get_io_ptr(png));
    const auto remaining = reader->size - reader->offset;
    const auto take = std::min<std::size_t>(count, remaining);
    std::memcpy(output, reader->data + reader->offset, take);
    reader->offset += take;
    if (take < count) png_error(png, "unexpected end of PNG data");
}

[[noreturn]] void png_fail(png_structp png, png_const_charp) { png_longjmp(png, 1); }

void png_ignore_warning(png_structp, png_const_charp) {}

bool png_is_decodable(std::string_view bytes) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_fail,
                                            png_ignore_warning);
    if (png == nullptr) return false;
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }

    PngMemoryReader reader{reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size(), 0};
    // libpng reports errors with longjmp, so anything it may have touched must
    // be either heap-allocated or volatile to stay valid in the error path.
    png_byte *volatile row_buffer = nullptr;
    volatile bool decodable = false;

    if (setjmp(png_jmpbuf(png)) == 0) {
        png_set_read_fn(png, &reader, png_read_from_memory);
        png_read_info(png, info);
        png_uint_32 width = 0;
        png_uint_32 height = 0;
        int depth = 0;
        int color_type = 0;
        png_get_IHDR(png, info, &width, &height, &depth, &color_type, nullptr, nullptr, nullptr);
        if (width > 0 && height > 0) {
            const int passes = png_set_interlace_handling(png);
            png_read_update_info(png, info);
            row_buffer = static_cast<png_byte *>(std::calloc(png_get_rowbytes(png, info), 1));
            if (row_buffer != nullptr) {
                for (int pass = 0; pass < passes; ++pass) {
                    for (png_uint_32 row = 0; row < height; ++row) {
                        png_read_row(png, row_buffer, nullptr);
                    }
                }
                png_read_end(png, nullptr);
                decodable = true;
            }
        }
    }

    std::free(const_cast<png_byte *>(row_buffer));
    png_destroy_read_struct(&png, &info, nullptr);
    return decodable != 0;
}

// --- libjpeg --------------------------------------------------------------

struct JpegErrorManager {
    jpeg_error_mgr base; // must stay first: libjpeg casts back to it
    std::jmp_buf jump;
};

void jpeg_fail(j_common_ptr info) {
    auto *manager = reinterpret_cast<JpegErrorManager *>(info->err);
    std::longjmp(manager->jump, 1);
}

void jpeg_ignore_message(j_common_ptr, int) {}

bool jpeg_is_decodable(std::string_view bytes) {
    // Allocated on the heap because libjpeg longjmps out of its own frames.
    auto *info = static_cast<jpeg_decompress_struct *>(
        std::calloc(1, sizeof(jpeg_decompress_struct)));
    auto *manager = static_cast<JpegErrorManager *>(std::calloc(1, sizeof(JpegErrorManager)));
    if (info == nullptr || manager == nullptr) {
        std::free(info);
        std::free(manager);
        return false;
    }

    info->err = jpeg_std_error(&manager->base);
    manager->base.error_exit = jpeg_fail;
    manager->base.emit_message = jpeg_ignore_message;

    unsigned char *volatile row_buffer = nullptr;
    volatile bool decodable = false;
    volatile bool created = false;

    if (setjmp(manager->jump) == 0) {
        jpeg_create_decompress(info);
        created = true;
        jpeg_mem_src(info, reinterpret_cast<const unsigned char *>(bytes.data()),
                     static_cast<unsigned long>(bytes.size()));
        jpeg_read_header(info, TRUE);
        if (info->image_width > 0 && info->image_height > 0) {
            jpeg_start_decompress(info);
            const auto stride = static_cast<std::size_t>(info->output_width) *
                                static_cast<std::size_t>(info->output_components);
            row_buffer = static_cast<unsigned char *>(std::calloc(stride, 1));
            if (row_buffer != nullptr) {
                while (info->output_scanline < info->output_height) {
                    JSAMPROW rows[1] = {const_cast<unsigned char *>(row_buffer)};
                    if (jpeg_read_scanlines(info, rows, 1) != 1) break;
                }
                jpeg_finish_decompress(info);
                decodable = true;
            }
        }
    }

    if (created != 0) jpeg_destroy_decompress(info);
    std::free(const_cast<unsigned char *>(row_buffer));
    std::free(info);
    std::free(manager);
    return decodable != 0;
}

// --- libwebp --------------------------------------------------------------

bool webp_is_decodable(std::string_view bytes) {
    int width = 0;
    int height = 0;
    return WebPGetInfo(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size(), &width,
                       &height) != 0 &&
           width > 0 && height > 0;
}

// --- giflib ---------------------------------------------------------------

struct GifMemoryReader {
    const std::uint8_t *data;
    std::size_t size;
    std::size_t offset;
};

int gif_read_from_memory(GifFileType *gif, GifByteType *output, int count) {
    // giflib passes the user pointer back unchanged, so the reader survives
    // being stored in the GifFileType.
    auto *reader = static_cast<GifMemoryReader *>(gif->UserData);
    const auto remaining = reader->size - reader->offset;
    const auto take = std::min<std::size_t>(static_cast<std::size_t>(count), remaining);
    std::memcpy(output, reader->data + reader->offset, take);
    reader->offset += take;
    return static_cast<int>(take);
}

bool gif_is_decodable(std::string_view bytes) {
    GifMemoryReader reader{reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size(), 0};
    int error = 0;
    GifFileType *gif = DGifOpen(&reader, gif_read_from_memory, &error);
    if (gif == nullptr) return false;
    const bool decodable = DGifSlurp(gif) == GIF_OK && gif->ImageCount > 0 && gif->SWidth > 0 &&
                           gif->SHeight > 0;
    DGifCloseFile(gif, &error);
    return decodable;
}

bool raster_is_decodable(const RasterFormat &format, std::string_view bytes) {
    switch (format.kind) {
    case RasterFormat::Kind::png: return png_is_decodable(bytes);
    case RasterFormat::Kind::jpeg: return jpeg_is_decodable(bytes);
    case RasterFormat::Kind::webp: return webp_is_decodable(bytes);
    case RasterFormat::Kind::gif: return gif_is_decodable(bytes);
    }
    return false;
}

std::string_view supported_raster_formats() { return "PNG, JPEG, WebP, GIF"; }

// ---------------------------------------------------------------------------
// Documents
//
// A file is either UTF-8 text or a raster image, decided by its content. The
// loaded document carries whatever later work needs and nothing else.
// ---------------------------------------------------------------------------

struct TextDocument {
    std::string bytes;
};

struct RasterImageDocument {
    RasterFormat format;
};

using LoadedDocument = std::variant<TextDocument, RasterImageDocument>;

LoadedDocument load_document(const fs::path &path) {
    auto bytes = read_file(path);
    if (const auto format = sniff_raster_format(bytes)) {
        if (!raster_is_decodable(*format, bytes)) {
            throw UnsupportedFileError(std::format("Cannot decode {} image: {}", format->name(),
                                                   path.string()));
        }
        return RasterImageDocument{*format};
    }
    if (!utf8::is_valid(bytes.begin(), bytes.end())) {
        throw UnsupportedFileError(std::format("Only UTF-8 text files and {} images are supported: {}",
                                             supported_raster_formats(), path.string()));
    }
    return TextDocument{std::move(bytes)};
}

// nlohmann prefixes its messages with "[json.exception.type_error.302] ".
std::string_view json_message(const json::exception &error) {
    const std::string_view message = error.what();
    const auto close = message.find("] ");
    return message.starts_with('[') && close != std::string_view::npos
               ? message.substr(close + 2)
               : message;
}

// ---------------------------------------------------------------------------
// Text annotations
// ---------------------------------------------------------------------------

struct TextAnnotation {
    std::string id;
    std::int64_t start;
    std::int64_t end;
    std::string comment;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const TextAnnotation &annotation) {
    output = json{{"id", annotation.id},         {"start", annotation.start},
                  {"end", annotation.end},       {"comment", annotation.comment},
                  {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, TextAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.start = input.at("start").get<std::int64_t>();
    annotation.end = input.at("end").get<std::int64_t>();
    annotation.comment = input.value("comment", std::string{});
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

// Document order: by position, then by id as a stable tiebreak. A projection
// keeps TextAnnotation a plain data struct and still lets std::ranges::sort work.
auto annotation_position(const TextAnnotation &annotation) {
    return std::tie(annotation.start, annotation.end, annotation.id);
}

// ---------------------------------------------------------------------------
// Image annotations
//
// Each shape is its own type with the fields its JSON form has, so a value of
// the wrong kind cannot carry fields it should not have.
// ---------------------------------------------------------------------------

struct Point {
    double x;
    double y;
};

void to_json(json &output, const Point &point) {
    output = json{{"x", point.x}, {"y", point.y}};
}

void from_json(const json &input, Point &point) {
    point.x = input.at("x").get<double>();
    point.y = input.at("y").get<double>();
}

struct RectangleAnnotation {
    static constexpr std::string_view type_name = "rectangle";

    std::string id;
    double x;
    double y;
    double width;
    double height;
    std::string line_color;
    double line_width;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const RectangleAnnotation &annotation) {
    output = json{{"id", annotation.id},
                  {"type", RectangleAnnotation::type_name},
                  {"x", annotation.x},
                  {"y", annotation.y},
                  {"width", annotation.width},
                  {"height", annotation.height},
                  {"line_color", annotation.line_color},
                  {"line_width", annotation.line_width},
                  {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, RectangleAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.x = input.at("x").get<double>();
    annotation.y = input.at("y").get<double>();
    annotation.width = input.at("width").get<double>();
    annotation.height = input.at("height").get<double>();
    annotation.line_color = input.at("line_color").get<std::string>();
    annotation.line_width = input.at("line_width").get<double>();
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

struct EllipseAnnotation {
    static constexpr std::string_view type_name = "ellipse";

    std::string id;
    double cx;
    double cy;
    double rx;
    double ry;
    std::string line_color;
    double line_width;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const EllipseAnnotation &annotation) {
    output = json{{"id", annotation.id},
                  {"type", EllipseAnnotation::type_name},
                  {"cx", annotation.cx},
                  {"cy", annotation.cy},
                  {"rx", annotation.rx},
                  {"ry", annotation.ry},
                  {"line_color", annotation.line_color},
                  {"line_width", annotation.line_width},
                  {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, EllipseAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.cx = input.at("cx").get<double>();
    annotation.cy = input.at("cy").get<double>();
    annotation.rx = input.at("rx").get<double>();
    annotation.ry = input.at("ry").get<double>();
    annotation.line_color = input.at("line_color").get<std::string>();
    annotation.line_width = input.at("line_width").get<double>();
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

struct LineSegmentAnnotation {
    static constexpr std::string_view type_name = "line_segment";

    std::string id;
    double x1;
    double y1;
    double x2;
    double y2;
    std::string line_color;
    double line_width;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const LineSegmentAnnotation &annotation) {
    output = json{{"id", annotation.id},
                  {"type", LineSegmentAnnotation::type_name},
                  {"x1", annotation.x1},
                  {"y1", annotation.y1},
                  {"x2", annotation.x2},
                  {"y2", annotation.y2},
                  {"line_color", annotation.line_color},
                  {"line_width", annotation.line_width},
                  {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, LineSegmentAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.x1 = input.at("x1").get<double>();
    annotation.y1 = input.at("y1").get<double>();
    annotation.x2 = input.at("x2").get<double>();
    annotation.y2 = input.at("y2").get<double>();
    annotation.line_color = input.at("line_color").get<std::string>();
    annotation.line_width = input.at("line_width").get<double>();
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

struct FreehandAnnotation {
    static constexpr std::string_view type_name = "freehand";

    std::string id;
    std::vector<Point> points;
    std::string line_color;
    double line_width;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const FreehandAnnotation &annotation) {
    output = json{{"id", annotation.id},
                  {"type", FreehandAnnotation::type_name},
                  {"points", annotation.points},
                  {"line_color", annotation.line_color},
                  {"line_width", annotation.line_width},
                  {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, FreehandAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.points = input.at("points").get<std::vector<Point>>();
    annotation.line_color = input.at("line_color").get<std::string>();
    annotation.line_width = input.at("line_width").get<double>();
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

struct PopupNoteAnnotation {
    static constexpr std::string_view type_name = "popup_note";

    std::string id;
    double x;
    double y;
    std::string text;
    Timestamp created_at;
    Timestamp updated_at;
};

void to_json(json &output, const PopupNoteAnnotation &annotation) {
    output = json{{"id", annotation.id},       {"type", PopupNoteAnnotation::type_name},
                  {"x", annotation.x},         {"y", annotation.y},
                  {"text", annotation.text},   {"created_at", annotation.created_at},
                  {"updated_at", annotation.updated_at}};
}

void from_json(const json &input, PopupNoteAnnotation &annotation) {
    annotation.id = input.value("id", std::string{});
    annotation.x = input.at("x").get<double>();
    annotation.y = input.at("y").get<double>();
    annotation.text = input.value("text", std::string{});
    annotation.created_at = input.at("created_at").get<Timestamp>();
    annotation.updated_at = input.at("updated_at").get<Timestamp>();
}

using ImageAnnotation =
    std::variant<RectangleAnnotation, EllipseAnnotation, LineSegmentAnnotation,
                 FreehandAnnotation, PopupNoteAnnotation>;

void to_json(json &output, const ImageAnnotation &annotation) {
    std::visit([&output](const auto &value) { to_json(output, value); }, annotation);
}

void from_json(const json &input, ImageAnnotation &annotation) {
    const auto type = input.at("type").get<std::string>();
    if (type == RectangleAnnotation::type_name) {
        annotation = input.get<RectangleAnnotation>();
    } else if (type == EllipseAnnotation::type_name) {
        annotation = input.get<EllipseAnnotation>();
    } else if (type == LineSegmentAnnotation::type_name) {
        annotation = input.get<LineSegmentAnnotation>();
    } else if (type == FreehandAnnotation::type_name) {
        annotation = input.get<FreehandAnnotation>();
    } else if (type == PopupNoteAnnotation::type_name) {
        annotation = input.get<PopupNoteAnnotation>();
    } else {
        throw BadRequestError("Unknown image annotation type: " + type);
    }
}

const std::string &annotation_id(const ImageAnnotation &annotation) {
    return std::visit([](const auto &value) -> const std::string & { return value.id; }, annotation);
}

// ---------------------------------------------------------------------------
// Annotation documents
//
// The in-memory types serialize to exactly the JSON the API and the sidecar
// files use, so there is one canonical shape everywhere.
// ---------------------------------------------------------------------------

struct TextAnnotationDocument {
    static constexpr std::string_view kind = "text";

    std::vector<TextAnnotation> annotations;
};

void to_json(json &output, const TextAnnotationDocument &document) {
    output = json{{"kind", TextAnnotationDocument::kind}, {"annotations", document.annotations}};
}

void from_json(const json &input, TextAnnotationDocument &document) {
    if (input.at("kind").get<std::string>() != TextAnnotationDocument::kind) {
        throw BadRequestError("Expected a text annotation document");
    }
    document.annotations = input.at("annotations").get<std::vector<TextAnnotation>>();
}

struct ImageAnnotationDocument {
    static constexpr std::string_view kind = "image";

    std::vector<ImageAnnotation> annotations;
};

void to_json(json &output, const ImageAnnotationDocument &document) {
    output = json{{"kind", ImageAnnotationDocument::kind}, {"annotations", document.annotations}};
}

void from_json(const json &input, ImageAnnotationDocument &document) {
    if (input.at("kind").get<std::string>() != ImageAnnotationDocument::kind) {
        throw BadRequestError("Expected an image annotation document");
    }
    document.annotations = input.at("annotations").get<std::vector<ImageAnnotation>>();
}

using AnnotationDocument = std::variant<TextAnnotationDocument, ImageAnnotationDocument>;

void to_json(json &output, const AnnotationDocument &document) {
    std::visit([&output](const auto &value) { to_json(output, value); }, document);
}

std::int64_t document_size(const AnnotationDocument &document) {
    return std::visit(
        [](const auto &value) { return static_cast<std::int64_t>(value.annotations.size()); },
        document);
}

// ---------------------------------------------------------------------------
// Field validation
// ---------------------------------------------------------------------------

bool is_hex_color(std::string_view color) {
    if (color.size() != 7 && color.size() != 9) return false;
    if (color.front() != '#') return false;
    return std::ranges::all_of(color.substr(1), [](char character) {
        return std::isxdigit(static_cast<unsigned char>(character)) != 0;
    });
}

void require_non_negative(double value, std::string_view field) {
    if (!std::isfinite(value) || value < 0.0) {
        throw BadRequestError(std::format("{} must be a non-negative finite number", field));
    }
}

void require_strictly_positive(double value, std::string_view field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw BadRequestError(std::format("{} must be a positive finite number", field));
    }
}

void require_line_style(std::string_view color, double width) {
    if (!is_hex_color(color)) {
        throw BadRequestError("line_color must be #RRGGBB or #RRGGBBAA: " + std::string(color));
    }
    require_strictly_positive(width, "line_width");
}

void validate_annotation(const RectangleAnnotation &annotation) {
    require_non_negative(annotation.x, "x");
    require_non_negative(annotation.y, "y");
    require_strictly_positive(annotation.width, "width");
    require_strictly_positive(annotation.height, "height");
    require_line_style(annotation.line_color, annotation.line_width);
}

void validate_annotation(const EllipseAnnotation &annotation) {
    require_non_negative(annotation.cx, "cx");
    require_non_negative(annotation.cy, "cy");
    require_strictly_positive(annotation.rx, "rx");
    require_strictly_positive(annotation.ry, "ry");
    require_line_style(annotation.line_color, annotation.line_width);
}

void validate_annotation(const LineSegmentAnnotation &annotation) {
    require_non_negative(annotation.x1, "x1");
    require_non_negative(annotation.y1, "y1");
    require_non_negative(annotation.x2, "x2");
    require_non_negative(annotation.y2, "y2");
    if (annotation.x1 == annotation.x2 && annotation.y1 == annotation.y2) {
        throw BadRequestError("A line segment needs two distinct endpoints");
    }
    require_line_style(annotation.line_color, annotation.line_width);
}

void validate_annotation(const FreehandAnnotation &annotation) {
    if (annotation.points.size() < 2) {
        throw BadRequestError("A freehand stroke needs at least two points");
    }
    for (const auto &point : annotation.points) {
        require_non_negative(point.x, "points.x");
        require_non_negative(point.y, "points.y");
    }
    require_line_style(annotation.line_color, annotation.line_width);
}

void validate_annotation(const PopupNoteAnnotation &annotation) {
    require_non_negative(annotation.x, "x");
    require_non_negative(annotation.y, "y");
}

void validate_annotation(const ImageAnnotation &annotation) {
    std::visit([](const auto &value) { validate_annotation(value); }, annotation);
}

// --- payload readers ------------------------------------------------------

double require_number(const json &payload, std::string_view field) {
    const auto value = payload.at(std::string(field)).get<double>();
    if (!std::isfinite(value)) {
        throw BadRequestError(std::format("{} must be a finite number", field));
    }
    return value;
}

double require_coordinate(const json &payload, std::string_view field) {
    const auto value = require_number(payload, field);
    require_non_negative(value, field);
    return value;
}

double require_size(const json &payload, std::string_view field) {
    const auto value = require_number(payload, field);
    require_strictly_positive(value, field);
    return value;
}

std::string require_string(const json &payload, std::string_view field) {
    return payload.at(std::string(field)).get<std::string>();
}

std::string require_identifier(const json &payload) {
    auto id = payload.at("id").get<std::string>();
    if (id.empty()) throw BadRequestError("id must not be empty");
    return id;
}

std::string require_line_color(const json &payload) {
    auto color = require_string(payload, "line_color");
    if (!is_hex_color(color)) {
        throw BadRequestError("line_color must be #RRGGBB or #RRGGBBAA: " + color);
    }
    return color;
}

double require_line_width(const json &payload) { return require_size(payload, "line_width"); }

std::vector<Point> require_points(const json &payload) {
    const auto &points = payload.at("points");
    if (!points.is_array() || points.size() < 2) {
        throw BadRequestError("points must be an array of at least two {x, y} objects");
    }
    std::vector<Point> result;
    result.reserve(points.size());
    for (const auto &entry : points) {
        Point point;
        point.x = entry.at("x").get<double>();
        point.y = entry.at("y").get<double>();
        require_non_negative(point.x, "points.x");
        require_non_negative(point.y, "points.y");
        result.push_back(point);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Text annotation mutations
//
// Overlapping highlights merge, and removing a range splits the survivors into
// fresh ids.
// ---------------------------------------------------------------------------

// Ids arrive from JavaScript as offsets inside the document; clamp them.
std::int64_t clamp_offset(const json &payload, std::string_view field,
                          std::int64_t text_length) {
    const auto value = payload.at(std::string(field)).get<std::int64_t>();
    return std::clamp(value, std::int64_t{0}, text_length);
}

std::vector<TextAnnotation> add_text_annotation(std::vector<TextAnnotation> annotations,
                                               std::int64_t start, std::int64_t end,
                                               const std::string &comment) {
    if (start > end) std::swap(start, end);
    if (start == end) return annotations;

    std::vector<std::string> merged_comments;
    if (const auto trimmed = strip(comment); !trimmed.empty()) {
        merged_comments.emplace_back(trimmed);
    }

    std::vector<TextAnnotation> overlapping;
    std::vector<TextAnnotation> remaining;
    for (auto &annotation : annotations) {
        // Touching highlights merge too, hence ">" and "<" rather than ">=".
        if (annotation.end < start || annotation.start > end) {
            remaining.push_back(std::move(annotation));
        } else {
            overlapping.push_back(std::move(annotation));
        }
    }

    auto created_at = Timestamp::now();
    if (!overlapping.empty()) {
        for (const auto &annotation : overlapping) {
            start = std::min(start, annotation.start);
            end = std::max(end, annotation.end);
        }
        for (const auto &annotation : overlapping) {
            const auto text = strip(annotation.comment);
            if (text.empty()) continue;
            if (std::ranges::find(merged_comments, text) == merged_comments.end()) {
                merged_comments.emplace_back(text);
            }
        }
        created_at = std::ranges::min(overlapping, {}, &TextAnnotation::created_at).created_at;
    }

    std::string joined_comment;
    for (const auto &part : merged_comments) {
        if (!joined_comment.empty()) joined_comment += "\n\n";
        joined_comment += part;
    }

    TextAnnotation merged;
    merged.id = overlapping.empty() ? new_uuid() : overlapping.front().id;
    merged.start = start;
    merged.end = end;
    merged.comment = std::move(joined_comment);
    merged.created_at = created_at;
    merged.updated_at = Timestamp::now();
    remaining.push_back(std::move(merged));
    std::ranges::sort(remaining, {}, annotation_position);
    return remaining;
}

std::vector<TextAnnotation> remove_text_range(std::vector<TextAnnotation> annotations,
                                             std::int64_t start, std::int64_t end) {
    if (start > end) std::swap(start, end);
    if (start == end) return annotations;

    std::vector<TextAnnotation> updated;
    for (const auto &annotation : annotations) {
        if (annotation.end <= start || annotation.start >= end) {
            updated.push_back(annotation);
            continue;
        }
        if (annotation.start < start) {
            auto left = annotation;
            left.id = new_uuid();
            left.start = annotation.start;
            left.end = start;
            left.updated_at = Timestamp::now();
            updated.push_back(std::move(left));
        }
        if (annotation.end > end) {
            auto right = annotation;
            right.id = new_uuid();
            right.start = end;
            right.end = annotation.end;
            right.updated_at = Timestamp::now();
            updated.push_back(std::move(right));
        }
    }
    std::ranges::sort(updated, {}, annotation_position);
    return updated;
}

std::vector<TextAnnotation> update_text_comment(const std::vector<TextAnnotation> &annotations,
                                               const std::string &annotation_id,
                                               const std::string &comment) {
    bool found = false;
    std::vector<TextAnnotation> updated;
    updated.reserve(annotations.size());
    for (const auto &annotation : annotations) {
        if (annotation.id != annotation_id) {
            updated.push_back(annotation);
            continue;
        }
        found = true;
        auto changed = annotation;
        changed.comment = comment;
        changed.updated_at = Timestamp::now();
        updated.push_back(std::move(changed));
    }
    if (!found) throw BadRequestError("Unknown highlight id: " + annotation_id);
    return updated;
}

std::vector<TextAnnotation> remove_text_annotation(std::vector<TextAnnotation> annotations,
                                                   const std::string &annotation_id) {
    const auto before = annotations.size();
    std::erase_if(annotations,
                  [&](const TextAnnotation &annotation) { return annotation.id == annotation_id; });
    if (annotations.size() == before) {
        throw BadRequestError("Unknown highlight id: " + annotation_id);
    }
    return annotations;
}

// Assigns ids to hand-written entries, drops ranges the text cannot hold, and
// restores document order.
std::vector<TextAnnotation> normalize_text_annotations(std::vector<TextAnnotation> annotations,
                                                       std::int64_t text_length) {
    for (auto &annotation : annotations) {
        if (annotation.id.empty()) annotation.id = new_uuid();
    }
    std::erase_if(annotations, [text_length](const TextAnnotation &annotation) {
        return annotation.start < 0 || annotation.start >= annotation.end ||
               annotation.end > text_length;
    });
    std::ranges::sort(annotations, {}, annotation_position);
    return annotations;
}

// ---------------------------------------------------------------------------
// Image annotation mutations
// ---------------------------------------------------------------------------

template <class Annotation>
std::vector<ImageAnnotation> append_image_annotation(std::vector<ImageAnnotation> annotations,
                                                     Annotation annotation) {
    const auto created_at = Timestamp::now();
    if (annotation.id.empty()) annotation.id = new_uuid();
    annotation.created_at = created_at;
    annotation.updated_at = created_at;
    validate_annotation(annotation);
    annotations.emplace_back(std::move(annotation));
    return annotations;
}

std::vector<ImageAnnotation> add_rectangle(std::vector<ImageAnnotation> annotations,
                                           const json &payload) {
    RectangleAnnotation annotation;
    annotation.x = require_coordinate(payload, "x");
    annotation.y = require_coordinate(payload, "y");
    annotation.width = require_size(payload, "width");
    annotation.height = require_size(payload, "height");
    annotation.line_color = require_line_color(payload);
    annotation.line_width = require_line_width(payload);
    return append_image_annotation(std::move(annotations), std::move(annotation));
}

std::vector<ImageAnnotation> add_ellipse(std::vector<ImageAnnotation> annotations,
                                         const json &payload) {
    EllipseAnnotation annotation;
    annotation.cx = require_coordinate(payload, "cx");
    annotation.cy = require_coordinate(payload, "cy");
    annotation.rx = require_size(payload, "rx");
    annotation.ry = require_size(payload, "ry");
    annotation.line_color = require_line_color(payload);
    annotation.line_width = require_line_width(payload);
    return append_image_annotation(std::move(annotations), std::move(annotation));
}

std::vector<ImageAnnotation> add_line_segment(std::vector<ImageAnnotation> annotations,
                                              const json &payload) {
    LineSegmentAnnotation annotation;
    annotation.x1 = require_coordinate(payload, "x1");
    annotation.y1 = require_coordinate(payload, "y1");
    annotation.x2 = require_coordinate(payload, "x2");
    annotation.y2 = require_coordinate(payload, "y2");
    annotation.line_color = require_line_color(payload);
    annotation.line_width = require_line_width(payload);
    return append_image_annotation(std::move(annotations), std::move(annotation));
}

std::vector<ImageAnnotation> add_freehand(std::vector<ImageAnnotation> annotations,
                                          const json &payload) {
    FreehandAnnotation annotation;
    annotation.points = require_points(payload);
    annotation.line_color = require_line_color(payload);
    annotation.line_width = require_line_width(payload);
    return append_image_annotation(std::move(annotations), std::move(annotation));
}

std::vector<ImageAnnotation> add_popup_note(std::vector<ImageAnnotation> annotations,
                                            const json &payload) {
    PopupNoteAnnotation annotation;
    annotation.x = require_coordinate(payload, "x");
    annotation.y = require_coordinate(payload, "y");
    annotation.text = require_string(payload, "text");
    return append_image_annotation(std::move(annotations), std::move(annotation));
}

std::vector<ImageAnnotation> update_popup_note(const std::vector<ImageAnnotation> &annotations,
                                               const json &payload) {
    const auto id = require_identifier(payload);
    const bool has_text = payload.contains("text");
    const bool has_x = payload.contains("x");
    const bool has_y = payload.contains("y");
    if (!has_text && !has_x && !has_y) {
        throw BadRequestError("update_popup_note needs text, x, or y");
    }
    const auto text = has_text ? payload.at("text").get<std::string>() : std::string{};
    const auto x = has_x ? require_coordinate(payload, "x") : 0.0;
    const auto y = has_y ? require_coordinate(payload, "y") : 0.0;

    std::vector<ImageAnnotation> updated;
    updated.reserve(annotations.size());
    bool found = false;
    for (const auto &annotation : annotations) {
        if (annotation_id(annotation) != id) {
            updated.push_back(annotation);
            continue;
        }
        const auto *note = std::get_if<PopupNoteAnnotation>(&annotation);
        if (note == nullptr) {
            throw BadRequestError("Annotation is not a popup note: " + id);
        }
        found = true;
        auto changed = *note;
        if (has_text) changed.text = text;
        if (has_x) changed.x = x;
        if (has_y) changed.y = y;
        changed.updated_at = Timestamp::now();
        validate_annotation(changed);
        updated.emplace_back(std::move(changed));
    }
    if (!found) throw BadRequestError("Unknown annotation id: " + id);
    return updated;
}

std::vector<ImageAnnotation> remove_image_annotation(std::vector<ImageAnnotation> annotations,
                                                     const json &payload) {
    const auto id = require_identifier(payload);
    const auto before = annotations.size();
    std::erase_if(annotations,
                  [&](const ImageAnnotation &annotation) { return annotation_id(annotation) == id; });
    if (annotations.size() == before) {
        throw BadRequestError("Unknown annotation id: " + id);
    }
    return annotations;
}

// Sidecars can be hand-edited, so every stored annotation is validated and
// given an id before it is used.
std::vector<ImageAnnotation> normalize_image_annotations(std::vector<ImageAnnotation> annotations) {
    for (auto &annotation : annotations) {
        std::visit([](auto &value) {
            if (value.id.empty()) value.id = new_uuid();
        }, annotation);
        validate_annotation(annotation);
    }
    return annotations;
}

// ---------------------------------------------------------------------------
// Sidecar persistence
//
// One owner for "<source>.json": reading and atomic replacement. Merely reading
// a source file never creates a sidecar; only a mutation writes one.
// ---------------------------------------------------------------------------

class SidecarFile {
  public:
    explicit SidecarFile(fs::path source_path)
        : metadata_path_(std::move(source_path).string() + ".json") {}

    [[nodiscard]] const fs::path &metadata_path() const noexcept { return metadata_path_; }

    [[nodiscard]] bool exists() const {
        std::error_code error;
        return fs::is_regular_file(metadata_path_, error);
    }

    [[nodiscard]] std::int64_t annotation_count() const {
        // The listing asks for a count only. A malformed sidecar must not break
        // the whole directory page; opening the file reports the error in full.
        try {
            if (!exists()) return 0;
            const auto document = json::parse(read_file(metadata_path_));
            if (!document.is_object()) return 0;
            const auto &annotations = document.at("annotations");
            return annotations.is_array() ? static_cast<std::int64_t>(annotations.size()) : 0;
        } catch (const std::exception &) {
            return 0;
        }
    }

    template <class Document> [[nodiscard]] Document load_as() const {
        std::error_code error;
        if (!fs::exists(metadata_path_, error)) {
            if (error) {
                throw std::runtime_error("Cannot access sidecar: " + metadata_path_.string());
            }
            return Document{};
        }

        json document;
        try {
            document = json::parse(read_file(metadata_path_));
        } catch (const json::exception &) {
            throw BadRequestError("Invalid JSON in sidecar: " + metadata_path_.string());
        }
        if (!document.is_object()) {
            throw BadRequestError(
                std::format(R"(Sidecar must be an object like {{"kind": "{}", "annotations": [...]}}: {})",
                            Document::kind, metadata_path_.string()));
        }
        try {
            return document.get<Document>();
        } catch (const BadRequestError &) {
            throw;
        } catch (const json::exception &error) {
            throw BadRequestError(std::format("Invalid sidecar {}: {}", metadata_path_.string(),
                                              json_message(error)));
        }
    }

    void save(const AnnotationDocument &document) const {
        write_atomically(metadata_path_, json(document).dump(2) + "\n");
    }

  private:
    fs::path metadata_path_;
};

// ---------------------------------------------------------------------------
// AppState
// ---------------------------------------------------------------------------

// What the directory listing needs to know about one file. Sidecars are
// optional, so "present but empty" and "absent" are different states.
struct AnnotationStatus {
    std::int64_t count = 0;
    bool sidecar_present = false;
};

class AppState {
  public:
    explicit AppState(const fs::path &root_path, bool use_sidecar = true)
        : root_path_(resolved_path(fs::absolute(root_path)).value_or(fs::absolute(root_path))),
          use_sidecar_(use_sidecar) {}

    [[nodiscard]] const fs::path &root_path() const noexcept { return root_path_; }

    [[nodiscard]] bool use_sidecar() const noexcept { return use_sidecar_; }

    // Used by the directory listing. It never creates a sidecar and never
    // throws for a malformed one; the annotation view reports those loudly.
    [[nodiscard]] AnnotationStatus annotation_status(const fs::path &source_path) const {
        if (!use_sidecar_) {
            std::scoped_lock lock(mutex_);
            const auto found = memory_documents_.find(source_path.string());
            if (found == memory_documents_.end()) return {0, false};
            return {document_size(found->second), false};
        }
        const SidecarFile sidecar(source_path);
        return {sidecar.annotation_count(), sidecar.exists()};
    }

    // Maps a URI path onto the filesystem, refusing anything outside the root.
    [[nodiscard]] std::optional<fs::path> filesystem_path_from_uri_path(
        std::string_view uri_path) const {
        fs::path candidate = root_path_;
        for (const auto &component : uri_path_components(uri_path)) candidate /= component;
        const auto resolved = resolved_path(candidate);
        if (!resolved || !is_within(root_path_, *resolved)) return std::nullopt;
        return resolved;
    }

    [[nodiscard]] json text_annotations_payload(std::string_view uri_path) {
        const auto text_path = require_existing_file(uri_path);
        const auto document = load_document(text_path);
        const auto *text = std::get_if<TextDocument>(&document);
        if (text == nullptr) {
            throw BadRequestError("Not a UTF-8 text file: " + text_path.string());
        }

        std::scoped_lock lock(mutex_);
        auto annotations = read_annotations<TextAnnotationDocument>(text_path);
        annotations.annotations =
            normalize_text_annotations(std::move(annotations.annotations),
                                       codepoint_length(text->bytes));
        return json(annotations);
    }

    [[nodiscard]] json image_annotations_payload(std::string_view uri_path) {
        const auto image_path = require_existing_file(uri_path);
        require_raster_image(image_path);

        std::scoped_lock lock(mutex_);
        auto document = read_annotations<ImageAnnotationDocument>(image_path);
        document.annotations = normalize_image_annotations(std::move(document.annotations));
        return json(document);
    }

    [[nodiscard]] json mutate_text(std::string_view uri_path, const json &payload) {
        const auto text_path = require_existing_file(uri_path);
        const auto loaded = load_document(text_path);
        const auto *text = std::get_if<TextDocument>(&loaded);
        if (text == nullptr) {
            throw BadRequestError("Not a UTF-8 text file: " + text_path.string());
        }
        const auto text_length = codepoint_length(text->bytes);

        std::scoped_lock lock(mutex_);
        auto document = read_annotations<TextAnnotationDocument>(text_path);
        auto annotations =
            normalize_text_annotations(std::move(document.annotations), text_length);

        const auto action = payload.value("action", std::string{"none"});
        if (action == "add") {
            annotations = add_text_annotation(std::move(annotations),
                                              clamp_offset(payload, "start", text_length),
                                              clamp_offset(payload, "end", text_length),
                                              payload.value("comment", std::string{}));
        } else if (action == "remove_range") {
            annotations = remove_text_range(std::move(annotations),
                                            clamp_offset(payload, "start", text_length),
                                            clamp_offset(payload, "end", text_length));
        } else if (action == "update_comment") {
            annotations = update_text_comment(annotations, require_identifier(payload),
                                              payload.value("comment", std::string{}));
        } else if (action == "remove_id") {
            annotations = remove_text_annotation(std::move(annotations), require_identifier(payload));
        } else {
            throw BadRequestError("Unsupported action for text annotations: " + action);
        }

        std::ranges::sort(annotations, {}, annotation_position);
        document.annotations = std::move(annotations);
        write_annotations(text_path, document);
        return json(document);
    }

    [[nodiscard]] json mutate_image(std::string_view uri_path, const json &payload) {
        const auto image_path = require_existing_file(uri_path);
        require_raster_image(image_path);

        std::scoped_lock lock(mutex_);
        auto document = read_annotations<ImageAnnotationDocument>(image_path);
        auto annotations = normalize_image_annotations(std::move(document.annotations));

        const auto action = payload.value("action", std::string{"none"});
        if (action == "add_rectangle") {
            annotations = add_rectangle(std::move(annotations), payload);
        } else if (action == "add_ellipse") {
            annotations = add_ellipse(std::move(annotations), payload);
        } else if (action == "add_line_segment") {
            annotations = add_line_segment(std::move(annotations), payload);
        } else if (action == "add_freehand") {
            annotations = add_freehand(std::move(annotations), payload);
        } else if (action == "add_popup_note") {
            annotations = add_popup_note(std::move(annotations), payload);
        } else if (action == "update_popup_note") {
            annotations = update_popup_note(annotations, payload);
        } else if (action == "remove_id") {
            annotations = remove_image_annotation(std::move(annotations), payload);
        } else {
            throw BadRequestError("Unsupported action for image annotations: " + action);
        }

        document.annotations = std::move(annotations);
        write_annotations(image_path, document);
        return json(document);
    }

    // Resolves a URI path to an existing file, or throws the right error.
    [[nodiscard]] fs::path require_existing_file(std::string_view uri_path) const {
        const auto resolved = filesystem_path_from_uri_path(uri_path);
        std::error_code error;
        if (!resolved || !fs::exists(*resolved, error)) {
            throw RequestNotFoundError("File not found: " + std::string(uri_path));
        }
        if (fs::is_directory(*resolved, error)) {
            throw BadRequestError("Path is a directory: " + std::string(uri_path));
        }
        return *resolved;
    }

  private:
    static void require_raster_image(const fs::path &path) {
        const auto document = load_document(path);
        if (!std::holds_alternative<RasterImageDocument>(document)) {
            throw BadRequestError(
                std::format("Not a {} image: {}", supported_raster_formats(), path.string()));
        }
    }

    template <class Document> [[nodiscard]] Document read_annotations(const fs::path &source_path) const {
        if (use_sidecar_) return SidecarFile(source_path).template load_as<Document>();
        const auto found = memory_documents_.find(source_path.string());
        if (found == memory_documents_.end()) return Document{};
        if (!std::holds_alternative<Document>(found->second)) {
            throw BadRequestError("Annotations for this path have a different kind: " +
                                  source_path.string());
        }
        return std::get<Document>(found->second);
    }

    template <class Document> void write_annotations(const fs::path &source_path, Document document) {
        if (use_sidecar_) {
            SidecarFile(source_path).save(AnnotationDocument{std::move(document)});
            return;
        }
        memory_documents_[source_path.string()] = AnnotationDocument{std::move(document)};
    }

    mutable std::mutex mutex_;
    fs::path root_path_;
    bool use_sidecar_ = true;
    std::map<std::string, AnnotationDocument> memory_documents_;
};

// ---------------------------------------------------------------------------
// Responses
// ---------------------------------------------------------------------------

void send_json(httplib::Response &response, const json &payload, int status = http_ok) {
    response.status = status;
    response.set_header("Cache-Control", "no-store");
    response.set_content(payload.dump(), "application/json; charset=utf-8");
}

void send_error_json(httplib::Response &response, const std::string &message, int status) {
    send_json(response, json{{"error", message}}, status);
}

void send_html(httplib::Response &response, std::string body, int status = http_ok) {
    response.status = status;
    response.set_content(std::move(body), "text/html; charset=utf-8");
}

void send_file(httplib::Response &response, const fs::path &path) {
    const auto filename = path.filename().string();
    response.status = http_ok;
    response.set_header(
        "Content-Disposition",
        std::format("attachment; filename*=UTF-8''{}", percent_encode(filename)));
    response.set_content(read_file(path), "application/octet-stream");
}

// Runs a handler, turning any escaping error into the right JSON error body.
template <class Handler> void respond(httplib::Response &response, Handler &&handler) {
    try {
        handler();
    } catch (const HttpError &error) {
        send_error_json(response, error.what(), error.status());
    } catch (const json::exception &error) {
        send_error_json(response, std::string(json_message(error)), http_bad_request);
    } catch (const std::exception &error) {
        send_error_json(response, error.what(), http_internal_server_error);
    }
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------

bool is_sidecar_metadata_file(const fs::path &entry) {
    std::error_code error;
    if (!fs::is_regular_file(entry, error)) return false;
    const auto name = entry.string();
    if (!name.ends_with(".json")) return false;
    return fs::is_regular_file(fs::path(name.substr(0, name.size() - 5)), error);
}

const std::set<std::string_view> &likely_text_extensions() {
    static const std::set<std::string_view> extensions{
        "txt", "text", "md",  "rst", "log", "csv",  "tsv", "jsonl", "ini", "cfg",
        "conf", "toml", "yaml", "yml", "xml", "html", "css", "py",    "js",  "ts",
        "tsx",  "jsx",  "c",   "cc",  "cpp", "h",    "hpp", "java",  "go",  "rs",
        "sh",   "bat",  "ps1", "sql",
    };
    return extensions;
}

bool has_text_extension(const std::string &entry_name) {
    const auto dot = entry_name.rfind('.');
    if (dot == std::string::npos) return true;
    return likely_text_extensions().contains(ascii_lower(entry_name.substr(dot + 1)));
}

// Reads only the first bytes: enough to classify a file for the listing without
// decoding every image on the page.
bool has_raster_signature(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char, 16> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    const auto read = static_cast<std::size_t>(input.gcount());
    return sniff_raster_format(std::string_view(header.data(), read)).has_value();
}

// What a row shows, decided once so the row markup stays a single statement.
struct EntryBadge {
    std::string css_class;
    std::string text;
};

EntryBadge sidecar_badge(const AnnotationStatus &status, bool use_sidecar) {
    if (!use_sidecar) return {"badge-sidecar", "sidecar (ignored)"};
    if (status.count > 0) {
        return {"badge-sidecar", std::format("sidecar · {} annotation{}", status.count,
                                              status.count == 1 ? "" : "s")};
    }
    return {"badge-sidecar", "sidecar"};
}

void replace_marker(std::string &text, std::string_view marker, std::string_view value) {
    if (const auto position = text.find(marker); position != std::string::npos) {
        text.replace(position, marker.size(), value);
    }
}

std::string render_sidecar_note(bool use_sidecar) {
    return use_sidecar
               ? "Sidecars are enabled: annotations are saved next to each source file as "
                 "<code>&lt;file&gt;.json</code>; the source file itself is never modified."
               : "<strong>Sidecars are disabled (<code>--no-sidecar</code>):</strong> "
                 "annotations are kept in memory only and disappear when this server stops. "
                 "Existing <code>.json</code> files are neither read nor written.";
}

std::string render_api_docs(std::string_view docs, bool use_sidecar) {
    std::string rendered(docs);
    replace_marker(rendered, "<!-- SIDECAR_MODE -->", render_sidecar_note(use_sidecar));
    return rendered;
}

std::string render_text_page(bool use_sidecar) {
    std::string page(assets::text_page);
    replace_marker(page, "/* API_DOCS_STYLE */", assets::api_docs_style);
    replace_marker(page, "<!-- API_DOCS -->", render_api_docs(assets::text_api_docs, use_sidecar));
    return page;
}

std::string render_image_page(bool use_sidecar) {
    std::string page(assets::image_page);
    replace_marker(page, "/* API_DOCS_STYLE */", assets::api_docs_style);
    replace_marker(page, "<!-- API_DOCS -->", render_api_docs(assets::image_api_docs, use_sidecar));
    return page;
}

std::string render_directory_listing(AppState &state, std::string_view raw_uri_path,
                                     const fs::path &filesystem_path) {
    const auto components = uri_path_components(raw_uri_path);
    const std::string display_path = raw_uri_path.empty() ? "/" : std::string(raw_uri_path);
    const auto &root_path = state.root_path();

    std::string rows;
    std::string annotated_items;
    std::int64_t annotated_file_count = 0;

    if (!components.empty()) {
        const std::vector<std::string> parent(components.begin(), components.end() - 1);
        rows += std::format(
            "<li><div class='entry'><a href='{}' class='name'>../</a>"
            "<span class='badge badge-dir'>up</span></div></li>\n",
            uri_path_from_components(parent, true));
    }

    std::vector<fs::directory_entry> entries(fs::directory_iterator(filesystem_path),
                                             fs::directory_iterator{});
    // Directories first, then names case-insensitively.
    std::ranges::stable_sort(
        entries, [](const fs::directory_entry &left, const fs::directory_entry &right) {
            std::error_code left_error;
            std::error_code right_error;
            const auto left_is_directory = left.is_directory(left_error);
            const auto right_is_directory = right.is_directory(right_error);
            if (left_is_directory != right_is_directory) return left_is_directory;
            return ascii_lower(left.path().filename().string()) <
                   ascii_lower(right.path().filename().string());
        });

    for (const auto &entry : entries) {
        const auto entry_path = entry.path();
        const auto entry_name = entry_path.filename().string();
        std::error_code error;
        const auto is_directory = entry.is_directory(error);
        const auto is_sidecar = is_sidecar_metadata_file(entry_path);

        auto entry_components = components;
        entry_components.push_back(entry_name);
        const auto href = uri_path_from_components(entry_components, is_directory);
        const auto annotate_href = std::format("/annotate?path={}", percent_encode(href));

        // The sidecar row describes the source file's annotations, not a
        // sidecar for the sidecar itself.
        const auto source_path =
            is_sidecar
                ? fs::path(entry_path.string().substr(0, entry_path.string().size() - 5))
                : entry_path;
        const auto status = is_directory ? AnnotationStatus{} : state.annotation_status(source_path);

        EntryBadge badge;
        if (is_directory) {
            badge = {"badge-dir", "directory"};
        } else if (is_sidecar) {
            badge = sidecar_badge(status, state.use_sidecar());
        } else if (status.count > 0) {
            badge = {"badge-annotated", std::format("annotated · {}", status.count)};
        } else if (has_raster_signature(entry_path)) {
            badge = {"badge-image", "image · annotate"};
        } else if (has_text_extension(entry_name)) {
            badge = {"badge-text", "annotate"};
        } else {
            badge = {"badge-file", "file"};
        }

        if (!is_directory && !is_sidecar && status.count > 0) {
            ++annotated_file_count;
            annotated_items += std::format(
                "<li><a href='{}'>{}</a> <span class='count'>— {} annotation{}</span></li>",
                annotate_href, html_escape(entry_name), status.count,
                status.count == 1 ? "" : "s");
        }

        const auto row_href = is_directory ? href : annotate_href;
        const auto download_link =
            is_directory
                ? std::string{}
                : std::format("<a class='download' href='{}' download>Download</a>", href);
        rows += std::format(
            "<li><div class='entry'><a href='{}' class='name'>{}</a> "
            "<span class='badge {}'>{}</span>{}</div></li>\n",
            row_href, html_escape(entry_name + (is_directory ? "/" : "")), badge.css_class,
            html_escape(badge.text), download_link);
    }

    std::string annotated_summary;
    if (annotated_file_count > 0) {
        annotated_summary = std::format(
            "<p><strong>{} annotated file{} in this directory{}:</strong></p><ul>{}</ul>",
            annotated_file_count, annotated_file_count == 1 ? "" : "s",
            state.use_sidecar() ? "" : " (in memory for this run)", annotated_items);
    } else {
        annotated_summary = state.use_sidecar()
                                ? "<p>No files in this directory are annotated yet.</p>"
                                : "<p>No files in this directory have in-memory annotations yet. "
                                  "Sidecars are disabled (<code>--no-sidecar</code>).</p>";
    }
    if (!state.use_sidecar()) {
        annotated_summary +=
            "<p>Existing <code>.json</code> sidecars are ignored in this run.</p>";
    }

    return std::format(assets::directory_page, html_escape(display_path),
                       html_escape(root_path.string()), rows, annotated_summary,
                       render_api_docs(assets::directory_api_docs, state.use_sidecar()),
                       assets::api_docs_style);
}

std::string render_error_page(std::string_view title, std::string_view message) {
    return std::format(assets::error_page, html_escape(title), html_escape(message));
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

void handle_annotation_view(AppState &state, const std::string &document_path,
                            httplib::Response &response) {
    const auto filesystem_path = state.filesystem_path_from_uri_path(document_path);
    std::error_code error;
    if (!filesystem_path || !fs::exists(*filesystem_path, error)) {
        throw RequestNotFoundError("File not found: " + document_path);
    }
    if (fs::is_directory(*filesystem_path, error)) {
        throw BadRequestError("Path is a directory: " + document_path);
    }

    std::optional<LoadedDocument> document;
    try {
        document = load_document(*filesystem_path);
    } catch (const UnsupportedFileError &unsupported) {
        send_html(response, render_error_page("Cannot annotate this file", unsupported.what()),
                  http_bad_request);
        return;
    }

    const auto is_image = std::holds_alternative<RasterImageDocument>(*document);
    send_html(response, is_image ? render_image_page(state.use_sidecar())
                                 : render_text_page(state.use_sidecar()));
}

void handle_get(AppState &state, const httplib::Request &request, httplib::Response &response) {
    const auto target = split_request_target(request.target);
    respond(response, [&] {
        if (target.path == text_annotations_route) {
            const auto document_path = first_query_value(target.query, "path").value_or("/");
            send_json(response, state.text_annotations_payload(document_path));
            return;
        }

        if (target.path == image_annotations_route) {
            const auto document_path = first_query_value(target.query, "path").value_or("/");
            send_json(response, state.image_annotations_payload(document_path));
            return;
        }

        if (target.path == "/annotate") {
            handle_annotation_view(state, first_query_value(target.query, "path").value_or("/"),
                                   response);
            return;
        }

        const auto filesystem_path = state.filesystem_path_from_uri_path(target.path);
        std::error_code error;
        if (!filesystem_path || !fs::exists(*filesystem_path, error)) {
            throw RequestNotFoundError("Not found");
        }
        if (fs::is_directory(*filesystem_path, error)) {
            send_html(response, render_directory_listing(state, target.path, *filesystem_path));
            return;
        }
        send_file(response, *filesystem_path);
    });
}

void handle_post(AppState &state, const httplib::Request &request, httplib::Response &response) {
    const auto target = split_request_target(request.target);
    respond(response, [&] {
        const bool is_text = target.path == text_annotations_route;
        const bool is_image = target.path == image_annotations_route;
        if (!is_text && !is_image) throw RequestNotFoundError("Not found");

        const auto document_path = first_query_value(target.query, "path").value_or("/");

        json payload = json::object();
        if (!request.body.empty()) {
            try {
                payload = json::parse(request.body);
            } catch (const json::parse_error &error) {
                throw BadRequestError(std::format("Invalid JSON body at byte {}", error.byte));
            }
        }
        if (!payload.is_object()) {
            throw BadRequestError("Request body must be a JSON object");
        }

        send_json(response, is_text ? state.mutate_text(document_path, payload)
                                    : state.mutate_image(document_path, payload));
    });
}

std::string log_timestamp() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%d/%b/%Y %H:%M:%S", &local);
    return buffer;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

template <class... Args>
void print_line(std::ostream &stream, std::format_string<Args...> format, Args &&...args) {
    stream << std::format(format, std::forward<Args>(args)...) << '\n';
}

int fail(std::string_view message) {
    print_line(std::cerr, "{}", message);
    return 1;
}

httplib::Server *active_server = nullptr;

extern "C" void handle_sigint(int) {
    if (active_server != nullptr) active_server->stop();
}

int serve(const std::string &raw_path, const std::string &host, int port, bool use_sidecar) {
    const fs::path target = fs::absolute(fs::path(raw_path)).lexically_normal();
    std::error_code error;
    if (!fs::exists(target, error)) return fail("Path not found: " + target.string());

    fs::path root;
    std::string initial_uri_path = "/";
    if (fs::is_directory(target, error)) {
        root = target;
        print_line(std::cout, "Serving directory {}", root.string());
    } else {
        root = target.parent_path();
        initial_uri_path = uri_path_from_components({target.filename().string()});
        print_line(std::cout, "Serving file {}", target.string());
    }

    AppState state(root, use_sidecar);
    if (!use_sidecar) {
        print_line(std::cout, "Sidecars disabled: annotations are kept in memory for this run");
    }

    httplib::Server server;
    active_server = &server;
    server.set_default_headers({{"Server", server_version}});
    server.set_logger([](const httplib::Request &request, const httplib::Response &response) {
        print_line(std::cout, "[{}] {} - \"{} {}\" {} -", log_timestamp(), request.remote_addr,
                   request.method, request.target, response.status);
    });

    // Any method other than GET/POST is rejected with 501. GET and POST must
    // fall through to real routes: the body is only read
    // once routing completes, so a pre-routing handler would always see it empty.
    server.set_pre_routing_handler(
        [](const httplib::Request &request, httplib::Response &response) {
            if (request.method == "GET" || request.method == "POST") {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            response.status = 501;
            response.set_content("Unsupported method: " + request.method, "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        });
    server.Get(R"((.*))", [&state](const httplib::Request &request, httplib::Response &response) {
        handle_get(state, request, response);
    });
    server.Post(R"((.*))", [&state](const httplib::Request &request, httplib::Response &response) {
        handle_post(state, request, response);
    });

    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    print_line(std::cout, "Root directory: {}", state.root_path().string());
    print_line(std::cout, "Directory view: http://{}:{}/", host, port);
    if (initial_uri_path != "/") {
        print_line(std::cout, "Annotation view: http://{}:{}/annotate?path={}", host, port,
                   percent_encode(initial_uri_path));
    }

    if (!server.listen(host, port)) {
        active_server = nullptr;
        return fail(std::format("Cannot listen on {}:{}", host, port));
    }

    active_server = nullptr;
    print_line(std::cout, "\nShutting down…");
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    // Line-buffer stdout so a redirected log is written as lines are produced,
    // not in 4 KiB blocks when the process exits.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    CLI::App app{"Serve a UTF-8 text file or raster image, or a directory of them, for "
                 "annotating in the browser."};

    std::string path = ".";
    std::string host = "localhost";
    int port = 8000;
    bool no_sidecar = false;
    app.add_option("path", path,
                   "UTF-8 text file, raster image, or a directory of them "
                   "(default: current directory)")
        ->capture_default_str();
    app.add_option("--host", host, "Host interface to bind to")->capture_default_str();
    app.add_option("--port", port, "Port to listen on")->capture_default_str();
    app.add_flag("--no-sidecar", no_sidecar,
                 "Do not read or write .json sidecar files; keep annotations in memory "
                 "for this run");

    CLI11_PARSE(app, argc, argv);
    return serve(path, host, port, !no_sidecar);
}
