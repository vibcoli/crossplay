#include "AppDataRoot.h"

#include <HalStorage.h>
#include <Logging.h>

namespace appdata {
namespace {

// One entry per card-root app folder. Six exist today (notes, wikipedia,
// trivia, xkcd, study, wallpapers); twelve leaves room without being a number
// anyone has to maintain.
//
// A FIXED ARRAY, and not a vector, because root() hands out c_str() into these
// strings. Growing a vector moves its elements, and a short name like
// "/.notes" lives in the string's own small-buffer storage -- so every pointer
// handed out before a growth would dangle. A fixed array never moves.
//
// `key` is compared by POINTER, not with strcmp: every call site passes a
// string literal, so one folder is always one address, and a lookup costs a
// handful of pointer compares rather than a string walk. That is also why the
// header requires static storage duration.
constexpr int kMaxRoots = 12;

struct Entry {
  const char* key = nullptr;
  std::string resolved;
};

Entry g_cache[kMaxRoots];

}  // namespace

const char* root(const char* visibleRoot) {
  if (!visibleRoot || !*visibleRoot) return visibleRoot;

  for (const Entry& entry : g_cache) {
    if (entry.key == visibleRoot) return entry.resolved.c_str();
  }

  const std::string hidden = hiddenSibling(visibleRoot);
  std::string chosen = visibleRoot;
  // hiddenSibling returns its input when there is no second name to look for,
  // and then there is nothing worth asking the card about.
  if (hidden != visibleRoot && preferHidden(Storage.exists(visibleRoot), Storage.exists(hidden.c_str()))) {
    chosen = hidden;
    LOG_INF("APPDIR", "%s is hidden as %s", visibleRoot, chosen.c_str());
  }

  for (Entry& entry : g_cache) {
    if (entry.key) continue;
    entry.key = visibleRoot;
    entry.resolved = std::move(chosen);
    return entry.resolved.c_str();
  }

  // Unreachable while kMaxRoots exceeds the number of distinct roots in the
  // tree, which is fixed at compile time -- so this is a code change away, not
  // a runtime condition. It says so rather than quietly answering the visible
  // name, which would be the wrong folder for anyone who hid theirs.
  LOG_ERR("APPDIR", "root cache full at %d entries; raise kMaxRoots (%s unresolved)", kMaxRoots, visibleRoot);
  return visibleRoot;
}

std::string path(const char* visibleRoot, const std::string& leaf) {
  return std::string(root(visibleRoot)) + "/" + leaf;
}

}  // namespace appdata
