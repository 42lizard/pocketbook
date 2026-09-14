#pragma once
#include <string>
namespace readest {
struct BookIntegrity { std::string readest_hash, sha256; long long size = 0; };
// Reads only: bounded archive validation, OCF/package checks, DRM rejection,
// Readest partial-MD5 and whole-file SHA-256. Never extracts files to disk.
BookIntegrity inspect_epub(const std::string& path);
// Resolve a CREngine element/text XPointer in the original EPUB. Throws when
// structure or offset cannot be resolved exactly; never uses percentages.
std::string xpointer_cfi(const std::string& path, const std::string& xpointer);
} // namespace readest
