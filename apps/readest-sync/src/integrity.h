#pragma once
#include <string>
namespace readest {
struct BookIntegrity { std::string readest_hash, sha256; long long size = 0; };
// Reads only: bounded archive validation, OCF/package checks, DRM rejection,
// Readest partial-MD5 and whole-file SHA-256. Never extracts files to disk.
// Fast candidate identity: at most twelve 1 KiB samples; not integrity validation.
std::string epub_fingerprint(const std::string& path);
BookIntegrity inspect_epub(const std::string& path);
// Resolve a CREngine element/text XPointer in the original EPUB. Throws when
// structure or offset cannot be resolved exactly; never uses percentages.
std::string xpointer_cfi(const std::string& path, const std::string& xpointer);
} // namespace readest
