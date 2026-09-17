#pragma once
#include "platform.h"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

// Temporary device diagnosis: fixed stages and numeric status only, no account,
// network names, addresses, credentials or book data. Retained for device validation.
inline void networkTrace(const char* stage,int status=0) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto path=(platform::dataRoot()+"/network.log").toUtf8();
    const int fd=::open(path.constData(),O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd<0) return;
    struct stat st;
    if(fstat(fd,&st)==0 && S_ISREG(st.st_mode) && (st.st_size<65536 || ftruncate(fd,0)==0)) {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        char line[192];
        const int size=snprintf(line,sizeof(line),"[DEBUG-wifi] wall=%lld ms=%lld pid=%ld %s status=%d\n",
            static_cast<long long>(time(nullptr)),static_cast<long long>(ms),static_cast<long>(getpid()),stage,status);
        if(size>0 && static_cast<size_t>(size)<sizeof(line)) {
            const auto written=write(fd,line,size); (void)written;
        }
    }
    close(fd);
}
