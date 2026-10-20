#include "common.hpp"
#include <csignal>
int main(int argc,char** argv) {
    signal(SIGPIPE,SIG_IGN);
    try {
        if(argc==5&&std::string(argv[1])=="--worker") return worker_main(number(argv[2],0,1048576),number(argv[3],1,5),number(argv[4],0,10000));
        bool interactive=false,init=false; int delay=0,timeout=60;
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            if(a=="--init") init=true;
            else if(a=="--interactive") interactive=true;
            else if(a=="--delay-ms"&&i+1<argc) delay=number(argv[++i],0,10000);
            else if(a=="--timeout-seconds"&&i+1<argc) timeout=number(argv[++i],1,3600);
            else if(a=="--help") { std::cout<<"Usage: ./oslab --init | [--interactive] [--delay-ms 0..10000] [--timeout-seconds 1..3600]\nConfig: $HOME/.config/tsi-os-lab1/config.ini\n"; return 0; }
            else throw std::runtime_error("unknown or incomplete option: "+a);
        }
        if(init) { if(argc!=2) throw std::runtime_error("--init must be used alone"); init_config(); return 0; }
        return supervisor_main(interactive,delay,timeout);
    } catch(const std::exception& e) { std::cerr<<"FATAL "<<e.what()<<'\n'; return 1; }
}
