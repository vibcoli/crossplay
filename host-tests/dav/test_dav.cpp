// DavCore: the iCalendar and vCard parsers, the multistatus reader, and the
// date helpers. Everything here is freestanding, so a failure names a parsing
// bug rather than a device problem.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/dav/DavCore.h"

static int checks = 0;
static int failed = 0;

static void ok() { checks++; }
static void bad(const std::string& what) {
  checks++;
  failed++;
  std::printf("  FAIL dav  %s\n", what.c_str());
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

// 2026-09-14T09:00:00Z, the instant every relative case below is written
// against. Fixed rather than taken from the clock: a suite whose answer moves
// with the wall clock is a suite that fails on one Tuesday a year.
static constexpr uint32_t kNow = 1789376400u;

// ---------------------------------------------------------------------------
// Folding. Servers fold at 75 octets, and they fold mid-word and mid-UTF-8.
// ---------------------------------------------------------------------------
static void testUnfolding() {
  expectEq(dav::unfoldIcal("SUMMARY:Ein\r\n kauf"), "SUMMARY:Einkauf", "CRLF + space is a continuation");
  expectEq(dav::unfoldIcal("SUMMARY:Ein\n\tkauf"), "SUMMARY:Einkauf", "LF + tab is a continuation");
  // CRLF + tab is the combination real servers send and the one a space-only
  // check misses: this case was added after a mutant that broke exactly it
  // survived the suite.
  expectEq(dav::unfoldIcal("SUMMARY:Ein\r\n\tkauf"), "SUMMARY:Einkauf", "CRLF + tab is a continuation");
  expectEq(dav::unfoldIcal("SUMMARY:Ein\n kauf"), "SUMMARY:Einkauf", "LF + space is a continuation");
  expectEq(dav::unfoldIcal("A:1\r\nB:2"), "A:1\nB:2", "a real break survives");

  const std::string split = "SUMMARY:Gr\xC3\r\n \xBC\x6E";  // "Grün" folded inside the ü
  expectEq(dav::unfoldIcal(split), "SUMMARY:Gr\xC3\xBC\x6E", "a fold inside a UTF-8 sequence rejoins");
}

static void testUnescaping() {
  expectEq(dav::unescapeIcalText("Milch\\, Brot"), "Milch, Brot", "escaped comma");
  expectEq(dav::unescapeIcalText("Zeile1\\nZeile2"), "Zeile1\nZeile2", "lowercase \\n is a newline");
  expectEq(dav::unescapeIcalText("Zeile1\\NZeile2"), "Zeile1\nZeile2", "uppercase \\N is a newline too");
  expectEq(dav::unescapeIcalText("C:\\\\Pfad"), "C:\\Pfad", "escaped backslash");
  expectEq(dav::unescapeIcalText("ends with\\"), "ends with\\", "a trailing backslash is not a crash");
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static void testTimedEvent() {
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:ev-1\r\n"
      "SUMMARY:Zahnarzt\r\n"
      "LOCATION:Hauptstr. 3\r\n"
      "DTSTART:20260914T090000Z\r\n"
      "DTEND:20260914T094500Z\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  std::vector<dav::Entry> out;
  dav::parseIcal(ical, "Privat", out);
  if (out.size() != 1) {
    bad("one VEVENT should give one entry, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectTrue(out[0].kind == dav::Kind::Event, "it is an event");
  expectEq(out[0].title, "Zahnarzt", "SUMMARY");
  expectEq(out[0].location, "Hauptstr. 3", "LOCATION");
  expectTrue(out[0].start == kNow, "DTSTART became an epoch");
  expectTrue(out[0].end == kNow + 45 * 60, "DTEND became an epoch");
  expectTrue(!out[0].allDay, "a timed event is not all-day");
  expectEq(out[0].collection, "Privat", "the collection name is carried onto the entry");
}

static void testAllDayEvent() {
  // An all-day event is a DATE, not midnight: rendering it as 00:00 is the
  // single most common calendar-client tell.
  std::vector<dav::Entry> out;
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:a\r\nSUMMARY:Urlaub\r\nDTSTART;VALUE=DATE:20260914\r\nEND:VEVENT\r\nEND:"
      "VCALENDAR\r\n",
      "Privat", out);
  expectTrue(out.size() == 1 && out[0].allDay, "VALUE=DATE marks the event all-day");

  // The bare eight-digit form, with no VALUE parameter, is what several
  // servers actually send and has to count the same.
  out.clear();
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:b\r\nSUMMARY:Feiertag\r\nDTSTART:20260914\r\nEND:VEVENT\r\nEND:"
      "VCALENDAR\r\n",
      "Privat", out);
  expectTrue(out.size() == 1 && out[0].allDay, "a bare DATE is all-day without the parameter");
}

static void testExpandedRecurrence() {
  // What a server returns for <expand>: one VEVENT per occurrence, sharing a
  // UID, each with its own DTSTART. The app has no RRULE engine by design, so
  // this shape is the whole contract.
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\nUID:r\r\nSUMMARY:Standup\r\nDTSTART:20260914T080000Z\r\nRECURRENCE-ID:20260914T080000Z\r\nEND:"
      "VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:r\r\nSUMMARY:Standup\r\nDTSTART:20260915T080000Z\r\nRECURRENCE-ID:20260915T080000Z\r\nEND:"
      "VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:r\r\nSUMMARY:Standup\r\nDTSTART:20260916T080000Z\r\nRECURRENCE-ID:20260916T080000Z\r\nEND:"
      "VEVENT\r\n"
      "END:VCALENDAR\r\n";
  std::vector<dav::Entry> out;
  dav::parseIcal(ical, "Arbeit", out);
  if (out.size() != 3) {
    bad("an expanded recurrence should give one entry per occurrence, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectTrue(out[0].start != out[1].start && out[1].start != out[2].start,
             "each occurrence keeps its own start rather than collapsing onto one");
  expectEq(out[2].title, "Standup", "the third occurrence is intact");
}

static void testVtimezoneDoesNotLeak() {
  // A payload carries a VTIMEZONE beside the component, with nested STANDARD
  // and DAYLIGHT parts that have their own names.
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VTIMEZONE\r\nTZID:Europe/Berlin\r\nBEGIN:DAYLIGHT\r\nTZNAME:CEST\r\nDTSTART:19700329T020000\r\nEND:"
      "DAYLIGHT\r\nEND:VTIMEZONE\r\n"
      "BEGIN:VEVENT\r\nUID:x\r\nSUMMARY:Termin\r\nDTSTART:20260914T120000Z\r\nEND:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  std::vector<dav::Entry> out;
  dav::parseIcal(ical, "Privat", out);
  if (out.size() != 1) {
    bad("the VTIMEZONE produced an entry of its own, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectEq(out[0].title, "Termin", "the event is the one that survived");
  expectTrue(out[0].start == 1789387200u, "the VTIMEZONE's own DTSTART did not overwrite the event's");
}

// ---------------------------------------------------------------------------
// Reminders
// ---------------------------------------------------------------------------
static void testReminder() {
  const std::string ical =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VTODO\r\nUID:t1\r\nSUMMARY:Zahnarzt anrufen\r\nDESCRIPTION:Nummer steht im "
      "Kalender\r\nDUE:20260915T100000Z\r\nEND:VTODO\r\n"
      "END:VCALENDAR\r\n";
  std::vector<dav::Entry> out;
  dav::parseIcal(ical, "Erinnerungen", out);
  if (out.size() != 1) {
    bad("one VTODO should give one entry");
    return;
  }
  ok();
  expectTrue(out[0].kind == dav::Kind::Reminder, "it is a reminder");
  expectEq(out[0].title, "Zahnarzt anrufen", "VTODO SUMMARY");
  expectTrue(out[0].start == 1789466400u, "DUE became the entry's start");
  expectTrue(!out[0].done, "it is not done");
}

static void testReminderDone() {
  std::vector<dav::Entry> out;
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:t\r\nSUMMARY:Erledigt\r\nSTATUS:COMPLETED\r\nEND:VTODO\r\nEND:"
      "VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].done, "STATUS:COMPLETED marks it done");

  // A VTODO may record a completion time and never set STATUS at all.
  out.clear();
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:t\r\nSUMMARY:Auch "
      "erledigt\r\nCOMPLETED:20260913T100000Z\r\nEND:VTODO\r\nEND:VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].done, "a COMPLETED timestamp marks it done without STATUS");
}

static void testEmptyComponentDropped() {
  std::vector<dav::Entry> out;
  dav::parseIcal("BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:x\r\nEND:VTODO\r\nEND:VCALENDAR\r\n", "K", out);
  expectTrue(out.empty(), "a component with neither summary nor body is dropped");

  out.clear();
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VJOURNAL\r\nUID:y\r\nDESCRIPTION:Nur Text\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n", "K",
      out);
  expectTrue(out.size() == 1 && out[0].title == "Nur Text" && out[0].kind == dav::Kind::Note,
             "a body-only journal is titled from its first line");
}

static void testPropertyParameters() {
  std::vector<dav::Entry> out;
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:p\r\nSUMMARY;LANGUAGE=de:Mit "
      "Parameter\r\nDTSTART:20260914T090000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].title == "Mit Parameter", "parameters are dropped, the value is not");

  // A quoted parameter value may hold a colon; splitting on the first colon
  // would cut the property name in half.
  out.clear();
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:q\r\nSUMMARY;X-A=\"a:b\":Echt\r\nEND:VEVENT\r\nEND:"
      "VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].title == "Echt", "the split ignores colons inside quotes");

  // TZID is a parameter this app deliberately does not honour, but its colon
  // must not break the split either.
  out.clear();
  dav::parseIcal(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:z\r\nSUMMARY:Mit "
      "Zone\r\nDTSTART;TZID=Europe/Berlin:20260914T090000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].start != 0, "a TZID-qualified DTSTART still yields a time");
}

static void testLowercaseKeywords() {
  std::vector<dav::Entry> out;
  dav::parseIcal(
      "begin:VCALENDAR\r\nbegin:vevent\r\nsummary:Klein\r\ndtstart:20260914T090000Z\r\nend:vevent\r\nend:"
      "VCALENDAR\r\n",
      "K", out);
  expectTrue(out.size() == 1 && out[0].title == "Klein", "lowercase BEGIN/END/SUMMARY parse");
}

// ---------------------------------------------------------------------------
// Contacts
// ---------------------------------------------------------------------------
static void testVcard() {
  const std::string vcf =
      "BEGIN:VCARD\r\n"
      "VERSION:3.0\r\n"
      "FN:Anna Schmidt\r\n"
      "N:Schmidt;Anna;;;\r\n"
      "TEL;TYPE=CELL:+49 170 1234567\r\n"
      "EMAIL;TYPE=INTERNET:anna@example.org\r\n"
      "NOTE:Nachbarin\r\n"
      "END:VCARD\r\n";
  std::vector<dav::Entry> out;
  dav::parseVcard(vcf, "Kontakte", out);
  if (out.size() != 1) {
    bad("one VCARD should give one entry, got " + std::to_string(out.size()));
    return;
  }
  ok();
  expectTrue(out[0].kind == dav::Kind::Contact, "it is a contact");
  expectEq(out[0].title, "Anna Schmidt", "FN is the name");
  expectEq(out[0].phone, "+49 170 1234567", "TEL");
  expectEq(out[0].email, "anna@example.org", "EMAIL");
  expectEq(out[0].body, "Nachbarin", "NOTE");
}

static void testVcardFallbacksAndGroups() {
  // No FN: vCard 4.0 forbids it, 3.0 exporters still do it.
  std::vector<dav::Entry> out;
  dav::parseVcard("BEGIN:VCARD\r\nN:Müller;Bernd;;;\r\nEND:VCARD\r\n", "K", out);
  expectTrue(out.size() == 1 && out[0].title == "Bernd Müller", "N stands in for a missing FN");

  // A group prefix carries no meaning and must not hide the property.
  out.clear();
  dav::parseVcard("BEGIN:VCARD\r\nFN:Chris\r\nitem1.TEL:+1 555 0100\r\nEND:VCARD\r\n", "K", out);
  expectTrue(out.size() == 1 && out[0].phone == "+1 555 0100", "a group-prefixed TEL is still a TEL");

  // The first of several is the one on the card.
  out.clear();
  dav::parseVcard("BEGIN:VCARD\r\nFN:Dana\r\nTEL:111\r\nTEL:222\r\nEND:VCARD\r\n", "K", out);
  expectTrue(out.size() == 1 && out[0].phone == "111", "the first TEL wins, the second does not overwrite it");

  // A card with no name at all would draw as a blank row.
  out.clear();
  dav::parseVcard("BEGIN:VCARD\r\nTEL:333\r\nEND:VCARD\r\n", "K", out);
  expectTrue(out.empty(), "a nameless card is dropped");
}

static void testMultipleVcards() {
  std::vector<dav::Entry> out;
  dav::parseVcard("BEGIN:VCARD\r\nFN:Eins\r\nEND:VCARD\r\nBEGIN:VCARD\r\nFN:Zwei\r\nEND:VCARD\r\n", "K", out);
  expectTrue(out.size() == 2, "two cards in one payload give two entries");
}

// ---------------------------------------------------------------------------
// WebDAV
// ---------------------------------------------------------------------------
static void testMultistatusCollections() {
  const std::string xml =
      "<?xml version=\"1.0\"?>"
      "<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
      "<D:response><D:href>/cal/home/</D:href><D:propstat><D:prop>"
      "<D:displayname>Privat &amp; Familie</D:displayname>"
      "<C:supported-calendar-component-set><C:comp name=\"VEVENT\"/></C:supported-calendar-component-set>"
      "</D:prop></D:propstat></D:response>"
      "<D:response><D:href>/cal/todo/</D:href><D:propstat><D:prop>"
      "<D:displayname>Erinnerungen</D:displayname>"
      "<C:supported-calendar-component-set><C:comp name=\"VTODO\"/></C:supported-calendar-component-set>"
      "</D:prop></D:propstat></D:response>"
      "</D:multistatus>";
  const auto responses = dav::parseMultistatus(xml);
  if (responses.size() != 2) {
    bad("two responses expected, got " + std::to_string(responses.size()));
    return;
  }
  ok();
  expectEq(responses[0].href, "/cal/home/", "href");
  expectEq(responses[0].displayName, "Privat & Familie", "displayname entities are resolved");
  expectTrue(responses[0].holdsEvents && !responses[0].holdsReminders, "a VEVENT collection holds events only");
  expectTrue(responses[1].holdsReminders && !responses[1].holdsEvents, "a VTODO collection holds reminders only");
}

static void testAddressBookDetection() {
  const std::string xml =
      "<d:multistatus xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\">"
      "<d:response><d:href>/card/default/</d:href><d:propstat><d:prop>"
      "<d:displayname>Kontakte</d:displayname>"
      "<d:resourcetype><d:collection/><card:addressbook/></d:resourcetype>"
      "</d:prop></d:propstat></d:response></d:multistatus>";
  const auto responses = dav::parseMultistatus(xml);
  expectTrue(responses.size() == 1 && responses[0].isAddressBook, "an addressbook resourcetype is recognised");
}

static void testMultistatusPrefixes() {
  const std::string xml =
      "<multistatus xmlns=\"DAV:\"><response><href>/p/</href>"
      "<propstat><prop><displayname>Ohne Prefix</displayname></prop></propstat>"
      "</response></multistatus>";
  const auto responses = dav::parseMultistatus(xml);
  expectTrue(responses.size() == 1 && responses[0].displayName == "Ohne Prefix",
             "an unprefixed multistatus parses the same");
}

static void testCalendarAndAddressData() {
  const std::string xml =
      "<d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:response><d:href>/cal/1.ics</d:href><d:propstat><d:prop>"
      "<c:calendar-data>BEGIN:VCALENDAR&#13;&#10;BEGIN:VEVENT&#13;&#10;SUMMARY:Aus dem "
      "XML&#13;&#10;DTSTART:20260914T090000Z&#13;&#10;END:VEVENT&#13;&#10;END:VCALENDAR</c:calendar-data>"
      "</d:prop></d:propstat></d:response></d:multistatus>";
  const auto responses = dav::parseMultistatus(xml);
  if (responses.size() != 1) {
    bad("one calendar-data response expected");
    return;
  }
  ok();
  std::vector<dav::Entry> out;
  dav::parseIcal(responses[0].calendarData, "Privat", out);
  expectTrue(out.size() == 1 && out[0].title == "Aus dem XML", "the extracted payload parses as iCalendar");

  // address-data lands in the same field, so contacts need no second path.
  const std::string cardXml =
      "<d:multistatus xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\">"
      "<d:response><d:href>/card/1.vcf</d:href><d:propstat><d:prop>"
      "<card:address-data>BEGIN:VCARD&#13;&#10;FN:Aus dem XML&#13;&#10;END:VCARD</card:address-data>"
      "</d:prop></d:propstat></d:response></d:multistatus>";
  const auto cardResponses = dav::parseMultistatus(cardXml);
  if (cardResponses.size() != 1) {
    bad("one address-data response expected");
    return;
  }
  ok();
  std::vector<dav::Entry> cards;
  dav::parseVcard(cardResponses[0].calendarData, "Kontakte", cards);
  expectTrue(cards.size() == 1 && cards[0].title == "Aus dem XML", "address-data parses as vCard");
}

static void testHrefResolution() {
  expectEq(dav::resolveHref("https://caldav.icloud.com/.well-known/caldav", "/1234/principal/"),
           "https://caldav.icloud.com/1234/principal/", "an absolute path takes the origin");
  expectEq(dav::resolveHref("https://a.example/x/y", "https://b.example/z"), "https://b.example/z",
           "a full URL is returned as is");
  expectEq(dav::resolveHref("https://a.example/home/", "cal/"), "https://a.example/home/cal/",
           "a relative href resolves against the directory");
}

static void testDiscoveryUrl() {
  expectEq(dav::discoveryUrl("icloud.com", false), "https://icloud.com/.well-known/caldav",
           "a bare host gets a scheme and the caldav path");
  expectEq(dav::discoveryUrl("icloud.com", true), "https://icloud.com/.well-known/carddav",
           "carddav asks for its own well-known path");
  expectEq(dav::discoveryUrl("https://cloud.example/remote.php/dav/", false), "https://cloud.example/remote.php/dav/",
           "a URL with a path is taken as given");
  expectEq(dav::discoveryUrl("  icloud.com \n", false), "https://icloud.com/.well-known/caldav",
           "surrounding space is trimmed");
}

static void testRequestBodies() {
  const std::string events = dav::reportEventsQuery("20260914T000000Z", "20261014T000000Z");
  expectTrue(events.find("<c:expand start=\"20260914T000000Z\" end=\"20261014T000000Z\"/>") != std::string::npos,
             "the agenda REPORT asks the server to expand recurrences");
  expectTrue(events.find("time-range") != std::string::npos, "and filters to the same window");
  expectTrue(events.find("name=\"VEVENT\"") != std::string::npos, "and to events");

  const std::string todos = dav::reportComponentQuery("VTODO");
  expectTrue(todos.find("name=\"VTODO\"") != std::string::npos, "the reminder REPORT names VTODO");
  expectTrue(todos.find("expand") == std::string::npos, "and does not ask for expansion it has no window for");

  const std::string contacts = dav::reportContactsQuery();
  expectTrue(std::string(contacts).find("addressbook-query") != std::string::npos,
             "the contact REPORT is an addressbook-query");
  expectTrue(std::string(contacts).find("address-data") != std::string::npos, "and asks for the card data");
}

static void testFormatIcalUtc() {
  expectEq(dav::formatIcalUtc(kNow), "20260914T090000Z", "an epoch becomes the iCalendar UTC form");
}

// ---------------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------------
static void testCivil() {
  const dav::Civil c = dav::civilFromEpoch(kNow);
  expectTrue(c.year == 2026 && c.month == 9 && c.day == 14, "civil date round-trips");
  expectTrue(c.hour == 9 && c.minute == 0, "civil time round-trips");
  expectTrue(c.weekday == 1, "2026-09-14 was a Monday");

  // The epoch itself, and a leap day, are the two the arithmetic gets wrong
  // when the era maths is off by one.
  const dav::Civil zero = dav::civilFromEpoch(0);
  expectTrue(zero.year == 1970 && zero.month == 1 && zero.day == 1 && zero.weekday == 4,
             "the epoch is Thursday 1 January 1970");
  const dav::Civil leap = dav::civilFromEpoch(1709164800u);  // 2024-02-29T00:00:00Z
  expectTrue(leap.year == 2024 && leap.month == 2 && leap.day == 29, "a leap day is a leap day");
}

static void testDayHelpers() {
  expectTrue(dav::startOfDay(kNow) == kNow - 9 * 3600, "startOfDay drops the time");
  expectEq(dav::formatTime(kNow), "09:00", "a time is 24-hour and zero-padded");
  expectEq(dav::formatDayHeading(kNow), "Mon 14 Sep", "a day heading names the weekday");
  expectEq(dav::formatDayLabel(kNow, kNow), "Today", "today is named, not dated");
  expectEq(dav::formatDayLabel(kNow + 86400, kNow), "Tomorrow", "tomorrow too");
  expectEq(dav::formatDayLabel(kNow + 3 * 86400, kNow), "Thu 17 Sep", "anything further is dated");
  // A day earlier is not "yesterday": the agenda never looks backwards, and a
  // label that can only appear by accident is one nobody can check.
  expectEq(dav::formatDayLabel(kNow - 86400, kNow), "Sun 13 Sep", "a past day is dated");
}

static void testAge() {
  expectTrue(dav::ageSince(kNow, kNow + 30).unit == dav::AgeUnit::JustNow, "under a minute is just now");
  const auto minutes = dav::ageSince(kNow, kNow + 5 * 60);
  expectTrue(minutes.unit == dav::AgeUnit::Minutes && minutes.value == 5, "five minutes");
  const auto hours = dav::ageSince(kNow, kNow + 3 * 3600);
  expectTrue(hours.unit == dav::AgeUnit::Hours && hours.value == 3, "three hours");
  const auto days = dav::ageSince(kNow, kNow + 2 * 86400);
  expectTrue(days.unit == dav::AgeUnit::Days && days.value == 2, "two days");
  expectTrue(dav::ageSince(kNow + 1000, kNow).unit == dav::AgeUnit::JustNow, "a future timestamp reads as just now");
  expectTrue(dav::ageSince(0, kNow).unit == dav::AgeUnit::JustNow, "a missing timestamp has no age");
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------
static void testSortByStart() {
  std::vector<dav::Entry> list(3);
  list[0].title = "spaeter";
  list[0].start = kNow + 7200;
  list[1].title = "ganztags";
  list[1].start = dav::startOfDay(kNow);
  list[1].allDay = true;
  list[2].title = "frueh";
  list[2].start = kNow;
  dav::sortByStart(list);
  expectEq(list[0].title, "ganztags", "an all-day entry heads its day");
  expectEq(list[1].title, "frueh", "then the earliest timed entry");
  expectEq(list[2].title, "spaeter", "then the later one");
}

static void testSortReminders() {
  std::vector<dav::Entry> list(4);
  list[0].title = "erledigt";
  list[0].done = true;
  list[0].start = kNow;
  list[1].title = "ohne Datum";
  list[1].start = 0;
  list[2].title = "morgen";
  list[2].start = kNow + 86400;
  list[3].title = "heute";
  list[3].start = kNow;
  dav::sortReminders(list);
  expectEq(list[0].title, "heute", "the soonest undone reminder leads");
  expectEq(list[1].title, "morgen", "then the later one");
  expectEq(list[2].title, "ohne Datum", "an undated reminder sits after the dated ones, not before them");
  expectEq(list[3].title, "erledigt", "and a done reminder goes last");
}

static void testSortByTitle() {
  std::vector<dav::Entry> list(3);
  list[0].title = "zebra";
  list[1].title = "Anna";
  list[2].title = "bernd";
  dav::sortByTitle(list);
  expectEq(list[0].title, "Anna", "case-insensitive: an uppercase name does not sort ahead of every lowercase one");
  expectEq(list[1].title, "bernd", "second");
  expectEq(list[2].title, "zebra", "third");
}

// ---------------------------------------------------------------------------
// The elision rule
// ---------------------------------------------------------------------------
static void testFirstLineNeverElides() {
  expectEq(dav::firstLine("Erste Zeile\nZweite"), "Erste Zeile", "the preview is the first line");
  expectEq(dav::firstLine(""), "", "an empty body gives an empty preview");
  expectEq(dav::firstLine("   \n  Text"), "Text", "leading blank lines are skipped");

  // docs/design-language.md: "Nothing is ever elided. No exceptions." A long
  // line comes back whole; choosing a cut that holds it is the screen's job.
  const std::string long_(400, 'a');
  const std::string got = dav::firstLine(long_);
  expectTrue(got.size() == long_.size(), "a long line is returned whole, not clipped");
  expectTrue(got.find("...") == std::string::npos, "and carries no ellipsis");
}

int main() {
  testUnfolding();
  testUnescaping();
  testTimedEvent();
  testAllDayEvent();
  testExpandedRecurrence();
  testVtimezoneDoesNotLeak();
  testReminder();
  testReminderDone();
  testEmptyComponentDropped();
  testPropertyParameters();
  testLowercaseKeywords();
  testVcard();
  testVcardFallbacksAndGroups();
  testMultipleVcards();
  testMultistatusCollections();
  testAddressBookDetection();
  testMultistatusPrefixes();
  testCalendarAndAddressData();
  testHrefResolution();
  testDiscoveryUrl();
  testRequestBodies();
  testFormatIcalUtc();
  testCivil();
  testDayHelpers();
  testAge();
  testSortByStart();
  testSortReminders();
  testSortByTitle();
  testFirstLineNeverElides();

  std::printf("  %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
