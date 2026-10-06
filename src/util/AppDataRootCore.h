#pragma once
#include <string>

// The decisions behind appdata::root(), with no I/O so they can be host-tested.
// See AppDataRoot.h for what this is for.
namespace appdata {

// "/notes" -> "/.notes". A root that is already dotted, or that is not a
// single card-root folder, comes back unchanged -- there is no second name to
// look for in either case.
std::string hiddenSibling(const std::string& visibleRoot);

// Which of the two names to use, given whether each is on the card.
//
// The dotted one wins when BOTH exist, and that is the case worth explaining:
// an app recreates its visible folder the first time it runs, so "both exist"
// is exactly what a user sees after hiding theirs and opening the app once.
// The folder they renamed holds the data; the one the app made is empty.
// Nothing in the firmware ever creates the dotted name, so its presence is
// always a deliberate act by the user.
bool preferHidden(bool visibleExists, bool hiddenExists);

}  // namespace appdata
