#include "DavCore.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace dav {
namespace {

constexpr uint32_t SECONDS_PER_MINUTE = 60;
constexpr uint32_t SECONDS_PER_HOUR = 3600;
constexpr uint32_t SECONDS_PER_DAY = 86400;

bool isSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

// Case-insensitive compare against an ASCII literal. Property and component
// names are case-insensitive in both formats, and servers do vary.
bool equalsIgnoreCase(const char* a, size_t aLen, const char* b) {
  const size_t bLen = std::strlen(b);
  if (aLen != bLen) return false;
  for (size_t i = 0; i < aLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}
bool is(const std::string& s, const char* literal) { return equalsIgnoreCase(s.c_str(), s.size(), literal); }

// Splits "DTSTART;TZID=Europe/Berlin:20260914T090000" into name, parameters
// and value. The scan stops at the first UNQUOTED colon: a parameter value may
// hold one inside quotes, and splitting on the first colon would cut the
// property name in half.
bool splitProperty(const std::string& line, std::string& name, std::string& params, std::string& value) {
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      inQuotes = !inQuotes;
    } else if (c == ':' && !inQuotes) {
      const size_t semi = line.find(';');
      if (semi != std::string::npos && semi < i) {
        name = line.substr(0, semi);
        params = line.substr(semi + 1, i - semi - 1);
      } else {
        name = line.substr(0, i);
        params.clear();
      }
      value = line.substr(i + 1);
      if (!value.empty() && value.back() == '\r') value.pop_back();
      return true;
    }
  }
  return false;
}

// vCard 4.0 allows a group prefix: "item1.TEL:...". It carries no meaning here.
std::string stripGroup(const std::string& name) {
  const size_t dot = name.find('.');
  return dot == std::string::npos ? name : name.substr(dot + 1);
}

// "20260913T170721Z", "20260913T170721" and "20260913" all appear. isDate says
// the value carried no time at all, which is what makes an event all-day.
uint32_t parseIcalTime(const std::string& value, bool* isDate = nullptr) {
  if (isDate) *isDate = false;
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
  } else if (isDate) {
    *isDate = true;
  }

  // Days since 1970-01-01 by Howard Hinnant's civil-from-days: no table, and
  // no mktime, which the device's libc does not usefully provide.
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

// Local name of an XML tag, so <D:href>, <d:href> and <href> all match.
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

// XML text with the predefined entities resolved. calendar-data arrives fully
// escaped, so skipping this feeds "&#13;&#10;" to the iCalendar parser as if
// it were text.
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
      // Numeric references, decimal and hex. Only ASCII is folded back to one
      // byte; anything above would need a UTF-8 encoder to be correct, and a
      // broken sequence draws as a replacement glyph.
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

// Reads name="X" off an opening tag, for <comp name="VEVENT"/>.
std::string tagNameAttribute(const std::string& tag) {
  const size_t attr = tag.find("name=");
  if (attr == std::string::npos) return std::string();
  const size_t quote = tag.find_first_of("\"'", attr);
  if (quote == std::string::npos) return std::string();
  const size_t endQuote = tag.find(tag[quote], quote + 1);
  if (endQuote == std::string::npos) return std::string();
  return tag.substr(quote + 1, endQuote - quote - 1);
}

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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

void parseIcal(const std::string& ical, const std::string& collectionName, std::vector<Entry>& out) {
  const std::string text = unfoldIcal(ical);

  Entry current;
  bool inComponent = false;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t lineEnd = text.find('\n', pos);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    const std::string line = text.substr(pos, lineEnd - pos);
    const bool lastLine = lineEnd == text.size();
    pos = lineEnd + 1;
    if (line.empty()) {
      if (lastLine) break;
      continue;
    }

    std::string name, params, value;
    if (!splitProperty(line, name, params, value)) {
      if (lastLine) break;
      continue;
    }

    if (is(name, "BEGIN")) {
      if (is(value, "VEVENT")) {
        current = Entry{};
        current.kind = Kind::Event;
        current.collection = collectionName;
        inComponent = true;
      } else if (is(value, "VTODO")) {
        current = Entry{};
        current.kind = Kind::Reminder;
        current.collection = collectionName;
        inComponent = true;
      } else if (is(value, "VJOURNAL")) {
        current = Entry{};
        current.kind = Kind::Note;
        current.collection = collectionName;
        inComponent = true;
      }
      continue;
    }
    if (is(name, "END")) {
      if (inComponent && (is(value, "VEVENT") || is(value, "VTODO") || is(value, "VJOURNAL"))) {
        // A component with no summary and no body is one somebody created and
        // never typed into; it would draw as a blank row.
        if (!current.title.empty() || !current.body.empty()) {
          if (current.title.empty()) current.title = firstLine(current.body);
          out.push_back(current);
        }
        inComponent = false;
      }
      continue;
    }
    if (!inComponent) continue;

    if (is(name, "UID")) {
      current.uid = value;
    } else if (is(name, "SUMMARY")) {
      current.title = unescapeIcalText(value);
    } else if (is(name, "DESCRIPTION")) {
      current.body = unescapeIcalText(value);
    } else if (is(name, "LOCATION")) {
      current.location = unescapeIcalText(value);
    } else if (is(name, "STATUS")) {
      current.done = is(value, "COMPLETED");
    } else if (is(name, "COMPLETED")) {
      // A VTODO may record its completion time without ever setting STATUS.
      current.done = true;
    } else if (is(name, "DTSTART")) {
      bool isDate = false;
      current.start = parseIcalTime(value, &isDate);
      // VALUE=DATE is the explicit spelling; a bare eight-digit value is the
      // one servers actually send, so both have to count.
      current.allDay = isDate || params.find("VALUE=DATE") != std::string::npos;
    } else if (is(name, "DUE")) {
      bool isDate = false;
      const uint32_t due = parseIcalTime(value, &isDate);
      if (due != 0) {
        current.start = due;
        current.allDay = isDate || params.find("VALUE=DATE") != std::string::npos;
      }
    } else if (is(name, "DTEND")) {
      current.end = parseIcalTime(value);
    } else if (is(name, "LAST-MODIFIED")) {
      current.modified = parseIcalTime(value);
    } else if (is(name, "DTSTAMP") && current.modified == 0) {
      // Fallback only: LAST-MODIFIED is the edit time, DTSTAMP is when the
      // object was serialised, and a server sending both means the former.
      current.modified = parseIcalTime(value);
    }
  }
}

void parseVcard(const std::string& vcf, const std::string& collectionName, std::vector<Entry>& out) {
  const std::string text = unfoldIcal(vcf);

  Entry current;
  bool inCard = false;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t lineEnd = text.find('\n', pos);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    const std::string line = text.substr(pos, lineEnd - pos);
    const bool lastLine = lineEnd == text.size();
    pos = lineEnd + 1;
    if (line.empty()) {
      if (lastLine) break;
      continue;
    }

    std::string rawName, params, value;
    if (!splitProperty(line, rawName, params, value)) {
      if (lastLine) break;
      continue;
    }
    const std::string name = stripGroup(rawName);

    if (is(name, "BEGIN") && is(value, "VCARD")) {
      current = Entry{};
      current.kind = Kind::Contact;
      current.collection = collectionName;
      inCard = true;
      continue;
    }
    if (is(name, "END") && is(value, "VCARD")) {
      if (inCard && !current.title.empty()) out.push_back(current);
      inCard = false;
      continue;
    }
    if (!inCard) continue;

    if (is(name, "FN")) {
      current.title = unescapeIcalText(value);
    } else if (is(name, "N") && current.title.empty()) {
      // Structured name as a fallback: "Family;Given;...". Only used when FN
      // is absent, which vCard 4.0 forbids and 3.0 exporters still do.
      const size_t semi = value.find(';');
      const std::string family = value.substr(0, semi);
      std::string given;
      if (semi != std::string::npos) {
        const size_t second = value.find(';', semi + 1);
        given = value.substr(semi + 1, second == std::string::npos ? std::string::npos : second - semi - 1);
      }
      std::string joined = unescapeIcalText(given);
      if (!joined.empty() && !family.empty()) joined += " ";
      joined += unescapeIcalText(family);
      current.title = joined;
    } else if (is(name, "TEL") && current.phone.empty()) {
      current.phone = unescapeIcalText(value);
    } else if (is(name, "EMAIL") && current.email.empty()) {
      current.email = unescapeIcalText(value);
    } else if (is(name, "NOTE")) {
      current.body = unescapeIcalText(value);
    } else if (is(name, "REV")) {
      current.modified = parseIcalTime(value);
    }
  }
}

std::vector<DavResponse> parseMultistatus(const std::string& xml) {
  std::vector<DavResponse> out;
  DavResponse current;
  bool inResponse = false;

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
        inResponse = true;
      }
      continue;
    }
    if (!inResponse) continue;

    // <comp/> and <resourcetype>'s children are self-closing, so they are read
    // before the closing-tag skip rather than after it.
    if (name == "comp") {
      const std::string comp = tagNameAttribute(tag);
      if (is(comp, "VEVENT")) current.holdsEvents = true;
      if (is(comp, "VTODO")) current.holdsReminders = true;
      if (is(comp, "VJOURNAL")) current.holdsNotes = true;
      continue;
    }
    if (name == "addressbook") {
      current.isAddressBook = true;
      continue;
    }
    if (closing || selfClosing) continue;

    if (name == "href") {
      // The resource href comes first; a later one belongs to a property such
      // as calendar-home-set, and that is the address the caller wants next.
      current.href = unescapeXml(text);
    } else if (name == "displayname") {
      current.displayName = unescapeXml(text);
    } else if (name == "calendar-data" || name == "address-data") {
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

const char* propfindHomeSets() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
         "xmlns:a=\"urn:ietf:params:xml:ns:carddav\">"
         "<d:prop><c:calendar-home-set/><a:addressbook-home-set/></d:prop></d:propfind>";
}

const char* propfindCollections() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
         "<d:prop><d:displayname/><d:resourcetype/>"
         "<c:supported-calendar-component-set/></d:prop></d:propfind>";
}

std::string reportEventsQuery(const std::string& startUtc, const std::string& endUtc) {
  // <expand> is the whole reason this app has no RRULE engine: with it the
  // server returns one component per occurrence, already resolved, in UTC.
  std::string query =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:prop><d:getetag/><c:calendar-data><c:expand start=\"";
  query += startUtc;
  query += "\" end=\"";
  query += endUtc;
  query +=
      "\"/></c:calendar-data></d:prop>"
      "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
      "<c:time-range start=\"";
  query += startUtc;
  query += "\" end=\"";
  query += endUtc;
  query += "\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";
  return query;
}

std::string reportComponentQuery(const char* componentName) {
  std::string query =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:prop><d:getetag/><c:calendar-data/></d:prop>"
      "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"";
  query += componentName;
  query += "\"/></c:comp-filter></c:filter></c:calendar-query>";
  return query;
}

const char* reportContactsQuery() {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<a:addressbook-query xmlns:d=\"DAV:\" xmlns:a=\"urn:ietf:params:xml:ns:carddav\">"
         "<d:prop><d:getetag/><a:address-data/></d:prop></a:addressbook-query>";
}

std::string formatIcalUtc(uint32_t epoch) {
  const Civil c = civilFromEpoch(epoch);
  // Sized for what the FORMAT can emit (an unsigned per %u), not for the
  // four-digit year we expect: -Wformat-truncation reasons about the former.
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04u%02u%02uT%02u%02u00Z", static_cast<unsigned>(c.year),
                static_cast<unsigned>(c.month), static_cast<unsigned>(c.day), static_cast<unsigned>(c.hour),
                static_cast<unsigned>(c.minute));
  return std::string(buf);
}

std::string discoveryUrl(const std::string& serverInput, bool cardDav) {
  std::string url = serverInput;
  // A pasted address routinely carries a trailing space or newline.
  while (!url.empty() && isSpaceOrTab(url.front())) url.erase(url.begin());
  while (!url.empty() && (isSpaceOrTab(url.back()) || url.back() == '\n' || url.back() == '\r')) url.pop_back();
  if (url.empty()) return url;

  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) url = "https://" + url;

  const char* wellKnown = cardDav ? "/.well-known/carddav" : "/.well-known/caldav";

  // A bare host gets the RFC 6764 well-known path; a host with a path is taken
  // as given, because that is how somebody points at their own server.
  const size_t schemeEnd = url.find("://");
  const size_t hostEnd = url.find('/', schemeEnd + 3);
  if (hostEnd == std::string::npos) return url + wellKnown;
  if (hostEnd == url.size() - 1) return url.substr(0, hostEnd) + wellKnown;
  return url;
}

void sortByStart(std::vector<Entry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.start != b.start) return a.start < b.start;
    // Within one instant, an all-day entry heads the day it belongs to.
    if (a.allDay != b.allDay) return a.allDay;
    return a.title < b.title;
  });
}

void sortReminders(std::vector<Entry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.done != b.done) return !a.done;
    if (a.start != b.start) {
      // Undated last: sorting a missing due date to 1970 would bury every
      // reminder that actually has one.
      if (a.start == 0) return false;
      if (b.start == 0) return true;
      return a.start < b.start;
    }
    return a.title < b.title;
  });
}

void sortByTitle(std::vector<Entry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    const size_t n = std::min(a.title.size(), b.title.size());
    for (size_t i = 0; i < n; ++i) {
      const int ca = std::tolower(static_cast<unsigned char>(a.title[i]));
      const int cb = std::tolower(static_cast<unsigned char>(b.title[i]));
      if (ca != cb) return ca < cb;
    }
    return a.title.size() < b.title.size();
  });
}

Civil civilFromEpoch(uint32_t epoch) {
  Civil c;
  const uint32_t days = epoch / SECONDS_PER_DAY;
  const uint32_t rem = epoch % SECONDS_PER_DAY;
  c.hour = static_cast<uint8_t>(rem / SECONDS_PER_HOUR);
  c.minute = static_cast<uint8_t>((rem % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
  // 1970-01-01 was a Thursday, and day 0 must land on it.
  c.weekday = static_cast<uint8_t>((days + 4) % 7);

  // civil_from_days, the inverse of the arithmetic in parseIcalTime.
  const long long z = static_cast<long long>(days) + 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned long long doe = static_cast<unsigned long long>(z - era * 146097);
  const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long y = static_cast<long long>(yoe) + era * 400;
  const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned long long mp = (5 * doy + 2) / 153;
  const unsigned long long d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned long long m = mp < 10 ? mp + 3 : mp - 9;
  c.year = static_cast<uint16_t>(y + (m <= 2 ? 1 : 0));
  c.month = static_cast<uint8_t>(m);
  c.day = static_cast<uint8_t>(d);
  return c;
}

uint32_t startOfDay(uint32_t epoch) { return epoch - (epoch % SECONDS_PER_DAY); }

std::string formatTime(uint32_t epoch) {
  const Civil c = civilFromEpoch(epoch);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(c.hour), static_cast<unsigned>(c.minute));
  return std::string(buf);
}

std::string formatDayHeading(uint32_t epoch) {
  const Civil c = civilFromEpoch(epoch);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s %u %s", kWeekdays[c.weekday % 7], static_cast<unsigned>(c.day),
                kMonths[(c.month - 1) % 12]);
  return std::string(buf);
}

std::string formatDayLabel(uint32_t epoch, uint32_t nowEpoch) {
  const uint32_t today = startOfDay(nowEpoch);
  const uint32_t day = startOfDay(epoch);
  if (day == today) return "Today";
  if (day == today + SECONDS_PER_DAY) return "Tomorrow";
  return formatDayHeading(epoch);
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

std::string firstLine(const std::string& body) {
  size_t start = 0;
  while (start < body.size() && (body[start] == '\n' || body[start] == '\r' || isSpaceOrTab(body[start]))) ++start;
  size_t end = body.find('\n', start);
  if (end == std::string::npos) end = body.size();
  while (end > start && (body[end - 1] == '\r' || isSpaceOrTab(body[end - 1]))) --end;
  return body.substr(start, end - start);
}

}  // namespace dav
