#include "AppDataRootCore.h"

namespace appdata {

std::string hiddenSibling(const std::string& visibleRoot) {
  // Only a plain card-root folder has a sibling to look for: "/notes" pairs
  // with "/.notes". A nested path ("/.crosspoint/hn") is already where the
  // user cannot see it, and a root that is itself dotted is the hidden name.
  if (visibleRoot.size() < 2 || visibleRoot.front() != '/') return visibleRoot;
  if (visibleRoot.find('/', 1) != std::string::npos) return visibleRoot;
  if (visibleRoot[1] == '.') return visibleRoot;
  return "/." + visibleRoot.substr(1);
}

bool preferHidden(const bool visibleExists, const bool hiddenExists) {
  (void)visibleExists;  // the dotted name wins wherever it exists; see the header
  return hiddenExists;
}

}  // namespace appdata
