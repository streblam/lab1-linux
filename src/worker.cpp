#include "common.hpp"
#include <sys/resource.h>
#include <time.h>

int worker_main(int fd,int id,int delay_ms) {
    Fd channel(fd);
    auto reply=[&](const std::string& s) { if(!send_packet(fd,s)) fail("worker send"); };
    reply("READY "+std::to_string(id)+" "+std::to_string(getpid()));
    for(;;) {
        std::string msg; int rc=receive_packet(fd,msg); if(rc==0) return 0;
        auto v=words(msg);
        if(v.size()==1&&v[0]=="SHUTDOWN") { reply("STOPPED "+std::to_string(id)); return 0; }
        if(v.size()==2&&v[0]=="SET_PRIORITY") {
            int wanted=nice_value(v[1]);
            if(setpriority(PRIO_PROCESS,0,wanted)<0) { int e=errno; reply("ERROR PRIORITY "+std::to_string(e)+" "+hex(error_text(e))); }
            else { errno=0; int actual=getpriority(PRIO_PROCESS,0); if(errno) { int e=errno; reply("ERROR PRIORITY "+std::to_string(e)+" "+hex(error_text(e))); } else reply("PRIORITY "+v[1]+" "+std::to_string(actual)); }
            continue;
        }
        if(v.size()==2&&v[0]=="PROCESS") {
            std::string path=unhex(v[1]);
            // Demonstration delay only; no threads or shell commands.
            timespec t{delay_ms/1000,(delay_ms%1000)*1000000L};
            while(nanosleep(&t,&t)<0&&errno==EINTR) {}
            Fd file(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW));
            int e=0; struct stat st{};
            if(file.n<0) e=errno;
            else if(fstat(file.n,&st)<0) e=errno;
            else if(!S_ISREG(st.st_mode)) e=EINVAL;
            uint64_t bytes=0,lines=0; uint32_t hash=2166136261u; unsigned char last=0;
            char b[65536];
            while(!e) {
                ssize_t n=read(file.n,b,sizeof b); if(n<0&&errno==EINTR) continue;
                if(n<0) { e=errno; break; } if(n==0) break;
                bytes+=static_cast<uint64_t>(n);
                for(ssize_t j=0;j<n;++j) { auto x=static_cast<unsigned char>(b[j]); lines+=(x=='\n'); hash=(hash^x)*16777619u; last=x; }
            }
            if(bytes&&last!='\n') ++lines;
            if(e) reply("ERROR PROCESS "+std::to_string(e)+" "+hex(error_text(e)));
            else reply("RESULT "+std::to_string(bytes)+" "+std::to_string(lines)+" "+std::to_string(hash));
            continue;
        }
        reply("ERROR PROTOCOL 22 "+hex("unknown command"));
    }
}
