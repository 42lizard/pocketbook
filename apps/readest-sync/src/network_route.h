#pragma once
#include <istream>
#include <sstream>
#include <string>

namespace readest {
// Linux /proc/net/route. A Wi-Fi status flag alone does not establish a route.
inline bool has_default_route(std::istream& routes) {
    std::string line;
    while(std::getline(routes,line)) {
        std::istringstream row(line);
        std::string interface,destination,gateway,mask;
        unsigned flags=0,refs=0,uses=0,metric=0;
        if(row>>interface>>destination>>gateway>>std::hex>>flags>>std::dec>>refs>>uses>>metric>>mask) {
            if(interface!="lo" && destination=="00000000" && mask=="00000000" &&
                (flags&0x1) && !(flags&0x200)) return true; // RTF_UP, not RTF_REJECT.
        }
    }
    return false;
}
}
