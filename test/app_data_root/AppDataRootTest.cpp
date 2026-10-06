#include <gtest/gtest.h>

#include <string>

#include "AppDataRootCore.h"

namespace {

TEST(HiddenSibling, DotsACardRootFolder) {
  EXPECT_EQ(appdata::hiddenSibling("/notes"), "/.notes");
  EXPECT_EQ(appdata::hiddenSibling("/wikipedia"), "/.wikipedia");
  EXPECT_EQ(appdata::hiddenSibling("/wallpapers"), "/.wallpapers");
}

TEST(HiddenSibling, AnAlreadyHiddenRootIsItsOwnSibling) {
  // Returning "/..notes" here would send an app looking for a folder no user
  // would ever create.
  EXPECT_EQ(appdata::hiddenSibling("/.notes"), "/.notes");
  EXPECT_EQ(appdata::hiddenSibling("/.sleep"), "/.sleep");
}

TEST(HiddenSibling, NestedPathsHaveNoSibling) {
  // Already out of sight, and dotting the first segment would point at a
  // folder that does not exist: "/.crosspoint/hn" is where it lives.
  EXPECT_EQ(appdata::hiddenSibling("/.crosspoint/hn"), "/.crosspoint/hn");
  EXPECT_EQ(appdata::hiddenSibling("/notes/drafts"), "/notes/drafts");
}

TEST(HiddenSibling, DegenerateInputsComeBackUnchanged) {
  EXPECT_EQ(appdata::hiddenSibling(""), "");
  EXPECT_EQ(appdata::hiddenSibling("/"), "/");
  EXPECT_EQ(appdata::hiddenSibling("notes"), "notes");
}

TEST(PreferHidden, TheDottedFolderWinsWhereverItExists) {
  // Both present is the ordinary case after a user hides theirs and opens the
  // app once: the app recreated the visible folder, empty, and the renamed one
  // holds the data. Nothing in the firmware creates the dotted name, so it is
  // there because somebody put it there.
  EXPECT_TRUE(appdata::preferHidden(/*visibleExists=*/true, /*hiddenExists=*/true));
  EXPECT_TRUE(appdata::preferHidden(false, true));
}

TEST(PreferHidden, WithoutADottedFolderNothingChanges) {
  EXPECT_FALSE(appdata::preferHidden(true, false));
  EXPECT_FALSE(appdata::preferHidden(false, false));
}

}  // namespace
