#include "probe.h"
#include <cassert>
#include <iostream>
void sync_checks();

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
    if(argc==4 && std::string(argv[1])=="native-progress") {
        std::cout<<readest::native_position(argv[2],argv[3]).progress<<'\n';
    } else if(argc==4 && std::string(argv[1])=="percentage") {
        try {
            const auto values=readest::native_percentages(argv[2],{argv[3]});
            const auto found=values.find(argv[3]);
            std::cout<<(found==values.end()?-1:found->second)<<'\n';
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if(argc==4 && std::string(argv[1])=="backup") {
        try { readest::backup_native_database(argv[2],argv[3]); }
        catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    } else if (argc == 7 && std::string(argv[1]) == "native") {
        try {
            auto expected=readest::native_position(argv[2],argv[4]);
            if(std::string(argv[5])=="stale") expected.timestamp="99";
            readest::apply_native_position(argv[2],argv[3],expected,argv[6],"PB743G",
                std::string(argv[5])=="firmware"?"unknown":"U743g.6.11.1683");
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
