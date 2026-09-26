#pragma once
#include <string>
#include <sys/stat.h>
namespace readest {
std::string epub_file_stamp(const struct stat& st);
struct EpubMetadata { std::string title, author, cover, cover_type; };
// Reads only bounded package metadata; full validation remains an operation boundary.
EpubMetadata epub_metadata(const std::string& path, bool cover=false);
struct BookIntegrity { std::string readest_hash, sha256; long long size = 0; };
// Reads only: bounded archive validation, OCF/package checks, DRM rejection,
// Readest partial-MD5 and whole-file SHA-256. Never extracts files to disk.
// Fast candidate identity: at most twelve 1 KiB samples; not integrity validation.
std::string epub_fingerprint(const std::string& path);
BookIntegrity inspect_epub(const std::string& path);
// Resolve a CREngine element/text XPointer in the original EPUB. Throws when
// structure or offset cannot be resolved exactly; never uses percentages.
std::string xpointer_cfi(const std::string& path, const std::string& xpointer);
// Verify exact text-node range against original EPUB bytes (UTF-16 offsets).
void validate_annotation_range(const std::string& path,const std::string& begin,
                               const std::string& end,const std::string& selected);
} // namespace readest
