#include "integrity.h"
#include "probe.h"
#include <functional>
#include "vendor/miniz/miniz.h"
#include <openssl/evp.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <regex>
#include <map>

namespace readest {
namespace {
struct Digest {
    EVP_MD_CTX* ctx;
    explicit Digest(const EVP_MD* type) {
        ctx = EVP_MD_CTX_new();
        if (!ctx) throw std::runtime_error("Cannot allocate book digest");
        if (EVP_DigestInit_ex(ctx, type, nullptr) != 1) {
            EVP_MD_CTX_free(ctx); throw std::runtime_error("Cannot initialize book digest");
        }
    }
    ~Digest() { EVP_MD_CTX_free(ctx); }
    void add(const void* bytes, size_t length) {
        if (EVP_DigestUpdate(ctx, bytes, length) != 1) throw std::runtime_error("Book digest failed");
    }
    std::string finish() {
        unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned int size = 0;
        if (EVP_DigestFinal_ex(ctx, bytes, &size) != 1) throw std::runtime_error("Book digest failed");
        const char* hex = "0123456789abcdef"; std::string result;
        for (unsigned int i = 0; i < size; ++i) { result += hex[bytes[i] >> 4]; result += hex[bytes[i] & 15]; }
        return result;
    }
};
struct Zip {
    mz_zip_archive zip = {};
    explicit Zip(FILE* file, size_t size) {
        if (!mz_zip_reader_init_cfile(&zip, file, size, 0)) throw std::runtime_error("Invalid EPUB ZIP archive");
    }
    ~Zip() { mz_zip_reader_end(&zip); }
    std::string read(const std::string& name, size_t limit) {
        int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        mz_zip_archive_file_stat st;
        if (index < 0 || !mz_zip_reader_file_stat(&zip, index, &st) || !st.m_uncomp_size || st.m_uncomp_size > limit)
            throw std::runtime_error("Missing or oversized EPUB metadata");
        std::string bytes(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, index, &bytes[0], bytes.size(), 0))
            throw std::runtime_error("Invalid EPUB metadata contents");
        return bytes;
    }
};
bool safe_path(const std::string& s) {
    return !s.empty() && s[0] != '/' && s.find('\\') == std::string::npos &&
        s.find('\0') == std::string::npos && s.find(':') == std::string::npos &&
        s != ".." && s.compare(0, 3, "../") != 0 && s.find("/../") == std::string::npos &&
        (s.size() < 3 || s.substr(s.size() - 3) != "/..");
}
using Xml = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
Xml xml(const std::string& text) {
    Xml doc(xmlReadMemory(text.data(), static_cast<int>(text.size()), "book.xml", nullptr,
                         XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING), xmlFreeDoc);
    if (!doc || doc->intSubset || doc->extSubset) throw std::runtime_error("Unsupported EPUB metadata XML");
    return doc;
}
bool named(xmlNode* n, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && xmlStrEqual(n->name, BAD_CAST name);
}
std::string property(xmlNode* n, const char* name) {
    xmlChar* v = xmlGetProp(n, BAD_CAST name);
    std::string s = v ? reinterpret_cast<char*>(v) : "";
    xmlFree(v); return s;
}
void check_encryption(xmlNode* n) {
    for (; n; n = n->next) {
        if (named(n, "EncryptionMethod")) {
            auto algorithm = property(n, "Algorithm");
            if (algorithm != "http://www.idpf.org/2008/embedding" &&
                algorithm != "http://ns.adobe.com/pdf/enc#RC")
                throw std::runtime_error("DRM-protected EPUB is unsupported");
        }
        check_encryption(n->children);
    }
}
}

std::string epub_file_stamp(const struct stat& st) {
#ifdef __APPLE__
    const auto modified=st.st_mtimespec,changed=st.st_ctimespec;
#else
    const auto modified=st.st_mtim,changed=st.st_ctim;
#endif
    return std::to_string(st.st_dev)+":"+std::to_string(st.st_ino)+":"+std::to_string(st.st_size)+":"+
        std::to_string(modified.tv_sec)+":"+std::to_string(modified.tv_nsec)+":"+
        std::to_string(changed.tv_sec)+":"+std::to_string(changed.tv_nsec);
}
std::string epub_fingerprint(const std::string& path) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) throw std::runtime_error("Cannot open EPUB candidate");
    FILE* raw=fdopen(fd,"rb");
    if(!raw) { close(fd); throw std::runtime_error("Cannot read EPUB candidate"); }
    const auto close_file=[](FILE* f) { fclose(f); };
    std::unique_ptr<FILE,decltype(close_file)> file(raw,close_file);
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>256LL*1024*1024)
        throw std::runtime_error("Unsupported EPUB candidate");
    Digest partial(EVP_md5()); char bytes[1024];
    for(int i=-1;i<=10;++i) {
        const long long offset=i<0?0:1024LL<<(2*i);
        if(offset>=st.st_size) break;
        const auto count=static_cast<size_t>(std::min(1024LL,st.st_size-offset));
        if(fseeko(raw,offset,SEEK_SET)!=0 || fread(bytes,1,count,raw)!=count)
            throw std::runtime_error("Cannot fingerprint EPUB candidate");
        partial.add(bytes,count);
    }
    return partial.finish();
}
BookIntegrity inspect_epub(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("Cannot open downloaded EPUB");
    struct stat before;
    if (fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size <= 0 ||
        before.st_size > 256LL * 1024 * 1024) {
        close(fd); throw std::runtime_error("Unsupported EPUB file size");
    }
    FILE* raw = fdopen(fd, "rb");
    if (!raw) { close(fd); throw std::runtime_error("Cannot read EPUB"); }
    std::unique_ptr<FILE, int(*)(FILE*)> file(raw, fclose);
    BookIntegrity result; result.size = before.st_size;
    Digest partial(EVP_md5()), whole(EVP_sha256());
    char bytes[32768];
    // JS's 1024 << (2 * -1) is zero (32-bit shift), then 1K, 4K, 16K...
    for (int i = -1; i <= 10; ++i) {
        const long long offset = i < 0 ? 0 : 1024LL << (2 * i);
        if (offset >= result.size) break;
        const size_t count = static_cast<size_t>(std::min(1024LL, result.size - offset));
        if (fseeko(raw, offset, SEEK_SET) != 0 || fread(bytes, 1, count, raw) != count)
            throw std::runtime_error("Cannot fingerprint EPUB");
        partial.add(bytes, count);
    }
    rewind(raw);
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), raw)) > 0) whole.add(bytes, count);
    if (ferror(raw)) throw std::runtime_error("Cannot hash EPUB");
    result.readest_hash = partial.finish(); result.sha256 = whole.finish();
    rewind(raw);
    Zip archive(raw, static_cast<size_t>(result.size));
    if (archive.zip.m_total_files > 20000) throw std::runtime_error("Too many EPUB entries");
    std::set<std::string> names;
    mz_uint64 expanded = 0;
    for (mz_uint i = 0; i < archive.zip.m_total_files; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive.zip, i, &st) || st.m_is_encrypted || !st.m_is_supported)
            throw std::runtime_error("Unsupported EPUB archive entry");
        const auto length = mz_zip_reader_get_filename(&archive.zip, i, nullptr, 0);
        if (!length || length > 4096) throw std::runtime_error("Invalid EPUB entry name");
        std::vector<char> name(length);
        mz_zip_reader_get_filename(&archive.zip, i, name.data(), length);
        const std::string entry(name.data(), length - 1);
        if (!safe_path(entry) || !names.insert(entry).second) throw std::runtime_error("Ambiguous EPUB archive paths");
        expanded += st.m_uncomp_size;
        if (st.m_uncomp_size > 64ULL * 1024 * 1024 || expanded > 512ULL * 1024 * 1024)
            throw std::runtime_error("EPUB expansion limit exceeded");
        if (entry == "mimetype" && (st.m_method != 0 || st.m_local_header_ofs != 0))
            throw std::runtime_error("Invalid EPUB mimetype entry");
    }
    if (archive.read("mimetype", 20) != "application/epub+zip") throw std::runtime_error("Not an EPUB book");
    if (!mz_zip_validate_archive(&archive.zip, 0)) throw std::runtime_error("EPUB integrity check failed");
    auto container = xml(archive.read("META-INF/container.xml", 1024 * 1024));
    auto* root = xmlDocGetRootElement(container.get());
    if (!named(root, "container")) throw std::runtime_error("Invalid EPUB container");
    std::string package_path;
    for (auto* children = root->children; children; children = children->next) {
        if (!named(children, "rootfiles")) continue;
        for (auto* child = children->children; child; child = child->next) {
            if (named(child, "rootfile") && property(child, "media-type") == "application/oebps-package+xml") {
                if (!package_path.empty()) throw std::runtime_error("Multiple EPUB renditions are unsupported");
                package_path = property(child, "full-path");
            }
        }
    }
    if (!safe_path(package_path)) throw std::runtime_error("Invalid EPUB package path");
    auto package = xml(archive.read(package_path, 4 * 1024 * 1024));
    if (!named(xmlDocGetRootElement(package.get()), "package")) throw std::runtime_error("Invalid EPUB package");
    if (names.count("META-INF/encryption.xml")) {
        auto encryption = xml(archive.read("META-INF/encryption.xml", 1024 * 1024));
        check_encryption(xmlDocGetRootElement(encryption.get()));
    }
    struct stat after;
    if (fstat(fd, &after) != 0 || before.st_size != after.st_size || before.st_mtime != after.st_mtime)
        throw std::runtime_error("EPUB changed during validation");
    return result;
}

namespace {
std::string zip_relative(const std::string& base, const std::string& href) {
    std::string decoded;
    for(size_t i=0;i<href.size();++i) {
        if(href[i]=='%') {
            if(i+2>=href.size() || !isxdigit(static_cast<unsigned char>(href[i+1])) || !isxdigit(static_cast<unsigned char>(href[i+2])))
                throw std::runtime_error("Invalid EPUB resource path");
            decoded+=static_cast<char>(std::stoi(href.substr(i+1,2),nullptr,16)); i+=2;
        } else decoded+=href[i];
    }
    if(decoded.empty() || decoded[0]=='/' || decoded.find_first_of("\\:#?")!=std::string::npos || decoded.find('\0')!=std::string::npos)
        throw std::runtime_error("Unsupported EPUB resource path");
    std::vector<std::string> parts; const auto joined=base+decoded;
    for(size_t at=0;at<joined.size();) {
        auto end=joined.find('/',at); if(end==std::string::npos) end=joined.size();
        auto part=joined.substr(at,end-at); at=end+1;
        if(part=="..") { if(parts.empty()) throw std::runtime_error("Invalid EPUB resource path"); parts.pop_back(); }
        else if(!part.empty() && part!=".") parts.push_back(part);
    }
    std::string result; for(const auto& part:parts) { if(!result.empty()) result+='/'; result+=part; }
    return result;
}
xmlNode* child(xmlNode* parent,const std::string& name,unsigned index=1) {
    if(!index || !parent) throw std::runtime_error("Invalid XPointer element index");
    for(auto* n=parent->children;n;n=n->next) if(named(n,name.c_str()) && --index==0) return n;
    throw std::runtime_error("XPointer element is absent from this EPUB");
}
std::string cfi_element(xmlNode* node,xmlNode* html) {
    if(node==html) return "";
    if(!node || !node->parent || node->type!=XML_ELEMENT_NODE) throw std::runtime_error("Invalid XPointer target");
    unsigned index=1;
    for(auto* n=node->prev;n;n=n->prev) if(n->type==XML_ELEMENT_NODE) ++index;
    return cfi_element(node->parent,html)+"/"+std::to_string(index*2);
}
Xml chapter_xml(std::string text) {
    // XHTML commonly references HTML entities via an external DTD. Resolve
    // only the standard named HTML entities locally; never load a DTD.
    auto at=text.find("<!DOCTYPE");
    if(at!=std::string::npos) {
        char quote=0; size_t end=at+9;
        for(;end<text.size();++end) {
            char c=text[end];
            if(quote) { if(c==quote) quote=0; }
            else if(c=='\'' || c=='"') quote=c;
            else if(c=='[') throw std::runtime_error("Custom EPUB entities are unsupported for XPointer conversion");
            else if(c=='>') break;
        }
        if(end==text.size()) throw std::runtime_error("Invalid EPUB document type");
        text.erase(at,end-at+1);
    }
    for(size_t i=0;(i=text.find('&',i))!=std::string::npos;) {
        auto end=text.find(';',i+1);
        if(end==std::string::npos || end-i>32) { ++i; continue; }
        auto name=text.substr(i+1,end-i-1);
        if(name!="amp" && name!="lt" && name!="gt" && name!="quot" && name!="apos") {
            const auto* entity=htmlEntityLookup(BAD_CAST name.c_str());
            if(entity) { auto replacement="&#"+std::to_string(entity->value)+";"; text.replace(i,end-i+1,replacement); i+=replacement.size(); continue; }
        }
        i=end+1;
    }
    return xml(text);
}
bool blank(unsigned c) { return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\f'; }
std::vector<unsigned> utf16(const xmlChar* value) {
    const std::string text=reinterpret_cast<const char*>(value?value:BAD_CAST "");
    std::vector<unsigned> units;
    for(size_t at=0;at<text.size();) {
        unsigned c=static_cast<unsigned char>(text[at++]); unsigned more=0;
        if(c>=0xf0) {c&=7;more=3;} else if(c>=0xe0){c&=15;more=2;} else if(c>=0xc0){c&=31;more=1;}
        for(unsigned i=0;i<more;++i) { if(at>=text.size()) throw std::runtime_error("Invalid EPUB text"); c=(c<<6)|(static_cast<unsigned char>(text[at++])&63); }
        if(c>0xffff) {c-=0x10000;units.push_back(0xd800+(c>>10));units.push_back(0xdc00+(c&1023));}
        else units.push_back(c);
    }
    return units;
}
}
EpubMetadata epub_metadata(const std::string& path,bool cover) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) throw std::runtime_error("Cannot read EPUB metadata");
    FILE* raw=fdopen(fd,"rb"); if(!raw) {close(fd); throw std::runtime_error("Cannot read EPUB metadata");}
    std::unique_ptr<FILE,int(*)(FILE*)> file(raw,fclose);
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>256LL*1024*1024)
        throw std::runtime_error("Unsupported EPUB metadata source");
    Zip zip(raw,st.st_size);
    auto container=xml(zip.read("META-INF/container.xml",1024*1024));
    auto package_path=property(child(child(xmlDocGetRootElement(container.get()),"rootfiles"),"rootfile"),"full-path");
    if(!safe_path(package_path)) throw std::runtime_error("Invalid EPUB package path");
    auto package=xml(zip.read(package_path,4*1024*1024));
    auto* root=xmlDocGetRootElement(package.get());
    auto* metadata=child(root,"metadata"); EpubMetadata result; std::string cover_id;
    for(auto* n=metadata->children;n;n=n->next) {
        if(named(n,"title") || named(n,"creator")) {
            xmlChar* text=xmlNodeGetContent(n); std::string value=text?reinterpret_cast<char*>(text):""; xmlFree(text);
            if(value.size()>4096) value.resize(4096);
            if(named(n,"title") && result.title.empty()) result.title=value;
            if(named(n,"creator") && result.author.empty()) result.author=value;
        }
        if(named(n,"meta") && property(n,"name")=="cover") cover_id=property(n,"content");
    }
    if(cover) for(auto* n=child(root,"manifest")->children;n;n=n->next) {
        if(!named(n,"item")) continue;
        const auto props=" "+property(n,"properties")+" ";
        if((!cover_id.empty() && property(n,"id")==cover_id) || props.find(" cover-image ")!=std::string::npos) {
            const auto type=property(n,"media-type");
            if(type!="image/jpeg" && type!="image/png") continue;
            auto slash=package_path.rfind('/');
            result.cover=zip.read(zip_relative(slash==std::string::npos?"":package_path.substr(0,slash+1),property(n,"href")),2*1024*1024);
            result.cover_type=type; break;
        }
    }
    return result;
}
std::string xpointer_cfi(const std::string& path,const std::string& pointer) {
    if(pointer.size()>8192) throw std::runtime_error("Oversized XPointer");
    static const std::regex prefix(R"(^/body(?:\[1\])?/DocFragment(?:\[([0-9]+)\])?/body(?:\[1\])?(.*)$)");
    std::smatch match;
    if(!std::regex_match(pointer,match,prefix)) throw std::runtime_error("Unsupported KOReader XPointer prefix");
    auto number=[](const std::string& s)->unsigned {
        if(s.empty() || s.size()>7) throw std::runtime_error("Invalid XPointer number");
        return static_cast<unsigned>(std::stoul(s));
    };
    const unsigned section=match[1].matched?number(match[1]):1;
    if(section==0) throw std::runtime_error("Invalid XPointer spine index");
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) throw std::runtime_error("Cannot open EPUB for XPointer conversion");
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>256LL*1024*1024) {close(fd);throw std::runtime_error("Invalid EPUB for XPointer conversion");}
    FILE* raw=fdopen(fd,"rb"); if(!raw){close(fd);throw std::runtime_error("Cannot read EPUB");}
    const auto close_file=[](FILE* f) { fclose(f); };
    std::unique_ptr<FILE,decltype(close_file)> file(raw,close_file); Zip zip(raw,static_cast<size_t>(st.st_size));
    auto container=xml(zip.read("META-INF/container.xml",1024*1024));
    auto* rootfile=child(child(xmlDocGetRootElement(container.get()),"rootfiles"),"rootfile");
    auto package_path=property(rootfile,"full-path");
    auto package=xml(zip.read(package_path,4*1024*1024)); auto* package_root=xmlDocGetRootElement(package.get());
    auto* spine=child(package_root,"spine"); auto* itemref=child(spine,"itemref",section);
    if(cfi_element(spine,package_root)!="/6") throw std::runtime_error("Nonstandard EPUB package spine path");
    auto* manifest=child(package_root,"manifest"); std::string href;
    for(auto* n=manifest->children;n;n=n->next) if(named(n,"item") && property(n,"id")==property(itemref,"idref")) {
        if(!href.empty() || property(n,"media-type")!="application/xhtml+xml") throw std::runtime_error("Unsupported XPointer spine resource");
        href=property(n,"href");
    }
    // Old CREngine DOM versions number only XHTML sections. Without its DOM
    // version metadata, require an XHTML-only spine so numbering is unambiguous.
    std::set<std::string> xhtml;
    for(auto* n=manifest->children;n;n=n->next) if(named(n,"item") && property(n,"media-type")=="application/xhtml+xml") xhtml.insert(property(n,"id"));
    for(auto* n=spine->children;n;n=n->next) if(named(n,"itemref") && !xhtml.count(property(n,"idref"))) throw std::runtime_error("Mixed-media spine needs KOReader DOM version information");
    auto slash=package_path.rfind('/');
    auto doc=chapter_xml(zip.read(zip_relative(slash==std::string::npos?"":package_path.substr(0,slash+1),href),16*1024*1024));
    auto* html=xmlDocGetRootElement(doc.get()); if(!named(html,"html")) throw std::runtime_error("Unsupported EPUB chapter DOM");
    auto* node=child(html,"body"); std::string rest=match[2];
    static const std::regex step(R"(^/([A-Za-z][A-Za-z0-9_-]*)(?:\[([0-9]+)\])?(.*)$)");
    static const std::regex text_step(R"(^/text\(\)(?:\[([0-9]+)\])?\.([0-9]+)$)");
    while(!rest.empty()) {
        if(std::regex_match(rest,match,text_step)) break;
        if(!std::regex_match(rest,match,step)) throw std::runtime_error("Unsupported XPointer path step");
        node=child(node,match[1],match[2].matched?number(match[2]):1); rest=match[3];
    }
    std::string cfi="epubcfi(/6/"+std::to_string(section*2)+"!"+cfi_element(node,html);
    if(!rest.empty()) {
        std::regex_match(rest,match,text_step);
        unsigned index=match[1].matched?number(match[1]):1, offset=number(match[2]);
        if(index==0) throw std::runtime_error("Invalid XPointer text index");
        const std::set<std::string> pre={"pre","code","listing","plaintext","xmp","textarea"};
        const std::set<std::string> blocks={"body","div","p","h1","h2","h3","h4","h5","h6","ul","ol","li","dl","dt","dd","blockquote","pre","section","article","aside","header","footer","nav","main","figure","figcaption","table","thead","tbody","tfoot","tr","td","th","caption","address","details","summary","form","fieldset","center"};
        bool preserve=false;
        for(auto* n=node;n && n->type==XML_ELEMENT_NODE;n=n->parent) if(pre.count(reinterpret_cast<const char*>(n->name))) preserve=true;
        unsigned elements=0; xmlNode* target=nullptr;
        for(auto* n=node->children;n;n=n->next) {
            if(n->type==XML_CDATA_SECTION_NODE) throw std::runtime_error("CDATA text requires additional XPointer validation");
            if(n->type==XML_ELEMENT_NODE) ++elements;
            if(n->type!=XML_TEXT_NODE) continue;
            const auto units=utf16(n->content);
            if(units.empty() || (!preserve && n==node->children && blocks.count(reinterpret_cast<const char*>(node->name)) && std::all_of(units.begin(),units.end(),blank))) continue;
            if(--index==0) {target=n;break;}
        }
        if(!target) throw std::runtime_error("XPointer text child is absent from this EPUB");
        const auto units=utf16(target->content); unsigned collapsed=0; size_t raw_offset=0;
        for(;raw_offset<units.size();++raw_offset) {
            if(!preserve && raw_offset>0 && blank(units[raw_offset]) && blank(units[raw_offset-1])) continue;
            if(collapsed==offset) break;
            ++collapsed;
        }
        if(collapsed!=offset) throw std::runtime_error("XPointer text offset exceeds EPUB text");
        // Comments can split one CFI text slot into multiple DOM text nodes.
        for(auto* n=target->prev;n && n->type!=XML_ELEMENT_NODE;n=n->prev)
            if(n->type==XML_TEXT_NODE || n->type==XML_CDATA_SECTION_NODE) raw_offset+=utf16(n->content).size();
        cfi+="/"+std::to_string(elements*2+1)+":"+std::to_string(raw_offset);
    }
    return cfi+")";
}

void validate_annotation_range(const std::string& path,const std::string& begin,
                               const std::string& end,const std::string& selected) {
    // Resolve both endpoints against the exact EPUB, including UTF-16 offsets.
    // Unsupported assertion forms fail closed instead of moving a highlight.
    struct Step { unsigned n; std::string id; };
    struct Point { std::vector<Step> package,content; unsigned offset=0; };
    auto parse=[](const std::string& cfi) {
        if(point_cfi(cfi).empty()) throw std::runtime_error("Unsupported annotation CFI");
        Point p; auto bang=cfi.find('!');
        auto steps=[](const std::string& s,std::vector<Step>& out,unsigned* offset) {
            size_t at=0;
            while(at<s.size() && s[at]=='/') {
                size_t start=++at; while(at<s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
                if(at-start>7) throw std::runtime_error("Annotation CFI index is too large");
                Step step{static_cast<unsigned>(std::stoul(s.substr(start,at-start))),{}};
                if(at<s.size() && s[at]=='[') {
                    ++at;
                    while(at<s.size() && s[at]!=']') { if(s[at]=='^') ++at; step.id+=s.at(at++); }
                    if(at==s.size()) throw std::runtime_error("Invalid annotation assertion");
                    ++at;
                }
                out.push_back(step);
            }
            if(offset && at<s.size() && s[at]==':') {
                auto value=s.substr(at+1);
                if(value.empty() || value.size()>7 || value.find_first_not_of("0123456789")!=std::string::npos)
                    throw std::runtime_error("Unsupported annotation text assertion");
                *offset=static_cast<unsigned>(std::stoul(value)); at=s.size();
            }
            if(at!=s.size()) throw std::runtime_error("Unsupported annotation path");
        };
        steps(cfi.substr(8,bang-8),p.package,nullptr);
        steps(cfi.substr(bang+1,cfi.size()-bang-2),p.content,&p.offset);
        if(p.package.size()!=2 || p.content.empty() || !(p.content.back().n%2))
            throw std::runtime_error("Annotation must end in a text node");
        return p;
    };
    const auto first=parse(begin), last=parse(end);
    if(first.package[0].n!=last.package[0].n || first.package[1].n!=last.package[1].n)
        throw std::runtime_error("Annotations spanning EPUB chapters are unsupported");
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0) throw std::runtime_error("Cannot read annotation EPUB");
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>256LL*1024*1024) {
        close(fd); throw std::runtime_error("Invalid annotation EPUB");
    }
    FILE* raw=fdopen(fd,"rb"); if(!raw) {close(fd);throw std::runtime_error("Cannot read annotation EPUB");}
    std::unique_ptr<FILE,int(*)(FILE*)> file(raw,fclose); Zip zip(raw,st.st_size);
    auto container=xml(zip.read("META-INF/container.xml",1024*1024));
    auto package_path=property(child(child(xmlDocGetRootElement(container.get()),"rootfiles"),"rootfile"),"full-path");
    if(!safe_path(package_path)) throw std::runtime_error("Invalid EPUB package path");
    auto package=xml(zip.read(package_path,4*1024*1024)); auto* root=xmlDocGetRootElement(package.get());
    auto element=[](xmlNode* parent,const Step& step) {
        if(step.n%2 || !step.n) throw std::runtime_error("Unsupported annotation element");
        auto index=step.n/2; xmlNode* result=nullptr;
        for(auto* n=parent->children;n;n=n->next) if(n->type==XML_ELEMENT_NODE && --index==0) {result=n;break;}
        if(!result || (!step.id.empty() && property(result,"id")!=step.id))
            throw std::runtime_error("Annotation element does not match EPUB");
        return result;
    };
    auto* spine=element(root,first.package[0]); auto* itemref=element(spine,first.package[1]);
    if(!named(spine,"spine") || !named(itemref,"itemref")) throw std::runtime_error("Invalid annotation spine");
    element(element(root,last.package[0]),last.package[1]);
    std::string href;
    for(auto* n=child(root,"manifest")->children;n;n=n->next) if(named(n,"item") && property(n,"id")==property(itemref,"idref")) {
        if(!href.empty() || property(n,"media-type")!="application/xhtml+xml") throw std::runtime_error("Unsupported annotation resource");
        href=property(n,"href");
    }
    auto slash=package_path.rfind('/');
    auto doc=chapter_xml(zip.read(zip_relative(slash==std::string::npos?"":package_path.substr(0,slash+1),href),16*1024*1024));
    auto* html=xmlDocGetRootElement(doc.get());
    if(!named(html,"html")) throw std::runtime_error("Invalid annotation document");
    std::vector<unsigned> text;
    std::map<xmlNode*,size_t> starts;
    const std::set<std::string> blocks={"p","div","section","li","h1","h2","h3","h4","h5","h6","br","blockquote"};
    std::function<void(xmlNode*)> flatten=[&](xmlNode* node) {
        for(auto* n=node;n;n=n->next) {
            bool block=n->type==XML_ELEMENT_NODE && blocks.count(reinterpret_cast<const char*>(n->name));
            if(block) text.push_back('\n');
            if(n->type==XML_TEXT_NODE || n->type==XML_CDATA_SECTION_NODE) {
                starts[n]=text.size(); auto units=utf16(n->content); text.insert(text.end(),units.begin(),units.end());
            } else if(n->type==XML_ELEMENT_NODE) flatten(n->children);
            if(block) text.push_back('\n');
        }
    };
    flatten(html);
    auto resolve=[&](const Point& p) {
        auto* parent=html;
        for(size_t i=0;i+1<p.content.size();++i) parent=element(parent,p.content[i]);
        const auto& step=p.content.back();
        if(!step.id.empty()) throw std::runtime_error("Unsupported annotation text assertion");
        unsigned slot=1,remaining=p.offset; bool found=false;
        for(auto* n=parent->children;n;n=n->next) {
            if(n->type==XML_ELEMENT_NODE) {slot+=2;continue;}
            if(slot!=step.n || (n->type!=XML_TEXT_NODE && n->type!=XML_CDATA_SECTION_NODE)) continue;
            found=true; auto units=utf16(n->content);
            if(remaining<=units.size()) {
                if(remaining && remaining<units.size() && units[remaining]>=0xdc00 && units[remaining]<=0xdfff)
                    throw std::runtime_error("Annotation splits a Unicode character");
                return starts.at(n)+remaining;
            }
            remaining-=units.size();
        }
        throw std::runtime_error(found?"Annotation text offset exceeds EPUB text":"Annotation text node is missing");
    };
    auto a=resolve(first), b=resolve(last);
    if(a>=b) throw std::runtime_error("Empty or reversed annotation range");
    auto normalized=[](const std::vector<unsigned>& input) {
        std::vector<unsigned> result;
        for(auto c:input) { if(blank(c)) {if(!result.empty() && result.back()!=32) result.push_back(32);} else result.push_back(c); }
        if(!result.empty() && result.back()==32) result.pop_back(); return result;
    };
    const auto expected=utf16(reinterpret_cast<const xmlChar*>(selected.c_str()));
    if(selected.find('\0')!=std::string::npos || normalized(std::vector<unsigned>(text.begin()+a,text.begin()+b))!=normalized(expected))
        throw std::runtime_error("Annotation text does not match its EPUB range");
}
} // namespace readest
