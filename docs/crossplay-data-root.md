# Proposal: one `/.crossplay` root for everything this fork adds

Status: **proposal, nothing implemented.** Written to be accepted, narrowed or
rejected on the record, because the migration it implies is the kind that is
cheap to discuss and expensive to undo.

The idea: everything CrossPlay puts on an SD card moves under a single
`/.crossplay/` folder, instead of being split between `/.crosspoint/` and a
handful of visible folders at the card root.

## What is on a card today

This is the whole surface, read off the tree rather than remembered.

### Hidden already, under upstream's `/.crosspoint/`

| path | what | written by |
| --- | --- | --- |
| `/.crosspoint/hn` | saved Hacker News articles | `HackerNewsLibrary.cpp:14` |
| `/.crosspoint/instapaper` | the Instapaper queue | `InstapaperLibrary.cpp:15` |
| `/.crosspoint/wikipedia` | article cache | `WikipediaActivity.cpp:30` |
| `/.crosspoint/sudoku.sav` | the game in progress | `SudokuActivity.cpp:20` |
| `/.crosspoint/live.json` | Live's state | `LiveStore.h:40` |
| `/.crosspoint/shelf.cfg`, `shelf-hidden.cfg` | shelf order, hidden items | `Shelf.cpp:133,138` |
| `/.crosspoint/notes-asleep.txt` | Notes' sleep marker | `NotesSleep.h:24` |
| `/.crosspoint/plugins` | installed plugins | `PluginLocations.h:11` |

### Visible, at the card root

| path | what | written by |
| --- | --- | --- |
| `/notes` | the user's notes (`.md`) | `NotesLibrary.cpp:14` |
| `/study` | converted Anki decks | `StudyActivity.cpp:34` |
| `/wikipedia` | the installed pack | `WikipediaPack.h:38` |
| `/trivia` | the installed pack | `TriviaActivity.cpp:27` |
| `/xkcd` | the downloaded archive | `XkcdActivity.cpp:25` |
| `/wallpapers` | the user's wallpapers | `WallpapersCore.h:20` |

Upstream owns `/.crosspoint/` itself, `/dictionaries`, `/plugins`, `/.sleep`
and `/sleep.bmp`. Several of those already answer to two names, hidden and
visible: `/dictionaries` or `/.dictionaries` (`DictionaryRegistry.cpp:17`),
`/plugins` or `/.plugins` (`PluginLocations.h:11`).

## The three scopes this could mean

**A. Move only the hidden data** out of `/.crosspoint/` and into
`/.crossplay/`. Gets the provenance win. Carries all of the migration risk,
because that is where the data a user cannot recreate lives: saved articles,
the Instapaper queue, a game in progress, shelf order.

**B. Move the visible folders too.** Gets a genuinely tidy card root. Buries
content the user manages, which the tree currently argues against in as many
words: `WallpapersCore.h:17` keeps `/wallpapers` undotted because the uploader
and the web UI both name that path to the user, and a dotted name "would read
as a system folder they should not touch".

**C. Both, with user content kept visible under a different root.** The most
coherent end state and the largest change: two new roots, every app touched,
and the docs and web UI re-pointed.

## What it would cost

**A migration, or orphaned data.** Every device in the field has files at
today's paths. Moving them needs a real migration that runs once on the device,
is interrupted safely (the card can be pulled mid-move), and is tested on
hardware. Without one, a user's saved articles and queue quietly stop existing.
There is a scar on this exact hazard in `SettingsList.h`: the
`triviaShowUsCentric` key stays where it is because it "shipped in v1.12.29 and
[is] already saved on devices, so moving either would silently discard a
preference somebody had chosen. Only the surface moved."

**Divergence from upstream.** `/.crosspoint/` is upstream's path, used by
upstream's code as well as ours. Moving our half means touching shared files
and keeping them diverged through every sync, which is the cost this repo
already works hardest to avoid (`FORK CHANGE:` notes, workflows kept in
upstream's shape).

**A second system folder, not one.** Upstream's `/.crosspoint/` does not go
away: settings, the EPUB cache and the reader's own state live there. Scope A
or C leaves a card with both `/.crosspoint/` and `/.crossplay/`, split by which
project's code wrote the file. That distinction is invisible and uninteresting
to the person holding the card.

**Burying what the user manages.** Scope B or C hides the Wikipedia pack, the
Trivia pack, the xkcd archive, the decks, the wallpapers and the notes. Those
are all things a person puts there and edits, and the docs name those paths.

## What it would buy

**One folder to remove.** `README.md` promises that installing stock
CrossPoint over the top "puts the device back where it was". Today the fork's
data is scattered and stays behind. With a single root, going back to stock is
one folder deleted. This is the strongest argument and it is real, though it
describes a rare operation.

**One folder to back up.** Same shape: copy `/.crossplay/` and you have the
decks, the packs, the notes and the saves.

**Provenance.** A reader of the card can tell which project put a file there.

## The alternative that costs nothing

The problems actually reported were both about the **shelf**, not about tidiness:
`.md` notes and a deck's `.txt` glyph sets turning up in the book list. Both
are governed by one rule, `isHiddenOrSidecar` in `LibraryBuilder.cpp:196`,
which skips any name starting with `.`, directories included, before `walk()`
descends. Neither needed anything moved:

- The deck glyph sets were renamed at the source, in the converter, and the
  readers accept the old spelling so nothing has to be re-converted.
- `/notes` could do the same, or the shelf could learn to skip a folder
  carrying a `.noindex` marker: one place, no data moved, folders stay
  visible and editable.

So the axis that matters is **content the user manages (visible) against data
the app derives (hidden)**, not CrossPlay against CrossPoint. The tree already
organises itself that way, mostly consistently; where it does not, those are
the bugs, and they are individually cheap to fix.

## Recommendation

**Do not take scope B or C.** They trade a daily cost (content the user cannot
find) for a rare benefit (a clean uninstall).

**Scope A is defensible but not yet worth it.** If the uninstall story is the
goal, the cheap version is a documented list of the paths the fork owns, in
`USER_GUIDE.md`, so a person going back to stock can delete them deliberately.
That buys most of the benefit for none of the risk, and it can be written
today.

If scope A is wanted anyway, the way to do it without a migration is the one
already in the tree twice: read both roots, prefer the new one, write only the
new one. `DictionaryRegistry.cpp:17` and `PluginLocations.h:11` are the
pattern. Old installs keep working, new ones land in the new place, and no
file is ever moved under a user's feet.

## Open questions

1. Which scope, if any?
2. Is the clean-uninstall story worth a migration at all, or does a documented
   path list close it?
3. If scope A: dual-root reads, or a real one-shot migration with the
   interruption handling that needs?
4. Does upstream want any of this? If `/.crosspoint/` is the agreed home for
   app data, keeping our apps there is the lower-friction answer and this whole
   proposal is moot.
