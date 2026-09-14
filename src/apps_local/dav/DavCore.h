#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Freestanding half of the DAV app: the request bodies, the multistatus
// reader, and the iCalendar and vCard parsers. No renderer, no storage, no
// network -- so host-tests/dav drives all of it on the desktop.
//
// Three things are read, over two protocols:
//
//   CalDAV  VEVENT   the agenda
//           VTODO    the reminders (this is what iCloud Reminders are)
//           VJOURNAL a note, where the server has them (not iCloud)
//   CardDAV VCARD    the contacts
//
// Two decisions shape everything below.
//
// RECURRENCE IS THE SERVER'S JOB. RFC 4791's calendar-query takes an <expand>
// element, and a server that honours it returns one component per occurrence
// with the recurrence already applied. Implementing RRULE here would mean
// BYDAY, BYSETPOS, EXDATE and the leap-second-free arithmetic under them, on a
// device with no tz database -- to arrive at an answer the server already has.
// So the agenda asks for a window and shows what comes back.
//
// TIME IS UTC OR IT IS A DATE. RFC 4791 says an expanded component's times are
// UTC, which is the other half of the bargain above: no TZID means no tz
// database. A floating time (no Z, no TZID) is taken as given, which is what a
// floating time means. An all-day event is a date with no time at all and is
// rendered as one rather than as midnight.
namespace dav {

// What a collection holds. A server names several per collection; this is the
// one the app asked it for.
enum class Kind : uint8_t { Event, Reminder, Note, Contact };

// One row in one of the three lists. Every field is already unfolded and
// unescaped; nothing downstream re-parses a wire format.
struct Entry {
  Kind kind = Kind::Event;
  std::string uid;
  std::string title;       // SUMMARY, or FN for a contact
  std::string body;        // DESCRIPTION, or the note text
  std::string location;    // LOCATION, events only
  std::string collection;  // display name of the collection it came from

  // Seconds since the Unix epoch, UTC. Zero means the component carried none.
  uint32_t start = 0;  // DTSTART, or DUE for a reminder
  uint32_t end = 0;    // DTEND
  uint32_t modified = 0;

  bool allDay = false;  // DTSTART was a DATE, so there is no time to show
  bool done = false;    // VTODO with STATUS:COMPLETED

  // Contacts. Kept as one string each: a device that cannot dial or mail has
  // no use for a list of them, and the first is the one on the card.
  std::string phone;
  std::string email;
};

// ---------------------------------------------------------------------------
// Line formats. iCalendar and vCard share folding and property syntax.
// ---------------------------------------------------------------------------

// Undoes RFC 5545 line folding: a CRLF (or LF) followed by one space or tab is
// a continuation, not a break. Servers fold at 75 octets mid-word and mid-UTF-8
// sequence, so this runs before anything looks at a line.
std::string unfoldIcal(const std::string& raw);

// Undoes TEXT escaping: \n and \N are newlines, \\ \, \; are literals. An
// unknown escape keeps its character, which is what every reader does with the
// handful of servers that emit \: in a URL.
std::string unescapeIcalText(const std::string& value);

// Parses one iCalendar object, appending every VEVENT, VTODO and VJOURNAL it
// contains. A calendar-data payload holds several components plus a VTIMEZONE,
// and an expanded recurrence arrives as many VEVENTs sharing a UID, so this
// walks components rather than assuming one.
void parseIcal(const std::string& ical, const std::string& collectionName, std::vector<Entry>& out);

// Parses one vCard payload, appending every VCARD in it. 3.0 and 4.0 differ in
// how they spell a telephone type but not in the two fields this reads.
void parseVcard(const std::string& vcf, const std::string& collectionName, std::vector<Entry>& out);

// ---------------------------------------------------------------------------
// WebDAV
// ---------------------------------------------------------------------------

// One <response> from a multistatus: the href, plus whichever payload the
// request asked for.
struct DavResponse {
  std::string href;
  std::string displayName;   // <displayname>
  std::string calendarData;  // <calendar-data> or <address-data>
  // Which of the components this collection said it holds. A collection
  // answers for several, and asking a VEVENT-only calendar for VTODOs is a
  // round trip that can only come back empty.
  bool holdsEvents = false;
  bool holdsReminders = false;
  bool holdsNotes = false;
  bool isAddressBook = false;
};

// Pulls the <response> elements out of a 207 body. Deliberately not a general
// XML parser: it matches local names so the namespace prefix a server picks
// (D:, d:, or a default xmlns) does not matter, and it ignores everything it
// was not asked for.
std::vector<DavResponse> parseMultistatus(const std::string& xml);

// Resolves an href against the URL it was fetched from. Servers answer with an
// absolute path far more often than a full URL, and iCloud's principal href is
// a path while its home-set is a full URL, so both shapes appear in one sync.
std::string resolveHref(const std::string& baseUrl, const std::string& href);

// ---------------------------------------------------------------------------
// Request bodies
// ---------------------------------------------------------------------------

// PROPFIND Depth:0 asking who we are. Step one of RFC 6764 discovery.
const char* propfindCurrentUserPrincipal();

// PROPFIND Depth:0 on the principal, asking where the calendars and the
// address books live. Both home sets in one request: iCloud answers for both
// from the same principal, and a second round trip buys nothing.
const char* propfindHomeSets();

// PROPFIND Depth:1 on a home set: every collection, its name, and what it holds.
const char* propfindCollections();

// REPORT Depth:1 for the agenda: every VEVENT overlapping [startUtc, endUtc),
// recurrences expanded by the server, data included. Times are the iCalendar
// UTC form, which is what formatIcalUtc produces.
std::string reportEventsQuery(const std::string& startUtc, const std::string& endUtc);

// REPORT Depth:1 for a component that has no useful time window: every VTODO
// or VJOURNAL in the collection, data included.
std::string reportComponentQuery(const char* componentName);

// REPORT Depth:1 for an address book: every VCARD, data included.
const char* reportContactsQuery();

// "20260914T000000Z" from an epoch, for the two query bounds above.
std::string formatIcalUtc(uint32_t epoch);

// The well-known path a bare domain is turned into before discovery starts.
// The CardDAV well-known path differs from CalDAV's, and iCloud serves the two
// protocols from different hosts, so the caller says which it wants.
std::string discoveryUrl(const std::string& serverInput, bool cardDav);

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

// Agenda order: soonest first, all-day before timed within a day, title as the
// tie-break so a re-sync never reshuffles two entries that start together.
void sortByStart(std::vector<Entry>& entries);

// Reminder order: undone before done, then by due date with the undated last,
// then title. An undated reminder sorting to 1970 would bury every real one.
void sortReminders(std::vector<Entry>& entries);

// Contact order: by title, case-insensitively, because a list sorted by byte
// value puts every lowercase surname after every uppercase one.
void sortByTitle(std::vector<Entry>& entries);

// Civil date and time, both UTC. The device has no tz database; see the file
// header for why that is the bargain rather than a gap.
struct Civil {
  uint16_t year = 1970;
  uint8_t month = 1;  // 1-12
  uint8_t day = 1;    // 1-31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t weekday = 4;  // 0 = Sunday; 1970-01-01 was a Thursday
};
Civil civilFromEpoch(uint32_t epoch);

// Midnight UTC opening the day that contains epoch. The agenda groups by this,
// so two entries on one day answer with one number.
uint32_t startOfDay(uint32_t epoch);

// "14:05". Twenty-four hour, because a device with no locale has no business
// guessing, and because it is one character narrower than the alternative on a
// 480px panel.
std::string formatTime(uint32_t epoch);

// "Mon 14 Sep" -- the day band's heading. Fixed English abbreviations rather
// than tr(): these are the only strings in the app that are read as data
// rather than as the device speaking, and a three-letter day is the same
// width in every language this fork ships.
std::string formatDayHeading(uint32_t epoch);

// Relative when it is close and dated when it is not: "Today", "Tomorrow", or
// the day heading. nowEpoch is the device's clock.
std::string formatDayLabel(uint32_t epoch, uint32_t nowEpoch);

// How long ago, for the one line under the header that says when the last sync
// was. Localising a duration needs plural rules, so this returns the pieces
// and the screen composes them with tr().
enum class AgeUnit : uint8_t { JustNow, Minutes, Hours, Days };
struct Age {
  AgeUnit unit = AgeUnit::JustNow;
  uint32_t value = 0;
};
// A then in the future (a device clock behind the server's) reads as JustNow
// rather than as a negative age.
Age ageSince(uint32_t thenEpoch, uint32_t nowEpoch);

// First line of a body, for a list row's second line. Returns the whole line:
// docs/design-language.md forbids eliding anything, so a line too long for its
// box is the screen's problem to solve by choosing a smaller cut, never this
// function's to solve by hiding characters.
std::string firstLine(const std::string& body);

}  // namespace dav
