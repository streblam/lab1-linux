#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
inline std::string error_text(int e) { return std::to_string(e) + ":" + strerror(e); }
inline void fail(const std::string& s) { throw std::runtime_error(s + ": " + error_text(errno)); }
// Move-only ownership. Never retry close() on Linux: the descriptor may be reused.
struct Fd {
    int n = -1;
    explicit Fd(int v = -1) : n(v) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : n(o.n) { o.n = -1; }
    Fd& operator=(Fd&& o) noexcept { if (this != &o) { reset(); n=o.n; o.n=-1; } return *this; }
    void reset(int v=-1) { if(n>=0) close(n); n=v; }
};
inline void write_all(int fd, const std::string& s) {
    size_t p=0;
    while(p<s.size()) {
        ssize_t n=write(fd,s.data()+p,s.size()-p);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) fail("write");
        p+=static_cast<size_t>(n);
    }
}
inline std::string hex(const std::string& s) {
    const char* d="0123456789abcdef"; std::string r;
    for(unsigned char c:s) { r+=d[c>>4]; r+=d[c&15]; } return r;
}
inline std::string unhex(const std::string& s) {
    if(s.size()%2) throw std::runtime_error("odd hex length");
    auto nib=[](char c)->int { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; throw std::runtime_error("invalid hex"); };
    std::string r;
    for(size_t i=0;i<s.size();i+=2) r+=static_cast<char>(nib(s[i])*16+nib(s[i+1]));
    if(r.find('\0')!=std::string::npos) throw std::runtime_error("NUL in text");
    return r;
}
inline std::vector<std::string> words(const std::string& s) {
    std::istringstream in(s); std::vector<std::string> v; std::string x;
    while(in>>x) v.push_back(x);
    return v;
}
inline int number(const std::string& s,int lo,int hi) {
    size_t end=0; int n=std::stoi(s,&end);
    if(end!=s.size()||n<lo||n>hi) throw std::runtime_error("number out of range: "+s);
    return n;
}
inline int nice_value(const std::string& s) {
    if(s=="LOW") return 10;
    if(s=="NORMAL") return 0;
    if(s=="HIGH") return -5;
    throw std::runtime_error("priority must be LOW, NORMAL or HIGH");
}
constexpr size_t MAX_PACKET=16384;
inline bool send_packet(int fd,const std::string& s) {
    if(s.empty()||s.size()>MAX_PACKET) { errno=EMSGSIZE; return false; }
    ssize_t n;
    do { n=send(fd,s.data(),s.size(),MSG_NOSIGNAL); } while(n<0&&errno==EINTR);
    return n==static_cast<ssize_t>(s.size());
}
// 1 packet, 0 EOF, -1 would block; other errors throw. SOCK_SEQPACKET preserves framing.
inline int receive_packet(int fd,std::string& s) {
    char b[MAX_PACKET]; ssize_t n;
    do { n=recv(fd,b,sizeof b,MSG_TRUNC); } while(n<0&&errno==EINTR);
    if(n<0) { if(errno==EAGAIN||errno==EWOULDBLOCK) return -1; fail("recv"); }
    if(n==0) return 0;
    if(n>static_cast<ssize_t>(sizeof b)) throw std::runtime_error("oversized IPC packet");
    s.assign(b,static_cast<size_t>(n)); return 1;
}
struct Config { int workers=3; std::string directory,log,path; std::vector<std::string> priorities; };
Config read_config();
void init_config();
int worker_main(int fd,int id,int delay_ms);
int supervisor_main(bool interactive,int delay_ms,int timeout_seconds);
