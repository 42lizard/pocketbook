#include "probe.h"
#include <cassert>
#include <iostream>
#include <sqlite3.h>
void sync_checks();

namespace {
sqlite3* position_writer=nullptr;
bool position_changed=false;
int changePositionBetweenQueries(unsigned,void*,void*,void* sql) {
    if(!position_changed && std::string(static_cast<const char*>(sql)).find("SELECT * FROM books_settings")==0) {
        assert(sqlite3_exec(position_writer,"UPDATE books_settings SET position_ts=999,cpage=99",nullptr,nullptr,nullptr)==SQLITE_OK);
        position_changed=true;
    }
    return 0;
}
int tracePositionRead(sqlite3* db,char**,const sqlite3_api_routines*) {
    return sqlite3_trace_v2(db,SQLITE_TRACE_STMT,changePositionBetweenQueries,nullptr);
}
}

int main(int argc, char** argv) {
    sync_checks();
    using readest::point_cfi;
    const std::string cfi = "epubcfi(/6/6[charlie]!/4/4/1:0)";
    assert(point_cfi(cfi) == cfi);
    assert(point_cfi("#" + cfi) == cfi);
    assert(point_cfi("pbr:/webkit?##" + cfi) == cfi);
    assert(point_cfi("epubcfi(/6/12!/4/2/3)") == "epubcfi(/6/12!/4/2/3)");
    assert(!point_cfi("epubcfi(/6/2[a^]b]!/4/2/1:8[abc,def;s=b])").empty());
    for (const auto& bad : {"", "epubcfi()", "epubcfi(/6/2)", "epubcfi(/6/0!/4/2)",
         "epubcfi(/6/2!/4/2/1:-1)", "epubcfi(/6/2!/4/2/1:)", "epubcfi(/6/2[abc!/4/2)",
         "epubcfi(/6/2!/4/2/1:0) trailing", "epubcfi(/6/2!/4,/2/1:0,/2/1:10)",
         "pbr:/word?page=4", "pbr:/webkit?##epubcfi(/6/2!/4/2)\n", "epubcfi(/6/2!/4/2[bad^x])"})
        assert(point_cfi(bad).empty());
    assert(point_cfi(std::string(9000, 'x')).empty());
    if(argc==4 && std::string(argv[1])=="native-interleaved") {
        // Register a test-only SQLite observer on the production read connection.
        // The writer commits after identity was read, before settings are queried.
        // https://sqlite.org/c3ref/auto_extension.html specifies this entry-point cast.
        assert(sqlite3_open(argv[2],&position_writer)==SQLITE_OK);
        assert(sqlite3_auto_extension(reinterpret_cast<void(*)()>(tracePositionRead))==SQLITE_OK);
        const auto before=readest::native_position(argv[2],argv[3]);
        sqlite3_reset_auto_extension();
        assert(position_changed && before.timestamp=="100" && before.progress=="[10,100]");
        const auto after=readest::native_position(argv[2],argv[3]);
        assert(after.timestamp=="999" && after.progress=="[99,100]");
        assert(sqlite3_close(position_writer)==SQLITE_OK);
    } else if(argc==4 && std::string(argv[1])=="native-read") {
        try {
            const auto position=readest::native_position(argv[2],argv[3]);
            std::cout<<position.indexed<<'\n'<<position.has_settings<<'\n'
                     <<position.timestamp<<'\n'<<position.progress<<'\n';
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if(argc==4 && std::string(argv[1])=="native-progress") {
        std::cout<<readest::native_position(argv[2],argv[3]).progress<<'\n';
    } else if(argc>=4 && std::string(argv[1])=="percentage") {
        try {
            const std::vector<std::string> paths(argv+3,argv+argc);
            const auto values=readest::native_percentages(argv[2],paths);
            for(const auto& path:paths) {
                const auto found=values.find(path);
                std::cout<<(found==values.end()?-1:found->second)<<'\n';
            }
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if(argc==4 && std::string(argv[1])=="backup") {
        try { readest::backup_native_database(argv[2],argv[3]); }
        catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if (argc == 7 && std::string(argv[1]) == "native") {
        try {
            auto expected=readest::native_position(argv[2],argv[4]);
            if(std::string(argv[5])=="stale") expected.timestamp="99";
            const auto applied=readest::apply_native_position(argv[2],argv[3],expected,argv[6],"PB743G",
                std::string(argv[5])=="firmware"?"unknown":"U743g.6.11.1683");
            if(!applied.warning.empty()) std::cerr<<applied.warning<<'\n';
            if(applied.status==readest::NativeApplyStatus::Uncertain) return 1;
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if (argc == 4 && std::string(argv[1]) == "trial") {
        try { readest::apply_readest_trial(argv[2], argv[3]); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    } else if (argc == 4) {
        auto state = readest::inspect(argv[1], argv[2], argv[3]);
        std::cout << state.report << '\n';
    } else if (argc == 5 && std::string(argv[1]) == "snapshot") {
        try { readest::snapshot_database(argv[2], argv[3]); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
        auto state = readest::inspect(argv[3], argv[4], "/mnt/ext1/Books/Readest/readest-sync-probe.epub");
        std::cout << state.report << '\n';
    }
}
