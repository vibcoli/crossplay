// NotesCore: the iCalendar parser, the multistatus reader, and the small
// presentation helpers. Everything here is freestanding, so a failure names a
// parser bug rather than a device problem.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/notes/NotesCore.h"

static int checks = 0;
static int failed = 0;

static void ok() { checks++; }
static void bad(const std::string& what) {
  checks++;
  failed++;
  std::printf("  FAIL notes  %s\n", what.c_str());
}
static void expectEq(const std::string& got, const std::string& want, const std::string& what) {
  if (got == want) {
    ok();
  } else {
    bad(what + " -- got \"" + got + "\", want \"" + want + "\"");
  }
}
static void expectTrue(bool cond, const std::string& what) {
  if (cond) {
    ok();
  } else {
    bad(what);
  }
}

// ---------------------------------------------------------------------------
// Folding. Servers fold at 75 octets, and they fold mid-word and mid-UTF-8.
// ---------------------------------------------------------------------------
static void testUnfolding() {
  expectEq(notes::unfoldIcal("SUMMARY:Ein\r\n kauf"), "SUMMARY:Einkauf", "CRLF + space is a continuation");
  expectEq(notes::unfoldIcal("SUMMARY:Ein\n\tkauf"), "SUMMARY:Einkauf", "LF + tab is a continuation");
  // CRLF + tab is the combination real servers send and the one a space-only
  // check misses: this case was added after a mutant that broke exactly it
  // survived the suite.
  expectEq(notes::unfoldIcal("SUMMARY:Ein\r\n\tkauf"), "SUMMARY:Einkauf", "CRLF + tab is a continuation");
  expectEq(notes::unfoldIcal("SUMMARY:Ein\n kauf"), "SUMMARY:Einkauf", "LF + space is a continuation");
  expectEq(notes::unfoldIcal("A:1\r\nB:2"), "A:1\nB:2", "a real break survives");

  // A fold splitting a two-byte character must rejoin it byte for byte; the
  // panel draws a replacement glyph otherwise.
  const std::string split = "SUMMARY:Gr\xC3\r\n \xBC\x6E";  // "Grün" folded inside the ü
  expectEq(notes::unfoldIcal(split), "SUMMARY:Gr\xC3\xBC\x6E", "a fold inside a UTF-8 sequence rejoins");
}

// ---------------------------------------------------------------------------
// TEXT escaping.
// ---------------------------------------------------------------------------
static void testUnescaping() {
  expectEq(notes::unescapeIcalText("Milch\\, Brot"), "Milch, Brot", "escaped comma");
  expectEq(notes::unescapeIcalText("Zeile1\\nZeile2"), "Zeile1\nZeile2", "lowercase \\n is a newline");
  expectEq(notes::unescapeIcalText("Zeile1\\NZeile2"), "Zeile1\nZeile2", "uppercase \\N is a newline too");
  expectEq(notes::unescapeIcalText("C:\\\\Pfad"), "C:\\Pfad", "escaped backslash");
  expectEq(notes::unescapeIcalText("ends with\\"), "ends with\\", "a trailing backslash is not a crash");
}

// ---------------------------------------------------------------------------
// Components.
// ---------------------------------------------------------------------------
static void testParseVjournal() {
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "VERSION:2.0\r\n"
      "BEGIN:VJOURNAL\r\n"
      "UID:note-1\r\n"
      "SUMMARY:Einkaufsliste\r\n"
      "DESCRIPTION:Milch\\, Brot\\nEier\r\n"
      "LAST-MODIFIED:20260913T170721Z\r\n"
      "END:VJOURNAL\r\n"
      "END:VCALENDAR\r\n";
  std::vector<notes::Note> out;
  notes::parseIcal(ical, "Notizen", out);

  if (out.size() != 1) {
    bad("one VJOURNAL should give one note, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectEq(out[0].uid, "note-1", "UID");
  expectEq(out[0].title, "Einkaufsliste", "SUMMARY");
  expectEq(out[0].body, "Milch, Brot\nEier", "DESCRIPTION is unescaped");
  expectEq(out[0].calendar, "Notizen", "collection name is carried onto the note");
  // 2026-09-13T17:07:21Z
  expectTrue(out[0].modified == 1789319241u, "LAST-MODIFIED became an epoch");
}

static void testParseVtodo() {
  // What iCloud actually returns for a reminder, which is the only thing on
  // that account with free text in it.
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VTODO\r\n"
      "UID:todo-1\r\n"
      "SUMMARY:Zahnarzt anrufen\r\n"
      "DESCRIPTION:Nummer steht im Kalender\r\n"
      "STATUS:COMPLETED\r\n"
      "DTSTAMP:20260101T000000Z\r\n"
      "END:VTODO\r\n"
      "END:VCALENDAR\r\n";
  std::vector<notes::Note> out;
  notes::parseIcal(ical, "Erinnerungen", out);

  if (out.size() != 1) {
    bad("one VTODO should give one note, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectEq(out[0].title, "Zahnarzt anrufen", "VTODO SUMMARY");
  expectTrue(out[0].done, "STATUS:COMPLETED marks the note done");
  expectTrue(out[0].modified != 0, "DTSTAMP stands in when LAST-MODIFIED is absent");
}

static void testMultipleComponentsAndTimezone() {
  // One calendar-data payload routinely holds a VTIMEZONE beside the component,
  // and the VTIMEZONE carries its own nested components. Treating the object as
  // a single component reads the timezone's fields onto the note.
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VTIMEZONE\r\n"
      "TZID:Europe/Berlin\r\n"
      "BEGIN:STANDARD\r\n"
      "TZNAME:CET\r\n"
      "END:STANDARD\r\n"
      "END:VTIMEZONE\r\n"
      "BEGIN:VJOURNAL\r\n"
      "UID:a\r\n"
      "SUMMARY:Erste\r\n"
      "END:VJOURNAL\r\n"
      "BEGIN:VJOURNAL\r\n"
      "UID:b\r\n"
      "SUMMARY:Zweite\r\n"
      "END:VJOURNAL\r\n"
      "END:VCALENDAR\r\n";
  std::vector<notes::Note> out;
  notes::parseIcal(ical, "Notizen", out);

  if (out.size() != 2) {
    bad("two journals beside a VTIMEZONE should give two notes, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectEq(out[0].title, "Erste", "first component");
  expectEq(out[1].title, "Zweite", "second component");
  expectTrue(out[0].title != "CET" && out[1].title != "CET", "the VTIMEZONE did not leak into a note");
}

static void testEmptyAndTitleless() {
  // A reminder someone created and never typed into draws as a blank row.
  std::vector<notes::Note> out;
  notes::parseIcal("BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:x\r\nEND:VTODO\r\nEND:VCALENDAR\r\n", "K", out);
  expectTrue(out.empty(), "a component with neither summary nor body is dropped");

  // A body with no summary still has something to title the row with.
  out.clear();
  notes::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VJOURNAL\r\nUID:y\r\nDESCRIPTION:Nur Text\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n", "K",
      out);
  if (out.size() != 1) {
    bad("a body-only note should survive");
    return;
  }
  ok();
  expectEq(out[0].title, "Nur Text", "a body-only note is titled from its first line");
}

static void testPropertyParameters() {
  std::vector<notes::Note> out;
  notes::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VJOURNAL\r\nUID:p\r\nSUMMARY;LANGUAGE=de:Mit "
      "Parameter\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n",
      "K", out);
  if (out.size() != 1) {
    bad("a property with a parameter should still parse");
    return;
  }
  ok();
  expectEq(out[0].title, "Mit Parameter", "parameters are dropped, the value is not");

  // A quoted parameter value may hold a colon; splitting on the first colon
  // would cut the property name in half.
  out.clear();
  notes::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VJOURNAL\r\nUID:q\r\nSUMMARY;X-A=\"a:b\":Echt\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n",
      "K", out);
  if (out.size() != 1) {
    bad("a quoted parameter containing a colon should still parse");
    return;
  }
  ok();
  expectEq(out[0].title, "Echt", "the split ignores colons inside quotes");
}

static void testLowercaseKeywords() {
  // RFC 5545 property and component names are case-insensitive.
  std::vector<notes::Note> out;
  notes::parseIcal("begin:VCALENDAR\r\nbegin:vjournal\r\nsummary:Klein\r\nend:vjournal\r\nend:VCALENDAR\r\n", "K", out);
  expectTrue(out.size() == 1 && out[0].title == "Klein", "lowercase BEGIN/END/SUMMARY parse");
}

// ---------------------------------------------------------------------------
// WebDAV
// ---------------------------------------------------------------------------
static void testMultistatus() {
  const std::string xml =
      "<?xml version=\"1.0\"?>"
      "<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
      "<D:response><D:href>/cal/notes/</D:href><D:propstat><D:prop>"
      "<D:displayname>Notizen &amp; Listen</D:displayname>"
      "<C:supported-calendar-component-set><C:comp name=\"VJOURNAL\"/></C:supported-calendar-component-set>"
      "</D:prop></D:propstat></D:response>"
      "<D:response><D:href>/cal/events/</D:href><D:propstat><D:prop>"
      "<D:displayname>Termine</D:displayname>"
      "<C:supported-calendar-component-set><C:comp name=\"VEVENT\"/></C:supported-calendar-component-set>"
      "</D:prop></D:propstat></D:response>"
      "</D:multistatus>";
  const auto responses = notes::parseMultistatus(xml);
  if (responses.size() != 2) {
    bad("two responses expected, got " + std::to_string(responses.size()));
    return;
  }
  ok();
  expectEq(responses[0].href, "/cal/notes/", "href");
  expectEq(responses[0].displayName, "Notizen & Listen", "displayname entities are resolved");
  expectTrue(responses[0].holdsNotes, "a VJOURNAL collection is a notes collection");
  expectTrue(!responses[1].holdsNotes, "a VEVENT-only collection is not");
}

static void testMultistatusPrefixes() {
  // A server may use a different prefix, or none at all with a default xmlns.
  const std::string xml =
      "<multistatus xmlns=\"DAV:\"><response><href>/p/</href>"
      "<propstat><prop><displayname>Ohne Prefix</displayname></prop></propstat>"
      "</response></multistatus>";
  const auto responses = notes::parseMultistatus(xml);
  expectTrue(responses.size() == 1 && responses[0].displayName == "Ohne Prefix",
             "an unprefixed multistatus parses the same");
}

static void testCalendarData() {
  const std::string xml =
      "<d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:response><d:href>/cal/notes/1.ics</d:href><d:propstat><d:prop>"
      "<c:calendar-data>BEGIN:VCALENDAR&#13;&#10;BEGIN:VJOURNAL&#13;&#10;SUMMARY:Aus dem "
      "XML&#13;&#10;END:VJOURNAL&#13;&#10;END:VCALENDAR</c:calendar-data>"
      "</d:prop></d:propstat></d:response></d:multistatus>";
  const auto responses = notes::parseMultistatus(xml);
  if (responses.size() != 1) {
    bad("one calendar-data response expected");
    return;
  }
  ok();
  expectTrue(responses[0].calendarData.find("SUMMARY:Aus dem XML") != std::string::npos,
             "calendar-data survives XML unescaping");

  std::vector<notes::Note> out;
  notes::parseIcal(responses[0].calendarData, "Notizen", out);
  expectTrue(out.size() == 1 && out[0].title == "Aus dem XML", "the extracted payload parses as iCalendar");
}

static void testHrefResolution() {
  expectEq(notes::resolveHref("https://caldav.icloud.com/.well-known/caldav", "/1234/principal/"),
           "https://caldav.icloud.com/1234/principal/", "an absolute path takes the origin");
  expectEq(notes::resolveHref("https://a.example/x/y", "https://b.example/z"), "https://b.example/z",
           "a full URL is returned as is");
  expectEq(notes::resolveHref("https://a.example/home/", "cal/"), "https://a.example/home/cal/",
           "a relative href resolves against the directory");
}

static void testDiscoveryUrl() {
  expectEq(notes::discoveryUrl("icloud.com"), "https://icloud.com/.well-known/caldav", "a bare host gets a scheme");
  expectEq(notes::discoveryUrl("https://caldav.icloud.com"), "https://caldav.icloud.com/.well-known/caldav",
           "a bare URL gets the well-known path");
  expectEq(notes::discoveryUrl("https://cloud.example/remote.php/dav/"), "https://cloud.example/remote.php/dav/",
           "a URL with a path is taken as given");
  expectEq(notes::discoveryUrl("  icloud.com \n"), "https://icloud.com/.well-known/caldav",
           "surrounding space is trimmed");
}

static void testRequestBodies() {
  const std::string query = notes::reportNotesQuery("VJOURNAL");
  expectTrue(query.find("calendar-query") != std::string::npos, "the REPORT body is a calendar-query");
  expectTrue(query.find("name=\"VJOURNAL\"") != std::string::npos, "the component filter names the component");
  expectTrue(query.find("calendar-data") != std::string::npos, "the REPORT asks for the data, not just etags");
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------
static void testSorting() {
  std::vector<notes::Note> list;
  notes::Note a;
  a.title = "alt";
  a.modified = 1000;
  notes::Note b;
  b.title = "neu";
  b.modified = 2000;
  notes::Note c;
  c.title = "ohne Zeit";
  c.modified = 0;
  list = {a, c, b};
  notes::sortNewestFirst(list);
  expectEq(list[0].title, "neu", "newest first");
  expectEq(list[1].title, "alt", "then older");
  expectEq(list[2].title, "ohne Zeit", "a note with no timestamp goes last, not first");
}

static void testAge() {
  expectTrue(notes::ageSince(1000, 1030).unit == notes::AgeUnit::JustNow, "under a minute is just now");
  const auto minutes = notes::ageSince(1000, 1000 + 5 * 60);
  expectTrue(minutes.unit == notes::AgeUnit::Minutes && minutes.value == 5, "five minutes");
  const auto hours = notes::ageSince(0 + 1, 1 + 3 * 3600);
  expectTrue(hours.unit == notes::AgeUnit::Hours && hours.value == 3, "three hours");
  const auto days = notes::ageSince(1, 1 + 2 * 86400);
  expectTrue(days.unit == notes::AgeUnit::Days && days.value == 2, "two days");
  // A device clock behind the server's must not render as a negative age.
  expectTrue(notes::ageSince(2000, 1000).unit == notes::AgeUnit::JustNow, "a future timestamp reads as just now");
  expectTrue(notes::ageSince(0, 99999).unit == notes::AgeUnit::JustNow, "a missing timestamp has no age");
}

static void testPreview() {
  expectEq(notes::previewLine("Erste Zeile\nZweite", 40), "Erste Zeile", "the preview is the first line");
  expectEq(notes::previewLine("", 40), "", "an empty body gives an empty preview");
  expectEq(notes::previewLine("   \n  Text", 40), "Text", "leading blank lines are skipped");

  // Clipping must land on a character boundary.
  const std::string umlauts = "ääääääää";  // eight two-byte characters
  const std::string clipped = notes::previewLine(umlauts, 5);
  expectTrue(clipped.size() >= 3 && clipped.substr(clipped.size() - 3) == "...",
             "a long line is clipped with an ellipsis");
  const std::string body = clipped.substr(0, clipped.size() - 3);
  expectTrue(body.size() % 2 == 0, "the clip did not cut a UTF-8 sequence in half");
}

int main() {
  testUnfolding();
  testUnescaping();
  testParseVjournal();
  testParseVtodo();
  testMultipleComponentsAndTimezone();
  testEmptyAndTitleless();
  testPropertyParameters();
  testLowercaseKeywords();
  testMultistatus();
  testMultistatusPrefixes();
  testCalendarData();
  testHrefResolution();
  testDiscoveryUrl();
  testRequestBodies();
  testSorting();
  testAge();
  testPreview();

  std::printf("  %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
