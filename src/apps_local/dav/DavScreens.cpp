#include "DavScreens.h"

#include "../ui/ToyboxText.h"

namespace davui {
namespace {

// Below the header band AND the rule under it, which is what kChromeHeight
// names. Shared by every screen here so they line up with each other and with
// the shelf the reader just came from.
constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;

// What the band says on every screen in this app. One place, because the
// notice screen has to agree with the lists and two literals cannot.
constexpr const char* kAppTitle = "CALENDAR";

// Header band, rule and page margin. Every screen here opens with this.
//
// rightLabel is drawn in PAPER, not ink. The band is solid black and the
// header component draws rightLabel with subtitleText, whose default colour is
// Black -- so a label left at the default is painted black on black and simply
// is not there. This fork has paid for that twice already; see the note in
// HackerNewsScreens.cpp.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel, bool showSync = false) {
  fui::HeaderProps header;
  header.title = title;
  if (showSync) {
    // The one control in the app that reaches for the radio. It lives on the
    // band rather than in the footer because the footer is the map between the
    // three lists, and a control that changes WHAT you are looking at must not
    // sit in the strip that says WHERE you are.
    header.trailingLabel = "SYNC";
    header.trailingAction = ActionSync;
    header.trailingStyles = toybox::bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The two lines every empty list gets, laid out as one block and centred as
// one block. Two centred calls would draw at the same y, because centeredText
// centres in the content rect and consumes nothing -- the trap that once had a
// headline painted invisibly over its own message.
void emptyBlock(toybox::Screen& screen, const char* headline, const char* message) {
  if (headline == nullptr) return;
  // OFF THE BAND, SO IT HAS TO BE INK. The display cut is otherwise only ever
  // set on the header band, so its token colour is paper, and taken as given
  // here it paints white on white paper.
  fui::TextStyle head = screen.theme().titleText;
  head.color = fui::Color::Black;
  head.align = fui::TextAlign::Center;
  fui::TextStyle body = screen.theme().smallText;
  body.color = fui::Color::Black;
  body.align = fui::TextAlign::Center;

  const fui::Rect content = screen.body();
  // Both blocks reserve what the SDK's OWN WRAP will emit, never a width
  // divided by a column count. Greedy wrapping breaks between words and so
  // does not fill a line to its edge: a sentence 2.6 columns wide needs three
  // lines while the division says two, and the third is then dropped.
  head.maxLines = 2;
  const int16_t headH = fui::measureWrappedText(screen.target(), headline, head, content.width).height;
  body.maxLines = 4;  // a ceiling, not a target: the wrap emits what it needs
  const bool hasMessage = message != nullptr && message[0] != '\0';
  const int16_t messageH =
      hasMessage ? fui::measureWrappedText(screen.target(), message, body, content.width).height : 0;
  const int16_t gap = hasMessage ? toybox::kGutter : 0;

  // Every element advances y as it is placed, and none advances for an element
  // that was not drawn: centeredText consumes nothing, which is how a headline
  // comes to be painted invisibly on top of its own message.
  int16_t y = static_cast<int16_t>(content.y + (content.height - headH - gap - messageH) / 2);
  screen.target().text(fui::makeRect(content.x, y, content.width, headH), headline, head);
  if (hasMessage) {
    y = static_cast<int16_t>(y + headH + gap);
    screen.target().text(fui::makeRect(content.x, y, content.width, messageH), message, body);
  }
}

}  // namespace

const char* tabLabel(const Tab tab) {
  switch (tab) {
    case Tab::Agenda:
      return "AGENDA";
    case Tab::Reminders:
      // "TO DO" rather than "REMINDERS": three segments share one 480px row,
      // so each has about 150px, and the label has to fit the smallest cut
      // without being shortened -- which docs/design-language.md forbids
      // outright. The shorter word says the same thing.
      return "TO DO";
    case Tab::Contacts:
      return "PEOPLE";
  }
  return "AGENDA";
}

fui::Rect listBand(const fui::DeviceContext& device) {
  // The segment strip lives at the bottom, so rows stop above it. Reserved
  // here rather than at the draw site because the Activity counts the rows
  // that fit in this exact rect: a full-height band draws rows underneath the
  // segments and pages by a count the screen never showed.
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  // Label plus subtitle plus air. Every row in all three lists carries a
  // second line -- a time, a place, a number -- so a height sized for one line
  // clips that second line against the row below.
  return static_cast<int16_t>(target.lineHeight(tokens.bodyText.font) + target.lineHeight(tokens.smallText.font) +
                              toybox::kGutter);
}

int16_t listLabelWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens) {
  (void)target;
  // The row less its side padding on both edges. Nothing sits beside the
  // label: the second line carries the time and the place, so the first line
  // owns the full width and can always be set in a smaller cut rather than
  // shortened.
  return static_cast<int16_t>(listBand(device).width - 2 * tokens.listSidePadding);
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  // The band names the APP, not the list. Which list is open is what the
  // segment strip says, and saying it twice puts the same word in two places
  // that can disagree -- it also made a host test tap the title believing it
  // had tapped the segment, and pass. The band is chrome that never repaints,
  // which is the black docs/design-language.md says is free; the segments are
  // the part that changes.
  chrome(screen, kAppTitle, model.syncLabel, /*showSync=*/true);

  // The three lists, as segments rather than a cycling toggle: each names
  // where it goes, so the one you are in is simply inert.
  //
  // takeBottom rather than absolute coordinates: it removes the strip from the
  // content flow, so the list above cannot draw rows into it. Positioning it
  // absolutely leaves it underneath the list, which both hides it and gives
  // the rows the taps meant for it.
  {
    const fui::Rect strip = screen.takeBottom(kFooterHeight, toybox::kGutter);
    const int16_t y = strip.y;
    const int16_t third = static_cast<int16_t>((strip.width - 2 * toybox::kGutter) / 3);
    const auto segment = [&screen, y, third](const Tab tab, const fui::ActionId action, const int16_t x,
                                             const bool here) {
      fui::ButtonProps button;
      button.label = tabLabel(tab);
      // Inert where you already are, rather than absent: a segment that
      // disappears moves its neighbours, and the trio is the map.
      button.action = here ? fui::NO_ACTION : action;
      // Filled is where you are, outlined is where you can go. Both must be
      // set: leaving the other to the default draws three filled segments,
      // which says "all" and so says nothing.
      button.styles = here ? toybox::invertedStyles() : toybox::rowStyles();
      screen.button(button, fui::makeRect(x, y, third, kFooterHeight));
    };
    segment(Tab::Agenda, ActionShowAgenda, strip.x, model.tab == Tab::Agenda);
    segment(Tab::Reminders, ActionShowReminders, static_cast<int16_t>(strip.x + third + toybox::kGutter),
            model.tab == Tab::Reminders);
    segment(Tab::Contacts, ActionShowContacts, static_cast<int16_t>(strip.x + 2 * (third + toybox::kGutter)),
            model.tab == Tab::Contacts);
  }

  if (model.count <= 0) {
    emptyBlock(screen, model.emptyHeadline, model.emptyMessage);
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpenEntry;
  list.rowHeight = listRowHeight(screen.target(), screen.theme());
  list.labelText = screen.theme().bodyText;
  list.labelText.maxLines = 1;
  list.subtitleText = screen.theme().smallText;
  // Off, or a wrapping label is capped at 60% of the row so it sits prettily
  // beside a value. There is no value here: the label is the content and the
  // second line is a footnote under it, not beside it.
  list.balanceWrappedLabelWithValue = false;
  screen.list(list);
}

// --- One entry -----------------------------------------------------------

fui::Rect detailBodyRect(const fui::DeviceContext& device, const bool hasFooter) {
  const int bottom = hasFooter ? toybox::kMargin + kFooterHeight + toybox::kGutter : toybox::kMargin;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

void buildDetail(toybox::Screen& screen, const DetailModel& model) {
  // The band carries the entry's own title. Handed over WHOLE rather than
  // pre-fitted: headerBand runs it through toybox::fittedTitle, which walks
  // the cuts against the room the component really leaves. Fitting it a second
  // time here, against a room hand-summed from the same terms, is the drift
  // that once put two ellipses on one screen.
  chrome(screen, model.title, model.pageLabel);

  const fui::DeviceContext& device = screen.device();
  const bool hasFooter = model.pageLabel != nullptr;

  if (hasFooter) {
    // Two controls, and nothing between them: there is no third thing a reader
    // can do to a calendar entry on a device that cannot write one back.
    const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
    const int16_t usable = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
    const int16_t half = static_cast<int16_t>((usable - toybox::kGutter) / 2);
    const auto footerButton = [&screen, footerY, half](const char* label, const fui::ActionId action, const int16_t x) {
      fui::ButtonProps button;
      button.label = label;
      button.action = action;
      screen.button(button, fui::makeRect(x, footerY, half, kFooterHeight));
    };
    footerButton("<", ActionPagePrev, toybox::kMargin);
    footerButton(">", ActionPageNext, static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter));
  }

  // The facts, one per line, then the body under them. One column: a label
  // beside a value would be a table, and two columns on this panel means
  // either a wrapped label or a cut value.
  const fui::Rect body = detailBodyRect(device, hasFooter);
  fui::TextStyle factStyle = screen.theme().bodyText;
  factStyle.color = fui::Color::Black;
  factStyle.maxLines = 1;
  const int16_t factLine = screen.target().lineHeight(factStyle.font);

  int16_t y = body.y;
  const auto fact = [&screen, &y, &factStyle, factLine, &body](const char* text) {
    if (text == nullptr || text[0] == '\0') return;
    screen.target().text(fui::makeRect(body.x, y, body.width, factLine), text, factStyle);
    y = static_cast<int16_t>(y + factLine);
  };
  fact(model.when);
  fact(model.where);
  fact(model.secondary);
  fact(model.collection);

  if (y > body.y) y = static_cast<int16_t>(y + toybox::kGutter);

  if (model.body.wrap != nullptr && model.body.text != nullptr && model.body.text[0] != '\0') {
    const fui::Rect rest = fui::makeRect(body.x, y, body.width, static_cast<int16_t>(body.y + body.height - y));
    // Through the wrap rather than fui::textArea(): textArea walks the text
    // from byte zero to find the lines it draws, so paging into the middle of
    // a long note costs the whole note on every paint.
    model.body.wrap->draw(screen.target(), rest, model.body.text, model.body.style, model.topLine);
  }
}

// --- The account ---------------------------------------------------------

void buildSetup(toybox::Screen& screen, const SetupModel& model) {
  chrome(screen, "ACCOUNT", nullptr);

  // A password is never echoed. The row says whether one is stored, which is
  // the only thing a reader can act on; showing dots of the right length would
  // leak the length and showing the value would leak the value.
  fui::ListItem rows[4] = {};
  rows[0].label = "Server";
  rows[0].subtitle = (model.server != nullptr && model.server[0] != '\0') ? model.server : "not set";
  rows[0].actionValue = 0;
  rows[1].label = "User";
  rows[1].subtitle = (model.username != nullptr && model.username[0] != '\0') ? model.username : "not set";
  rows[1].actionValue = 1;
  rows[2].label = "Password";
  rows[2].subtitle = model.hasPassword ? "stored" : "not set";
  rows[2].actionValue = 2;
  rows[3].label = "Forget this account";
  rows[3].subtitle = "Removes the login and every synced entry";
  rows[3].actionValue = 3;

  fui::ListProps list;
  list.items = rows;
  list.count = 4;
  list.selectedIndex = static_cast<int16_t>(model.selected);
  // One action for the list; the Activity reads actionValue to learn which
  // row. Four actions would be four things that can disagree with four rows.
  list.action = ActionEditServer;
  list.labelText = screen.theme().bodyText;
  list.subtitleText = screen.theme().smallText;
  list.balanceWrappedLabelWithValue = false;
  screen.list(list);

  if (model.problem != nullptr && model.problem[0] != '\0') {
    // The reason sits with the fields that would fix it. A failure shown on a
    // screen the reader has already left is a failure nobody reads.
    fui::TextStyle problem = screen.theme().smallText;
    problem.color = fui::Color::Black;
    problem.align = fui::TextAlign::Center;
    const fui::DeviceContext& device = screen.device();
    const int16_t line = screen.target().lineHeight(problem.font);
    screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - line),
                                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), line),
                         model.problem, problem);
  }
}

// --- A sentence, and at most one way on ----------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, kAppTitle, nullptr);
  emptyBlock(screen, model.headline, model.message);

  if (model.actionLabel == nullptr || model.action == fui::NO_ACTION) return;

  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>((device.width - 2 * toybox::kMargin) / 2);
  fui::ButtonProps button;
  button.label = model.actionLabel;
  button.action = model.action;
  screen.button(button, fui::makeRect(static_cast<int16_t>((device.width - width) / 2),
                                      static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight), width,
                                      kFooterHeight));
}

}  // namespace davui
