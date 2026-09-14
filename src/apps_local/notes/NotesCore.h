#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Freestanding half of the NOTES app: the DAV request bodies, the multistatus
// reader, and the iCalendar parser. No renderer, no storage, no network -- so
// host-tests/notes drives all of it on the desktop.
//
// What this reads, and why it is not "notes" in the Apple sense: iCloud has no
// CardDAV or CalDAV interface to Notes.app. It exposes calendars and reminders
// over CalDAV and contacts over CardDAV, and the reminder is the one that
// carries free text (SUMMARY plus DESCRIPTION). VJOURNAL is the component
// RFC 5545 actually calls a journal entry; iCloud never returns one, but
// Nextcloud and Radicale do, so both are read and shown in one list.
namespace notes {

// A note as the list and the detail screen see it. Fields are already
// unescaped and unfolded; nothing downstream re-parses iCalendar.
struct Note {
  std::string uid;
  std::string title;     // SUMMARY, or the first line of the body when absent
  std::string body;      // DESCRIPTION
  std::string calendar;  // display name of the collection it came from
  // Seconds since the Unix epoch, from LAST-MODIFIED or DTSTAMP. Zero when the
  // server sent neither, which sorts such notes to the end rather than to 1970
  // pretending to be the oldest thing you own.
  uint32_t modified = 0;
  bool done = false;  // VTODO with STATUS:COMPLETED
};

// ---------------------------------------------------------------------------
// iCalendar
// ---------------------------------------------------------------------------

// Undoes RFC 5545 line folding: a CRLF (or LF) followed by one space or tab is
// a continuation, not a break. Servers fold at 75 octets mid-word and mid-UTF-8
// sequence, so this runs before anything looks at a line.
std::string unfoldIcal(const std::string& raw);

// Undoes TEXT escaping: \n and \N are newlines, \\ \, \; are literals. An
// unknown escape keeps its character, which is what every reader does with the
// handful of servers that emit \: in a URL.
std::string unescapeIcalText(const std::string& value);

// Parses one iCalendar object, appending every VJOURNAL and VTODO it contains.
// A calendar-data payload can hold several components plus a VTIMEZONE, so
// this walks components rather than assuming one.
void parseIcal(const std::string& ical, const std::string& calendarName, std::vector<Note>& out);

// ---------------------------------------------------------------------------
// WebDAV
// ---------------------------------------------------------------------------

// One <response> from a multistatus: the href, plus whichever of the two
// payloads the request asked for.
struct DavResponse {
  std::string href;
  std::string displayName;   // <displayname>
  std::string calendarData;  // <calendar-data>
  bool holdsNotes = false;   // supported-calendar-component-set names VJOURNAL or VTODO
};

// Pulls the <response> elements out of a 207 body. Deliberately not a general
// XML parser: it matches local names so the namespace prefix a server picks
// (D:, d:, or a default xmlns) does not matter, and it ignores everything it
// was not asked for.
std::vector<DavResponse> parseMultistatus(const std::string& xml);

// Resolves an href against the URL it was fetched from. Servers answer with an
// absolute path far more often than a full URL, and iCloud's principal href is
// a path while its calendar-home-set is a full URL, so both shapes appear in
// one sync.
std::string resolveHref(const std::string& baseUrl, const std::string& href);

// ---------------------------------------------------------------------------
// Request bodies
// ---------------------------------------------------------------------------

// PROPFIND Depth:0 asking who we are. Step one of RFC 6764 discovery.
const char* propfindCurrentUserPrincipal();

// PROPFIND Depth:0 on the principal, asking where its calendars live.
const char* propfindCalendarHomeSet();

// PROPFIND Depth:1 on the home set: every collection, its name, and which
// components it holds.
const char* propfindCalendarCollections();

// REPORT Depth:1 asking one collection for every note of one component type,
// data included, so a sync is one round trip per collection rather than one
// per note.
std::string reportNotesQuery(const char* componentName);

// The well-known path a bare domain is turned into before discovery starts.
// "https://caldav.icloud.com" and "icloud.com" both have to become a URL the
// first PROPFIND can be sent to.
std::string discoveryUrl(const std::string& serverInput);

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

// Newest first; notes with no timestamp go last. Stable in the title for two
// notes saved in the same second, so a re-sync does not reshuffle the list.
void sortNewestFirst(std::vector<Note>& notes);

// "vor 3 Minuten" is not this device's voice and localising a duration needs
// plural rules; this returns the pieces and the screen composes them with
// tr(). Value is the count, unit is one of the STR_* choices the caller maps.
enum class AgeUnit : uint8_t { JustNow, Minutes, Hours, Days };
struct Age {
  AgeUnit unit = AgeUnit::JustNow;
  uint32_t value = 0;
};
// nowEpoch and thenEpoch are both seconds. A then in the future (a device
// clock behind the server's) reads as JustNow rather than as a negative age.
Age ageSince(uint32_t thenEpoch, uint32_t nowEpoch);

// First line of the body, clipped to maxChars, for the list's second row. An
// empty body gives an empty string; the screen draws nothing rather than a
// placeholder.
std::string previewLine(const std::string& body, size_t maxChars);

}  // namespace notes
