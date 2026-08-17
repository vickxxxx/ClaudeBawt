


#pragma once

namespace dlog {


    void Init();
    void Shutdown();


    void Write(const char* fmt, ...);
}

#define DBLOG(...) ::dlog::Write(__VA_ARGS__)
