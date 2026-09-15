# CALENDAR: reading CalDAV and CardDAV

**Status: half built, and the half that is missing is the half that needs a
device.** The core and the screens exist and are green in the host suites. There
is no Activity, no account store and no shelf row, so nothing reaches a device
yet. This file is the handover: what is decided, what is verified, and what the
next session has to do.

It was written in a checkout that could not build firmware at all -- the
environment's network policy answers `api.registry.platformio.org` with 403, so
PlatformIO could not install a platform and neither a device env nor the
simulator would build. Everything below marked NOT VERIFIED is that, and
nothing else.

## What it reads

| Protocol | Component | Becomes         |
| -------- | --------- | --------------- |
| CalDAV   | VEVENT    | the agenda      |
| CalDAV   | VTODO     | the reminders   |
| CalDAV   | VJOURNAL  | a note          |
| CardDAV  | VCARD     | the contacts    |

**iCloud has no CardDAV or CalDAV interface to Notes.app.** It serves calendars
and reminders over CalDAV and contacts over CardDAV; Notes runs over CloudKit
and is not reachable. On an iCloud account the reminder is the component
carrying free text, which is why VTODO is read at all. VJOURNAL is what RFC 5545
actually calls a journal entry -- iCloud never returns one, Nextcloud and
Radicale do.

## The two decisions everything else rests on

**Recurrence is the server's job.** RFC 4791's `calendar-query` takes an
`<expand>` element, and a server honouring it returns one component per
occurrence with the recurrence already applied. An RRULE engine on the device
would mean BYDAY, BYSETPOS and EXDATE with no tz database, to arrive at an
answer the server already has. `reportEventsQuery()` asks for a window;
`parseIcal` shows what comes back.

Worth knowing: `ilvar/esp32-calendar`, an unrelated ESP32 agenda for a wall
panel, independently reached the same two decisions -- `<c:expand>` and an
arbitrary-method `REPORT`. Nothing was copied (it carries no licence at all, so
nothing could be), but the approach has been arrived at twice.

**Time is UTC or it is a date.** The other half of the same bargain: an expanded
component's times are UTC per the RFC, so no TZID means no tz database is
needed. An all-day event stays a DATE and is never drawn as 00:00 -- both the
`VALUE=DATE` spelling and the bare eight-digit form servers actually send.

A consequence to keep in view: everything is displayed in UTC. For a user east
or west of Greenwich the agenda is offset by their own zone. Fixing that is a
fixed per-account offset, or the device's own configured zone -- not a tz
database.

## What is built, and how far it is trusted

| Layer                   | File                    | Verified by                        |
| ----------------------- | ----------------------- | ---------------------------------- |
| Protocol and parsing    | `DavCore.{h,cpp}`       | `host-tests/dav`, 122 checks       |
| Screens                 | `DavScreens.{h,cpp}`    | `host-tests/ui`, compiled -Werror  |
| DAV verbs on the client | `HttpDownloader::davRequest` | NOTHING -- never compiled     |
| Activity, store, shelf  | --                      | does not exist                     |

`davRequest()` adds PROPFIND and REPORT: a method the HTTP verbs do not cover,
an XML body, and the `Depth` header those methods are defined in terms of.
The SDK's `SecureHttpClient::sendRequest` already takes a method and a payload,
so the wolfSSL path needed a caller rather than new transport code. Redirects
are hopped in `runDavWolf` rather than inside the client so the body survives
them -- iCloud answers the well-known path with a 301 onto the user's shard, and
a REPORT that dropped its body there arrives as an empty query.

Every suite here was checked against mutants rather than trusted. Six were
tried and all six failed the suite after the fixes they prompted. Two of them
found real defects in code that was already written and believed:

- The band repeated the open tab, so `find("TO DO")` in the screen test
  returned the TITLE's run and every tap assertion was about a word with no
  action behind it.
- Three taps on one `Rendered` do not answer "is this segment inert": the first
  routes and the later ones do not. One tap per render now.

## What the next session does

**1. The account store.** `PersistableStore` under `/.crosspoint/`, modelled on
`lib/KOReaderSync/KOReaderCredentialStore.h` -- which already XOR-obfuscates a
password against the device MAC and base64s it. Server URL, username,
app-specific password. An iCloud account needs an app-specific password; the
ordinary one will not authenticate.

**2. The sync, and the four things `host-tests/dav/run.sh` will scan for.**
They are written out in that file's tail. In short: manual sync only (no task,
no timer, nothing from `onEnter`), `requestUpdate(true)` before the blocking
request, `dedupeOccurrences()` called ONCE over the merged list rather than per
collection, and the agenda REPORT carrying `reportEventsQuery`'s bounds.

Discovery is three round trips, then one REPORT per collection:
`propfindCurrentUserPrincipal` -> `propfindHomeSets` -> `propfindCollections`
-> `reportEventsQuery` / `reportComponentQuery` / `reportContactsQuery`.

**3. The cached index.** The list has to open without a network round trip, and
the last-sync timestamp has to survive a reboot.

**4. The three variants, which are still owed.**
`docs/building-apps.md` requires three arrangements behind a `-DDAV_VARIANT`
macro, rendered with `sim-shot.sh`, composed with
`tools_local/compose_shots.py`, and the winner built with the macro deleted in
the same commit. **This has not happened.** What is in `DavScreens.cpp` is one
arrangement, reasoned from `docs/design-language.md` and never seen. Seed the
card first: an agenda judged against an empty account is a layout nobody will
ever see.

Three things in it are judgement calls a render may overturn:

- The segments say AGENDA / TO DO / PEOPLE. "REMINDERS" was rejected on width
  -- three segments share one 480px row -- but that was arithmetic, not a look.
- SYNC is a chip on the band rather than a footer control.
- A row is label over subtitle, with the day as a section heading above the
  first row of each day. No two-column layout anywhere, which is both the
  request and the only thing that fits: eliding is forbidden outright.

**5. The shelf row and the icon.** One line in `src/apps_local/Shelf.cpp` under
`kApps`, one line in `tools_local/toybox/icons.txt`, then
`./tools_local/toybox/gen_toybox_icons.sh`. A row with no icon does not compile.

## Still open

- **Write-back.** Nothing writes. Ticking a reminder off would be a PUT with an
  `If-Match` etag, and etags are not stored today.
- **The display timezone**, above.
- **One event, two devices.** No etag caching at all, so every sync refetches
  every collection in full.
