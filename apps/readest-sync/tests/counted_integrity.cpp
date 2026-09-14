// Compile the real validator under a second name so operation tests can count
// full inspections without changing production code or mocking validation.
#define inspect_epub inspect_epub_impl
#include "../src/integrity.cpp"
#undef inspect_epub
namespace readest {
unsigned full_inspections = 0;
BookIntegrity inspect_epub(const std::string& path) {
    ++full_inspections;
    return inspect_epub_impl(path);
}
}
