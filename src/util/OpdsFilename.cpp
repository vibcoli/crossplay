#include "OpdsFilename.h"

#include <cctype>
#include <cstddef>

#include "StringUtils.h"

namespace {

// The only container format the reader opens, so it is also the only suffix
// worth keeping off a server. KOReader asks its DocumentRegistry the same
// question for every format it supports.
constexpr char kExtension[] = ".epub";

// Value of the plain `filename` parameter, quoted form first. `filename*`
// (RFC 5987) is deliberately not read -- see the header.
std::string dispositionFilename(const std::string& disposition) {
  const size_t quoted = disposition.find("filename=\"");
  if (quoted != std::string::npos) {
    const size_t start = quoted + 10;  // past `filename="`
    const size_t end = disposition.find('"', start);
    // An empty quoted value names nothing, so the other sources get their
    // turn. KOReader instead carries the two quotes through and stores the
    // book as `"".epub`; that is an unusable name on any device, so there is
    // no hash there worth matching. An UNterminated quote is different: it
    // falls through to the unquoted read below, which is what KOReader does.
    if (end != std::string::npos) return disposition.substr(start, end - start);
  }
  // Unquoted: everything up to the next parameter separator. A `filename*=`
  // earlier in the header cannot be mistaken for this one, because the `*`
  // sits between the name and the `=`.
  const size_t bare = disposition.find("filename=");
  if (bare == std::string::npos) return "";
  const size_t start = bare + 9;  // past `filename=`
  const size_t end = disposition.find(';', start);
  return disposition.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Everything after the last '/', the whole string when there is none.
std::string lastSegment(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// True when `name` already ends in the extension, compared case-insensitively
// so a server's "BOOK.EPUB" is not handed a second extension.
bool hasExtension(const std::string& name) {
  const size_t extLen = sizeof(kExtension) - 1;
  if (name.size() <= extLen) return false;
  const size_t offset = name.size() - extLen;
  for (size_t i = 0; i < extLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(name[offset + i])) != kExtension[i]) return false;
  }
  return true;
}

// Decodes %XX escapes; anything that is not a complete escape stays literal.
// '+' is left alone: it is a query-string convention, and a literal plus is
// perfectly ordinary in a book title.
std::string percentDecode(const std::string& text) {
  const auto hexValue = [](const unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int high = hexValue(static_cast<unsigned char>(text[i + 1]));
      const int low = hexValue(static_cast<unsigned char>(text[i + 2]));
      if (high >= 0 && low >= 0) {
        out += static_cast<char>(high * 16 + low);
        i += 2;
        continue;
      }
    }
    out += text[i];
  }
  return out;
}

}  // namespace

std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format) {
  std::string base;
  switch (format) {
    case OpdsFilenameFormat::TitleAuthor:
      base = author.empty() ? title : title + " - " + author;
      break;
    case OpdsFilenameFormat::TitleOnly:
      base = title;
      break;
    case OpdsFilenameFormat::AuthorTitle:
    default:
      base = author.empty() ? title : author + " - " + title;
      break;
  }
  // sanitizeFilename caps at 100 bytes and never returns empty (falls back to
  // "book"); ".epub" is appended after so the extension is never truncated —
  // identical treatment to the previous inline construction.
  return StringUtils::sanitizeFilename(base) + ".epub";
}

std::string opdsServerFilename(const std::string& contentDisposition, const std::string& location,
                               const std::string& url) {
  std::string name = dispositionFilename(contentDisposition);
  // A redirect's target names the file on servers that answer the acquisition
  // link with a 302 to static storage.
  if (name.empty()) name = lastSegment(location);
  if (name.empty()) {
    // Path segment first, query second: a '/' inside the query counts as the
    // last slash, exactly as KOReader's two successive substitutions leave it.
    name = lastSegment(url);
    const size_t query = name.find('?');
    if (query != std::string::npos) name.erase(query);
  }
  // Nothing to go on (a collection URL ending in '/', an empty header). The
  // caller composes from metadata instead; inventing "book.epub" here would
  // collide with every other nameless download and match no other device.
  if (name.empty()) return "";

  if (!hasExtension(name)) name += kExtension;
  return StringUtils::sanitizeFilenamePreservingExtension(percentDecode(name));
}
