#include "NotesCore.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace notes {
namespace {

constexpr uint32_t SECONDS_PER_MINUTE = 60;
constexpr uint32_t SECONDS_PER_HOUR = 3600;
constexpr uint32_t SECONDS_PER_DAY = 86400;

bool isSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

// Case-insensitive compare against an ASCII literal. Property names are
// case-insensitive per RFC 5545 and servers do vary (Last-Modified appears
// mixed-case in the wild).
bool equalsIgnoreCase(const char* a, size_t aLen, const char* b) {
  const size_t bLen = std::strlen(b);
  if (aLen != bLen) return false;
  for (size_t i = 0; i < aLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// Splits "SUMMARY;LANGUAGE=de:Einkauf" into name and value. Parameters are
// dropped: a parameter value may itself contain a colon inside quotes, so the
// split scans for the first unquoted colon rather than the first colon.
bool splitProperty(const std::string& line, std::string& name, std::string& value) {
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      inQuotes = !inQuotes;
    } else if (c == ':' && !inQuotes) {
      const size_t nameEnd = line.find(';');
      name = line.substr(0, std::min(nameEnd, i));
      value = line.substr(i + 1);
      return true;
    }
  }
  return false;
}

// "20260913T170721Z" and "20260913" both appear; the date form is midnight UTC.
// Returns 0 for anything else, which the Note treats as "no timestamp".
uint32_t parseIcalTime(const std::string& value) {
  if (value.size() < 8) return 0;
  for (size_t i = 0; i < 8; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) return 0;
  }
  const int year = std::stoi(value.substr(0, 4));
  const int month = std::stoi(value.substr(4, 2));
  const int day = std::stoi(value.substr(6, 2));
  if (month < 1 || month > 12 || day < 1 || day > 31) return 0;

  int hour = 0, minute = 0, second = 0;
  if (value.size() >= 15 && value[8] == 'T') {
    for (size_t i = 9; i < 15; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(value[i]))) return 0;
    }
    hour = std::stoi(value.substr(9, 2));
    minute = std::stoi(value.substr(11, 2));
    second = std::stoi(value.substr(13, 2));
    if (hour > 23 || minute > 59 || second > 60) return 0;
  }

  // Days since 1970-01-01 by Howard Hinnant's civil-from-days, which needs no
  // table and no mktime (absent from the device's libc in any useful form).
  int y = year;
  y -= month <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
  if (days < 0) return 0;
  const long long epoch = days * SECONDS_PER_DAY + hour * SECONDS_PER_HOUR + minute * SECONDS_PER_MINUTE + second;
  if (epoch < 0 || epoch > 0xFFFFFFFFLL) return 0;
  return static_cast<uint32_t>(epoch);
}

// Local name of an XML tag, so <D:href>, <d:href> and <href> all match. Returns
// the name without prefix; the caller compares case-insensitively.
std::string localName(const std::string& tag) {
  size_t start = 0;
  while (start < tag.size() && (tag[start] == '/' || isSpaceOrTab(tag[start]))) ++start;
  size_t end = start;
  while (end < tag.size() && !isSpaceOrTab(tag[end]) && tag[end] != '/' && tag[end] != '>') ++end;
  std::string name = tag.substr(start, end - start);
  const size_t colon = name.find(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name;
}

// XML text with the five predefined entities resolved. Servers escape & and <
// in displaynames, and calendar-data arrives fully escaped, so skipping this
// puts "&amp;" on the panel.
std::string unescapeXml(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] != '&') {
      out += text[i++];
      continue;
    }
    const size_t semi = text.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out += text[i++];
      continue;
    }
    const std::string entity = text.substr(i + 1, semi - i - 1);
    if (entity == "amp") {
      out += '&';
    } else if (entity == "lt") {
      out += '<';
    } else if (entity == "gt") {
      out += '>';
    } else if (entity == "quot") {
      out += '"';
    } else if (entity == "apos") {
      out += '\'';
    } else if (entity.size() > 1 && entity[0] == '#') {
      // Numeric references, decimal and hex. Only Latin-1 is folded back to one
      // byte; anything above that is left as the reference rather than
      // producing a broken UTF-8 sequence the font renderer would choke on.
      const bool hex = entity[1] == 'x' || entity[1] == 'X';
      const char* digits = entity.c_str() + (hex ? 2 : 1);
      char* parseEnd = nullptr;
      const long code = std::strtol(digits, &parseEnd, hex ? 16 : 10);
      if (parseEnd && *parseEnd == '\0' && code > 0 && code < 0x80) {
        out += static_cast<char>(code);
      } else {
        out += text.substr(i, semi - i + 1);
      }
    } else {
      out += text.substr(i, semi - i + 1);
    }
    i = semi + 1;
  }
  return out;
}

}  // namespace

std::string unfoldIcal(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') {
      if (i + 2 < raw.size() && isSpaceOrTab(raw[i + 2])) {
        i += 2;  // the space is consumed with the break
        continue;
      }
      out += '\n';
      ++i;
      continue;
    }
    if (raw[i] == '\n') {
      if (i + 1 < raw.size() && isSpaceOrTab(raw[i + 1])) {
        ++i;
        continue;
      }
      out += '\n';
      continue;
    }
    out += raw[i];
  }
  return out;
}

std::string unescapeIcalText(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out += value[i];
      continue;
    }
    const char next = value[++i];
    switch (next) {
      case 'n':
      case 'N':
        out += '\n';
        break;
      default:
        // \\ \, \; and anything else: the character stands for itself.
        out += next;
        break;
    }
  }
  return out;
}

void parseIcal(const std::string& ical, const std::string& calendarName, std::vector<Note>& out) {
  const std::string text = unfoldIcal(ical);

  Note current;
  bool inNote = false;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t lineEnd = text.find('\n', pos);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    const std::string line = text.substr(pos, lineEnd - pos);
    pos = lineEnd + 1;
    if (line.empty()) {
      if (lineEnd == text.size()) break;
      continue;
    }

    std::string name, value;
    if (!splitProperty(line, name, value)) continue;
    // Trailing CR survives when a server sends bare CR line ends.
    if (!value.empty() && value.back() == '\r') value.pop_back();

    if (equalsIgnoreCase(name.c_str(), name.size(), "BEGIN")) {
      if (equalsIgnoreCase(value.c_str(), value.size(), "VJOURNAL") ||
          equalsIgnoreCase(value.c_str(), value.size(), "VTODO")) {
        current = Note{};
        current.calendar = calendarName;
        inNote = true;
      }
      continue;
    }
    if (equalsIgnoreCase(name.c_str(), name.size(), "END")) {
      if (inNote && (equalsIgnoreCase(value.c_str(), value.size(), "VJOURNAL") ||
                     equalsIgnoreCase(value.c_str(), value.size(), "VTODO"))) {
        // A component with neither a summary nor a body is a reminder someone
        // created and never typed into; it would draw as a blank row.
        if (!current.title.empty() || !current.body.empty()) {
          if (current.title.empty()) current.title = previewLine(current.body, 64);
          out.push_back(current);
        }
        inNote = false;
      }
      continue;
    }
    if (!inNote) continue;

    if (equalsIgnoreCase(name.c_str(), name.size(), "UID")) {
      current.uid = value;
    } else if (equalsIgnoreCase(name.c_str(), name.size(), "SUMMARY")) {
      current.title = unescapeIcalText(value);
    } else if (equalsIgnoreCase(name.c_str(), name.size(), "DESCRIPTION")) {
      current.body = unescapeIcalText(value);
    } else if (equalsIgnoreCase(name.c_str(), name.size(), "STATUS")) {
      current.done = equalsIgnoreCase(value.c_str(), value.size(), "COMPLETED");
    } else if (equalsIgnoreCase(name.c_str(), name.size(), "LAST-MODIFIED")) {
      current.modified = parseIcalTime(value);
    } else if (equalsIgnoreCase(name.c_str(), name.size(), "DTSTAMP") && current.modified == 0) {
      // Fallback only: LAST-MODIFIED is the edit time, DTSTAMP is when the
      // object was serialised, and a server that sends both means the former.
      current.modified = parseIcalTime(value);
    }
  }
}

std::vector<DavResponse> parseMultistatus(const std::string& xml) {
  std::vector<DavResponse> out;
  DavResponse current;
  bool inResponse = false;
  // Nested <href> appear inside <calendar-home-set> and <current-user-principal>
  // as well as directly under <response>; the first one seen in a response is
  // the resource, any later one is the property value and is the one wanted.
  bool hrefTaken = false;

  size_t pos = 0;
  while (pos < xml.size()) {
    const size_t open = xml.find('<', pos);
    if (open == std::string::npos) break;
    const size_t close = xml.find('>', open);
    if (close == std::string::npos) break;
    const std::string tag = xml.substr(open + 1, close - open - 1);
    const bool closing = !tag.empty() && tag[0] == '/';
    const bool selfClosing = !tag.empty() && tag.back() == '/';
    const std::string name = localName(tag);
    const size_t textStart = close + 1;
    const size_t nextOpen = xml.find('<', textStart);
    const std::string text =
        nextOpen == std::string::npos ? std::string() : xml.substr(textStart, nextOpen - textStart);
    pos = close + 1;

    if (name == "response") {
      if (closing) {
        if (inResponse && !current.href.empty()) out.push_back(current);
        inResponse = false;
      } else if (!selfClosing) {
        current = DavResponse{};
        hrefTaken = false;
        inResponse = true;
      }
      continue;
    }
    if (!inResponse) continue;
    // <comp/> is where the component set lives and it is always self-closing,
    // so it is read before the closing-tag skip below rather than after it.
    if (name == "comp") {
      // <comp name="VTODO"/> inside supported-calendar-component-set. Read off
      // the tag's attribute rather than tracking the parent element.
      const size_t nameAttr = tag.find("name=");
      if (nameAttr != std::string::npos) {
        const size_t quote = tag.find_first_of("\"'", nameAttr);
        if (quote != std::string::npos) {
          const size_t endQuote = tag.find(tag[quote], quote + 1);
          if (endQuote != std::string::npos) {
            const std::string comp = tag.substr(quote + 1, endQuote - quote - 1);
            if (equalsIgnoreCase(comp.c_str(), comp.size(), "VJOURNAL") ||
                equalsIgnoreCase(comp.c_str(), comp.size(), "VTODO")) {
              current.holdsNotes = true;
            }
          }
        }
      }
      continue;
    }
    if (closing || selfClosing) continue;

    if (name == "href") {
      // The resource href comes first; a later one belongs to a property such
      // as calendar-home-set, and that is the address a caller wants next.
      if (!hrefTaken) {
        current.href = unescapeXml(text);
        hrefTaken = true;
      } else {
        current.href = unescapeXml(text);
      }
    } else if (name == "displayname") {
      current.displayName = unescapeXml(text);
    } else if (name == "calendar-data") {
      current.calendarData = unescapeXml(text);
    }
  }
  return out;
}

std::string resolveHref(const std::string& baseUrl, const std::string& href) {
  if (href.empty()) return baseUrl;
  if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) return href;

  const size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd == std::string::npos) return href;
  const size_t hostEnd = baseUrl.find('/', schemeEnd + 3);
  const std::string origin = hostEnd == std::string::npos ? baseUrl : baseUrl.substr(0, hostEnd);
  if (href[0] == '/') return origin + href;

  // Relative to the base's directory. Rare from real servers, but a href of
  // "calendar/" against ".../home/" must not become ".../calendar/".
  const std::string path = hostEnd == std::string::npos ? std::string("/") : baseUrl.substr(hostEnd);
  const size_t lastSlash = path.rfind('/');
  return origin + path.substr(0, lastSlash + 1) + href;
}

const char* propfindCurrentUserPrincipal() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";
}

const char* propfindCalendarHomeSet() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
         "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";
}

const char* propfindCalendarCollections() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
         "<d:prop><d:displayname/><d:resourcetype/>"
         "<c:supported-calendar-component-set/></d:prop></d:propfind>";
}

std::string reportNotesQuery(const char* componentName) {
  std::string query =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:prop><d:getetag/><c:calendar-data/></d:prop>"
      "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"";
  query += componentName;
  query += "\"/></c:comp-filter></c:filter></c:calendar-query>";
  return query;
}

std::string discoveryUrl(const std::string& serverInput) {
  std::string url = serverInput;
  // Trim, because a pasted address routinely carries a trailing space.
  while (!url.empty() && isSpaceOrTab(url.front())) url.erase(url.begin());
  while (!url.empty() && (isSpaceOrTab(url.back()) || url.back() == '\n' || url.back() == '\r')) url.pop_back();
  if (url.empty()) return url;

  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) url = "https://" + url;

  // A bare host gets the RFC 6764 well-known path; a host with a path is taken
  // as given, because that is how someone points at a single collection.
  const size_t schemeEnd = url.find("://");
  const size_t hostEnd = url.find('/', schemeEnd + 3);
  if (hostEnd == std::string::npos) return url + "/.well-known/caldav";
  if (hostEnd == url.size() - 1) return url.substr(0, hostEnd) + "/.well-known/caldav";
  return url;
}

void sortNewestFirst(std::vector<Note>& notes) {
  std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
    if (a.modified != b.modified) {
      if (a.modified == 0) return false;
      if (b.modified == 0) return true;
      return a.modified > b.modified;
    }
    return a.title < b.title;
  });
}

Age ageSince(uint32_t thenEpoch, uint32_t nowEpoch) {
  Age age;
  if (thenEpoch == 0 || nowEpoch <= thenEpoch) return age;
  const uint32_t elapsed = nowEpoch - thenEpoch;
  if (elapsed < SECONDS_PER_MINUTE) return age;
  if (elapsed < SECONDS_PER_HOUR) {
    age.unit = AgeUnit::Minutes;
    age.value = elapsed / SECONDS_PER_MINUTE;
  } else if (elapsed < SECONDS_PER_DAY) {
    age.unit = AgeUnit::Hours;
    age.value = elapsed / SECONDS_PER_HOUR;
  } else {
    age.unit = AgeUnit::Days;
    age.value = elapsed / SECONDS_PER_DAY;
  }
  return age;
}

std::string previewLine(const std::string& body, size_t maxChars) {
  size_t start = 0;
  while (start < body.size() && (body[start] == '\n' || body[start] == '\r' || isSpaceOrTab(body[start]))) ++start;
  size_t end = body.find('\n', start);
  if (end == std::string::npos) end = body.size();
  while (end > start && (body[end - 1] == '\r' || isSpaceOrTab(body[end - 1]))) --end;

  std::string line = body.substr(start, end - start);
  if (line.size() <= maxChars) return line;

  // Clip on a character boundary: cutting mid-sequence puts a replacement glyph
  // on the panel, and the fork's fonts draw one visibly.
  size_t clip = maxChars;
  while (clip > 0 && (static_cast<unsigned char>(line[clip]) & 0xC0) == 0x80) --clip;
  line.resize(clip);
  line += "...";
  return line;
}

}  // namespace notes
