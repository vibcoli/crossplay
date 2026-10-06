#pragma once
#include <string>

#include "AppDataRootCore.h"

// Card-root app folders under two names, the way CrossPoint already treats
// several of its own: "/dictionaries" or "/.dictionaries"
// (DictionaryRegistry.cpp), "/plugins" or "/.plugins" (PluginLocations.h). A
// user who wants a folder out of the way renames it with a leading dot and the
// app keeps finding it.
//
// WHY IT IS WORTH MORE THAN TIDINESS. The shelf indexes .epub, .txt, .md and
// .xtc (LibraryBuilder isBookName) and skips any entry whose name starts with
// '.' -- directories included, before it ever descends (isHiddenOrSidecar, and
// its use in walk()). So dotting a folder keeps its text files out of the book
// list, recursively, and does so regardless of the Show Hidden Files setting,
// which governs only the file browser and the web file list.
//
// Renaming could not achieve that before this existed: every app spelled its
// folder once as a constant, so it would recreate the visible name, write
// there, and leave the renamed folder behind as data nothing reads.
//
// Folders already under /.crosspoint (Hacker News, Instapaper, the Wikipedia
// article cache) need none of this, and content the user puts on the card
// themselves still arrives at the visible name by default -- a folder nobody
// renamed behaves exactly as it did.
namespace appdata {

// Whichever of `visibleRoot` and its dotted sibling is on the card; the
// visible name when neither is, so a first run creates what it always did.
//
// `visibleRoot` MUST have static storage duration -- a string literal, which
// is what every call site passes. The cache keys on the pointer, and the
// returned pointer stays valid for the rest of the session.
//
// Resolved once per root, because callers build paths from it inside loops and
// every miss would cost an SD stat. A folder hidden while the device is
// running therefore takes effect at the next boot.
const char* root(const char* visibleRoot);

// root(visibleRoot) + "/" + leaf, for the paths apps derive from their folder.
std::string path(const char* visibleRoot, const std::string& leaf);

}  // namespace appdata
