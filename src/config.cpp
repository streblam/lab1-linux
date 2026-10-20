#include "common.hpp"
#include <limits.h>

static std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
static std::string config_dir() {
    const char* h=getenv("HOME");
    if(!h||h[0]!='/') throw std::runtime_error("HOME must be an absolute directory");
    return std::string(h)+"/.config/tsi-os-lab1";
}
static void mkdir_checked(const std::string& p) {
    if(mkdir(p.c_str(),0700)<0&&errno!=EEXIST) fail("mkdir "+p);
    struct stat st{}; if(stat(p.c_str(),&st)<0) fail("stat "+p);
    if(!S_ISDIR(st.st_mode)) throw std::runtime_error(p+" is not a directory");
}
void init_config() {
    std::string dir=config_dir(); mkdir_checked(dir.substr(0,dir.rfind('/'))); mkdir_checked(dir);
    char cwd[PATH_MAX]; if(!getcwd(cwd,sizeof cwd)) fail("getcwd");
    std::string root=cwd;
    if(root.find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("INI cannot represent newline in working directory");
    std::string text="[supervisor]\nworkers=3\ndirectory="+root+"/data\nlog="+dir+"/supervisor.log\n\n[priorities]\nworker1=NORMAL\nworker2=NORMAL\nworker3=NORMAL\n";
    Fd f(open((dir+"/config.ini").c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));
    if(f.n<0) fail("create config (existing files are not overwritten)");
    try { write_all(f.n,text); if(fsync(f.n)<0) fail("fsync config"); }
    catch(...) { unlink((dir+"/config.ini").c_str()); throw; }
    std::cout<<"CONFIG "<<dir<<"/config.ini\n";
}
Config read_config() {
    Config c; c.path=config_dir()+"/config.ini";
    Fd f(open(c.path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK)); if(f.n<0) fail("open "+c.path+" (run --init first)");
    struct stat st{}; if(fstat(f.n,&st)<0) fail("fstat config");
    if(!S_ISREG(st.st_mode)) throw std::runtime_error("config must be a regular file");
    std::string text; char b[4096];
    for(;;) { ssize_t n=read(f.n,b,sizeof b); if(n<0&&errno==EINTR) continue; if(n<0) fail("read config"); if(n==0) break; text.append(b,static_cast<size_t>(n)); if(text.size()>65536) throw std::runtime_error("config too large"); }
    std::map<std::string,std::string> values; std::string section,line; std::istringstream in(text);
    while(std::getline(in,line)) {
        line=trim(line); if(line.empty()||line[0]=='#'||line[0]==';') continue;
        if(line.front()=='['&&line.back()==']') { section=line.substr(1,line.size()-2); if(section!="supervisor"&&section!="priorities") throw std::runtime_error("unknown INI section"); continue; }
        auto p=line.find('='); if(p==std::string::npos||section.empty()) throw std::runtime_error("invalid INI line");
        std::string key=section+"."+trim(line.substr(0,p));
        if(!values.emplace(key,trim(line.substr(p+1))).second) throw std::runtime_error("duplicate INI key: "+key);
    }
    auto take=[&](const std::string& key) { auto i=values.find(key); if(i==values.end()) throw std::runtime_error("missing INI key: "+key); auto v=i->second; values.erase(i); return v; };
    c.workers=number(take("supervisor.workers"),3,5);
    c.directory=take("supervisor.directory"); c.log=take("supervisor.log");
    for(const auto& p:{c.directory,c.log}) if(p.empty()||p[0]!='/'||p.find('\0')!=std::string::npos) throw std::runtime_error("INI paths must be absolute, nonempty and NUL-free");
    for(int i=1;i<=c.workers;++i) { auto p=take("priorities.worker"+std::to_string(i)); nice_value(p); c.priorities.push_back(p); }
    if(!values.empty()) throw std::runtime_error("unknown/unused INI key: "+values.begin()->first);
    return c;
}
