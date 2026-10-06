#include <gtest/gtest.h>

#include <string>

#include "OpdsFilename.h"
#include "StringUtils.h"

namespace {

TEST(OpdsFilename, AuthorTitleIsDefaultOrder) {
  EXPECT_EQ(opdsBookFilename("J. Doe", "My Book", OpdsFilenameFormat::AuthorTitle), "J. Doe - My Book.epub");
}

TEST(OpdsFilename, TitleAuthorSwapsOrder) {
  EXPECT_EQ(opdsBookFilename("J. Doe", "My Book", OpdsFilenameFormat::TitleAuthor), "My Book - J. Doe.epub");
}

TEST(OpdsFilename, TitleOnlyIgnoresAuthor) {
  EXPECT_EQ(opdsBookFilename("J. Doe", "My Book", OpdsFilenameFormat::TitleOnly), "My Book.epub");
}

TEST(OpdsFilename, EmptyAuthorCollapsesToTitleForEveryFormat) {
  EXPECT_EQ(opdsBookFilename("", "My Book", OpdsFilenameFormat::AuthorTitle), "My Book.epub");
  EXPECT_EQ(opdsBookFilename("", "My Book", OpdsFilenameFormat::TitleAuthor), "My Book.epub");
  EXPECT_EQ(opdsBookFilename("", "My Book", OpdsFilenameFormat::TitleOnly), "My Book.epub");
}

TEST(OpdsFilename, IllegalCharactersAreSanitized) {
  // '/' ':' '*' '?' etc. are replaced with '_' by sanitizeFilename.
  EXPECT_EQ(opdsBookFilename("A/B", "C:D*E?", OpdsFilenameFormat::AuthorTitle), "A_B - C_D_E_.epub");
}

TEST(OpdsFilename, EmptyAuthorAndTitleFallsBackToBook) {
  // sanitizeFilename returns "book" when nothing usable remains.
  EXPECT_EQ(opdsBookFilename("", "", OpdsFilenameFormat::AuthorTitle), "book.epub");
  EXPECT_EQ(opdsBookFilename("", "", OpdsFilenameFormat::TitleOnly), "book.epub");
}

TEST(OpdsFilename, LongNameIsTruncatedToByteBudgetBeforeExtension) {
  // sanitizeFilename caps the base at 100 bytes; ".epub" is appended after.
  const std::string longTitle(200, 'a');
  const std::string result = opdsBookFilename("", longTitle, OpdsFilenameFormat::TitleOnly);
  EXPECT_EQ(result, std::string(100, 'a') + ".epub");
  EXPECT_EQ(result.size(), 105u);
}

TEST(OpdsFilename, UnknownFormatValueFallsBackToAuthorTitle) {
  // Defensive: a persisted value outside the enum still yields a valid name.
  const auto bogus = static_cast<OpdsFilenameFormat>(99);
  EXPECT_EQ(opdsBookFilename("J. Doe", "My Book", bogus), "J. Doe - My Book.epub");
}

TEST(OpdsFilename, ServerFilenameFormatFallsBackToAuthorTitle) {
  // The server name is resolved by opdsServerFilename(); when a server offers
  // none, this is what the caller composes instead.
  EXPECT_EQ(opdsBookFilename("J. Doe", "My Book", OpdsFilenameFormat::ServerFilename), "J. Doe - My Book.epub");
}

TEST(OpdsFilename, CountCoversEverySelectableFormat) {
  // The option picker shows Count entries; one short hides a format, one over
  // selects a value nothing maps to.
  EXPECT_EQ(static_cast<int>(OpdsFilenameFormat::Count), 4);
}

// --- opdsServerFilename: KOReader's getServerFileName, step for step ---

TEST(OpdsServerFilename, QuotedDispositionWins) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"J. Doe - My Book.epub\"", "", "https://h/get/42/library"),
            "J. Doe - My Book.epub");
}

TEST(OpdsServerFilename, UnquotedDispositionStopsAtParameterSeparator) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=My Book.epub; size=1234", "", "https://h/get/42"), "My Book.epub");
}

TEST(OpdsServerFilename, UnquotedDispositionRunsToEndOfHeader) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=My Book.epub", "", "https://h/get/42"), "My Book.epub");
}

TEST(OpdsServerFilename, ExtendedDispositionParameterIsIgnored) {
  // RFC 5987's filename* is the correct place for a non-ASCII name, and
  // reading it would give a BETTER filename than KOReader picks -- and a
  // filename hash that matches no other device. The URL step answers instead.
  EXPECT_EQ(opdsServerFilename("attachment; filename*=UTF-8''Caf%C3%A9.epub", "", "https://h/books/1.epub"), "1.epub");
}

TEST(OpdsServerFilename, PlainParameterPreferredOverExtendedOne) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"Cafe.epub\"; filename*=UTF-8''Caf%C3%A9.epub", "",
                               "https://h/books/1.epub"),
            "Cafe.epub");
}

TEST(OpdsServerFilename, UnterminatedQuoteFallsBackToUnquotedRead) {
  // The stray quote is a filesystem-illegal character, not a delimiter.
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"My Book.epub", "", "https://h/get/42"), "_My Book.epub");
}

TEST(OpdsServerFilename, RedirectTargetNamesTheFileWhenDispositionDoesNot) {
  EXPECT_EQ(opdsServerFilename("", "/files/a1b2/My Book.epub", "https://h/get/42"), "My Book.epub");
}

TEST(OpdsServerFilename, UrlIsTheLastResort) {
  EXPECT_EQ(opdsServerFilename("", "", "https://h/books/My%20Book.epub?token=abc"), "My Book.epub");
}

TEST(OpdsServerFilename, PathIsTrimmedBeforeTheQuery) {
  // A '/' inside the query counts as the last slash: KOReader strips the path
  // first and the query second, and reversing the two would yield "get.epub".
  EXPECT_EQ(opdsServerFilename("", "", "https://h/get?path=/shelf/Deep.epub"), "Deep.epub");
}

TEST(OpdsServerFilename, ExtensionIsAppendedWhenTheServerOmitsIt) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"My Book\"", "", "https://h/get/42"), "My Book.epub");
  EXPECT_EQ(opdsServerFilename("", "", "https://h/get/EPUB/42/library"), "library.epub");
}

TEST(OpdsServerFilename, ExistingExtensionIsKeptWhateverItsCase) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"MY BOOK.EPUB\"", "", "https://h/get/42"), "MY BOOK.EPUB");
}

TEST(OpdsServerFilename, PercentEscapesAreDecoded) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"Caf%C3%A9.epub\"", "", "https://h/get/42"), "Café.epub");
}

TEST(OpdsServerFilename, IncompleteEscapesAndPlusSignsStayLiteral) {
  // '+' means space only in a query string, and a title may contain either.
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"50% C++.epub\"", "", "https://h/get/42"), "50% C++.epub");
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"Done%2.epub\"", "", "https://h/get/42"), "Done%2.epub");
}

TEST(OpdsServerFilename, IllegalCharactersAreSanitizedAndTheExtensionSurvives) {
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"A/B:C.epub\"", "", "https://h/get/42"), "A_B_C.epub");
}

TEST(OpdsServerFilename, LongNameIsShortenedWithoutLosingTheExtension) {
  const std::string disposition = "attachment; filename=\"" + std::string(200, 'a') + ".epub\"";
  const std::string result = opdsServerFilename(disposition, "", "https://h/get/42");
  EXPECT_EQ(result, std::string(95, 'a') + ".epub");
  EXPECT_EQ(result.size(), 100u);
}

TEST(OpdsServerFilename, NothingUsableReturnsEmptyForTheCallerToFallBack) {
  // A collection URL with no final segment and no headers: "book.epub" would
  // collide with every other nameless download and hash to nothing useful.
  EXPECT_EQ(opdsServerFilename("", "", "https://h/opds/download/"), "");
  EXPECT_EQ(opdsServerFilename("attachment", "", "https://h/opds/download/"), "");
  EXPECT_EQ(opdsServerFilename("attachment; filename=\"\"", "", "https://h/opds/download/"), "");
}

TEST(PluginFilename, PreservesWebDavExtension) {
  const std::string name = "Shadow Divers (Robert Kurson) (z-library.sk, 1lib.sk, z-lib.sk).epub";
  EXPECT_EQ(StringUtils::sanitizeFilenamePreservingExtension(name), name);
}

TEST(PluginFilename, TruncatesStemBeforeExtension) {
  const std::string name = std::string(200, 'a') + ".epub";
  const std::string result = StringUtils::sanitizeFilenamePreservingExtension(name);
  EXPECT_EQ(result, std::string(95, 'a') + ".epub");
  EXPECT_EQ(result.size(), 100u);
}

TEST(PluginFilename, TruncatesUtf8StemOnCodepointBoundary) {
  std::string title;
  for (int i = 0; i < 20; i++) title += "\xC3\xA9";
  title += ".epub";
  const std::string result = StringUtils::sanitizeFilenamePreservingExtension(title, 20);
  EXPECT_EQ(result, title.substr(0, 14) + ".epub");
  EXPECT_EQ(result.size(), 19u);
  EXPECT_EQ(result.substr(result.size() - 5), ".epub");
}

}  // namespace
