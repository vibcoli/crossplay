#pragma once
#include <cstdint>
#include <string>

// On-disk filename format for books downloaded from an OPDS server. Stored as a
// uint8_t in CrossPointSettings; cast to this enum at the call sites. `Count` is
// the number of selectable formats (used to cycle the setting in the UI).
enum class OpdsFilenameFormat : uint8_t {
  AuthorTitle = 0,     // "Author - Title.epub" (default; matches legacy behaviour)
  TitleAuthor = 1,     // "Title - Author.epub"
  TitleOnly = 2,       // "Title.epub"
  ServerFilename = 3,  // whatever the server calls the file (see opdsServerFilename)
  Count = 4,
};

// Composes and sanitizes the on-disk filename (including the ".epub" extension)
// for a downloaded OPDS book, according to `format`. When the author is empty,
// every format collapses to just the sanitized title. Pure: no I/O, no globals.
//
// `ServerFilename` is not composed from metadata at all, so it falls through to
// the `AuthorTitle` default here; it is resolved by opdsServerFilename(), and
// this is the fallback when that returns nothing.
std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format);

// Derives the filename the server itself hands out for an acquisition, from the
// response headers of a probe request plus the URL that was probed. Returns ""
// when nothing usable can be derived, so the caller can fall back to a
// metadata-composed name.
//
// WHY THIS EXISTS, and why it copies another program's quirks: KOReader's
// progress sync can identify a document by the md5 of its *filename* instead of
// by a partial hash of its contents. Filename matching is the only method that
// survives a re-download or a different conversion of the same book, but it
// only works while both devices store the book under the same name. KOReader
// (OPDS catalog option "Use server filenames") derives that name in
// plugins/opds.koplugin/opdsbrowser.lua getServerFileName(); this reproduces
// its order and its limits deliberately, including:
//   * only the plain `filename` Content-Disposition parameter is read, never
//     RFC 5987's `filename*`, so a server that sends only the extended form
//     falls through to the Location/URL steps on both sides;
//   * the extension is fixed up BEFORE percent-decoding, not after.
// A "more correct" reading here would produce a better filename and a hash that
// matches nothing.
//
// `contentDisposition` and `location` are the raw header values ("" when
// absent); `url` is the probed acquisition URL. Pure: no I/O, no globals.
std::string opdsServerFilename(const std::string& contentDisposition, const std::string& location,
                               const std::string& url);
