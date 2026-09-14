#include "state.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
using namespace readest;
int main(int argc,char** argv) {
    assert(argc==3);
    const std::string root=argv[1],db=root+"/scale.db";
    auto run=[&](ScanMetrics& metrics) {
        State state(db);
        return discover_device_books(state,"scale-user",{root},root+"/managed",[] { return false; },&metrics);
    };
    {
        State state(db); LibraryBook book; book.hash=std::string(32,'f'); book.title="Not on this device"; book.format="EPUB";
        LibraryPage page; page.cursor=1; page.books.push_back(book); state.apply_page("scale-user",0,page);
    }
    const auto baseline_start=std::chrono::steady_clock::now();
    for(int i=0;i<500;++i) {
        const auto number=std::to_string(i);
        inspect_epub(root+"/"+std::string(3-number.size(),'0')+number+".epub");
    }
    const auto baseline_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-baseline_start).count();
    const auto start=std::chrono::steady_clock::now(); ScanMetrics cold,warm,changed,matched;
    assert(run(cold)==0 && cold.candidates==500 && cold.fingerprints==500 && cold.validations==0);
    { State state(db); std::set<std::string> identities;
      for(const auto& item:state.scan_cache()) identities.insert(item.second.second);
      assert(identities.size()==500); }
    const auto middle=std::chrono::steady_clock::now();
    assert(run(warm)==0 && warm.cache_hits==500 && warm.fingerprints==0 && warm.validations==0);
    const auto finish=std::chrono::steady_clock::now();
    // Invalidate one path and retain the other 499 persisted candidate identities.
    const auto touched_path=root+"/000.epub";
    struct stat before; assert(stat(touched_path.c_str(),&before)==0);
    utimbuf stamp{before.st_atime,before.st_mtime+4}; assert(utime(touched_path.c_str(),&stamp)==0);
    ScanMetrics touched; assert(run(touched)==0 && touched.fingerprints==1 && touched.cache_hits==499);
    const auto moved=root+"/moved.epub";
    assert(rename(argv[2],moved.c_str())==0);
    assert(run(changed)==0 && changed.cache_hits==499 && changed.fingerprints==1);
    {
        State state(db); LibraryBook book; book.hash=epub_fingerprint(moved); book.title="Now in cloud"; book.format="EPUB";
        LibraryPage page; page.cursor=2; page.books.push_back(book); state.apply_page("scale-user",1,page);
    }
    assert(run(matched)==1 && matched.fingerprints==0 && matched.validations==1);
    assert(epub_fingerprint(moved)==inspect_epub(moved).readest_hash);
    std::cout<<"500 EPUBs (500 MiB): full-validation baseline "<<baseline_ms<<" ms; cold "<<std::chrono::duration_cast<std::chrono::milliseconds>(middle-start).count()
             <<" ms (500 bounded fingerprints, zero full validations); warm "
             <<std::chrono::duration_cast<std::chrono::milliseconds>(finish-middle).count()
             <<" ms (500 cache hits, zero EPUB reads); one new cloud match: one full validation.\n";
}
