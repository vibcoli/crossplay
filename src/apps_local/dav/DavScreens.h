#pragma once

// The DAV screens. Freestanding builders in the ChessScreens mould: a model
// in, a drawn frame out, no renderer and no Activity, so host-tests/ui/ can
// assert what they drew and what they made tappable.
//
// ---------------------------------------------------------------------------
// FOUR SCREENS, AND WHY THE THREE LISTS ARE ONE OF THEM.
//
// An agenda, a reminder list and a contact list are the same object here: a
// column of rows, each a line of text with a second line under it. They differ
// in what fills those two lines, which is the Activity's problem, and in
// nothing the screen can see. One builder means the segment strip, the sync
// chip and the scroll arithmetic cannot drift between the three, and a reader
// who has learned one has learned all of them.
//
// NOTHING HERE IS A TABLE. A day's entries are a run of rows under a section
// heading -- `fui::ListItem::sectionHeading`, which the component already
// draws -- rather than a column of times beside a column of titles. Two
// columns on a 480px panel means either a time that wraps or a title that is
// cut, and docs/design-language.md forbids the second absolutely: "Nothing is
// ever elided. No exceptions." A row that owns its full width can always shrink
// a cut instead.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"

namespace davui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer owns the 200s, Hacker News the 300s.
enum : fui::ActionId {
  ActionOpenEntry = 400,
  // The three segments. One action each rather than one cycling toggle: a
  // segment names a destination absolutely, so tapping the one you are in does
  // nothing and no label can disagree with what is on screen.
  ActionShowAgenda = 401,
  ActionShowReminders = 402,
  ActionShowContacts = 403,
  // The only control in the app that asks for the network, and the only one
  // that ever will: syncing is manual, always.
  ActionSync = 404,
  ActionSetup = 405,
  ActionNoticeBack = 406,
  // The account screen's three fields and its one destructive door.
  ActionEditServer = 407,
  ActionEditUser = 408,
  ActionEditPassword = 409,
  ActionForget = 410,
  // The detail screen's paging, for a body longer than one screenful.
  ActionPagePrev = 411,
  ActionPageNext = 412,
};

enum class Tab : uint8_t { Agenda, Reminders, Contacts };

// --- The three lists -----------------------------------------------------

struct ListModel {
  Tab tab = Tab::Agenda;
  // Right of the header band: when the last sync was. Built by the Activity
  // because only it can read a clock, and composed with tr() there because
  // localising a duration needs plural rules the core deliberately does not
  // carry. Null draws nothing, which is the honest state before a first sync.
  const char* syncLabel = nullptr;
  // Drawn instead of rows when the list is empty. An account that has never
  // synced, and a day with nothing in it, are both ordinary -- and a blank
  // panel reads as a fault.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
  // Built by the Activity, the way shelfui::MenuModel carries its rows. label
  // is the entry, subtitle is when or where, and sectionHeading carries the
  // day band on the first row of each day.
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
};

void buildList(toybox::Screen& screen, const ListModel& model);

// The band the list draws into. Shared with the Activity so its scroll maths
// and the drawn rows come from one function rather than two that are only ever
// wrong together.
fui::Rect listBand(const fui::DeviceContext& device);

// How tall a row is, measured from the fonts that will draw it. Two lines:
// every row in all three lists carries a second line, and a height sized for
// one clips the second against the row below.
int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);

// The width a row's label is actually drawn into. Exported so the Activity
// fits its text to the space the component will give it rather than to a
// second guess at that space -- the disagreement docs/design-language.md
// names as the one that cost Connections 48 shortened tiles.
int16_t listLabelWidth(const fui::DrawTarget& target, const fui::DeviceContext& device, const fui::ThemeTokens& tokens);

// The segment label for a tab, in one place. The Activity needs it for its
// header title and the strip needs it for its buttons; two copies are two
// things that can be edited alone.
const char* tabLabel(Tab tab);

// --- One entry -----------------------------------------------------------

// The detail screen's body: the words, the cut they are set in, and the wrap
// that counts AND draws them. One object rather than three arguments that must
// agree.
struct DetailBody {
  const char* text = "";
  fui::TextStyle style{};
  toybox::WrappedText* wrap = nullptr;
};

struct DetailModel {
  const char* title = "";
  // The facts, one per line, in the order a reader wants them. Each is null
  // when the entry does not carry it, and a null line is not drawn rather than
  // drawn empty -- a labelled blank says the server sent something it did not.
  const char* when = nullptr;        // "Today 09:00 - 09:45", or "All day"
  const char* where = nullptr;       // LOCATION, or a contact's phone
  const char* secondary = nullptr;   // a contact's email
  const char* collection = nullptr;  // which calendar or address book
  DetailBody body{};
  uint32_t topLine = 0;
  // "2 / 5", built by the Activity because only it knows the line count. Null
  // when the body fits on one screen, and then no footer is drawn at all.
  const char* pageLabel = nullptr;
};

void buildDetail(toybox::Screen& screen, const DetailModel& model);

// The rect the body is wrapped and drawn into. Exported for the same reason as
// listBand: the Activity counts lines against it before the screen draws them.
fui::Rect detailBodyRect(const fui::DeviceContext& device, bool hasFooter);

// --- The account ---------------------------------------------------------

struct SetupModel {
  // What is stored, shown as it will be used. A password is never echoed: the
  // row says whether one is set, which is the only thing a reader can act on.
  const char* server = nullptr;
  const char* username = nullptr;
  bool hasPassword = false;
  // Drawn under the rows when a sync has failed, so the reason sits with the
  // fields that would fix it rather than on a screen the reader has left.
  const char* problem = nullptr;
  int selected = 0;
};

void buildSetup(toybox::Screen& screen, const SetupModel& model);

// --- A sentence, and at most one way on ----------------------------------

struct NoticeModel {
  const char* headline = "";
  const char* message = nullptr;
  // Both must be set or no control is drawn: an action with no label is an
  // invisible control, which docs/design-language.md calls a dead gesture.
  const char* actionLabel = nullptr;
  fui::ActionId action = fui::NO_ACTION;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace davui
