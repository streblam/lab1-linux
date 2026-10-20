#include "common.hpp"
#include <csignal>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <iomanip>
#include <poll.h>
#include <sys/wait.h>

static volatile sig_atomic_t interrupted=0;
static void on_signal(int) { interrupted=1; }
static std::string timestamp() {
    time_t t=time(nullptr); tm v{}; localtime_r(&t,&v); char b[64];
    strftime(b,sizeof b,"%Y-%m-%dT%H:%M:%S%z",&v); return b;
}
struct Log {
    Fd fd;
    explicit Log(const std::string& path):fd(open(path.c_str(),O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW,0600)) {
        if(fd.n<0) fail("open log");
        struct stat st{}; if(fstat(fd.n,&st)<0) fail("fstat log");
        if(!S_ISREG(st.st_mode)) throw std::runtime_error("log must be a regular file");
    }
    void put(const std::string& s) { auto line=timestamp()+" "+s+"\n"; write_all(fd.n,line); std::cout<<line<<std::flush; }
};
struct Worker {
    int id=0; pid_t pid=-1; Fd fd; bool ready=false,busy=false,shutdown=false,stopped=false;
    std::string job,start,end; Clock::time_point activity=Clock::now();
};
// Exception safety: any owned child is killed and reaped before returning to main.
struct Children {
    std::vector<Worker> list;
    ~Children() {
        for(auto& w:list) if(w.pid>0) kill(w.pid,SIGKILL);
        for(auto& w:list) if(w.pid>0) { while(waitpid(w.pid,nullptr,0)<0&&errno==EINTR) {} }
    }
};
static std::deque<std::string> scan(const Config& c) {
    DIR* raw=opendir(c.directory.c_str()); if(!raw) fail("opendir "+c.directory);
    struct Guard { DIR* p; ~Guard(){closedir(p);} } guard{raw};
    struct stat logst{}; bool have_log=stat(c.log.c_str(),&logst)==0;
    std::vector<std::string> paths;
    for(;;) {
        errno=0; dirent* e=readdir(raw); if(!e) { if(errno) fail("readdir"); break; }
        std::string name=e->d_name; if(name=="."||name=="..") continue;
        struct stat st{}; if(fstatat(dirfd(raw),name.c_str(),&st,AT_SYMLINK_NOFOLLOW)<0) fail("fstatat");
        if(!S_ISREG(st.st_mode)) continue;
        if(have_log&&st.st_dev==logst.st_dev&&st.st_ino==logst.st_ino) continue;
        std::string path=c.directory+"/"+name;
        if(path.size()>4095) throw std::runtime_error("file path too long");
        paths.push_back(path);
    }
    std::sort(paths.begin(),paths.end()); return {paths.begin(),paths.end()};
}
int supervisor_main(bool interactive,int delay_ms,int timeout_seconds) {
    interrupted=0;
    struct sigaction sa{}; sa.sa_handler=on_signal; sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT,&sa,nullptr)<0||sigaction(SIGTERM,&sa,nullptr)<0) fail("sigaction");
    Config c=read_config(); Log log(c.log); auto queue=scan(c);
    log.put("CONFIG path_hex="+hex(c.path)+" workers="+std::to_string(c.workers)+" files="+std::to_string(queue.size()));
    Children owned; owned.list.reserve(static_cast<size_t>(c.workers));
    for(int i=1;i<=c.workers;++i) {
        int s[2]; if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,s)<0) fail("socketpair");
        Fd parent(s[0]),child(s[1]);
        std::string id=std::to_string(i),endpoint=std::to_string(child.n),delay=std::to_string(delay_ms);
        pid_t pid=fork(); if(pid<0) fail("fork");
        if(pid==0) {
            // All unrelated descriptors have CLOEXEC. Only this endpoint survives exec.
            if(fcntl(child.n,F_SETFD,0)<0) _exit(126);
            execl("/proc/self/exe","oslab","--worker",endpoint.c_str(),id.c_str(),delay.c_str(),static_cast<char*>(nullptr));
            _exit(127);
        }
        Worker w; w.id=i; w.pid=pid; w.fd=std::move(parent); w.start=timestamp();
        owned.list.push_back(std::move(w));
        auto& saved=owned.list.back();
        int flags=fcntl(saved.fd.n,F_GETFL); if(flags<0||fcntl(saved.fd.n,F_SETFL,flags|O_NONBLOCK)<0) fail("fcntl nonblock");
        log.put("START worker="+id+" pid="+std::to_string(pid)+" start="+saved.start);
    }
    auto& ws=owned.list;
    bool stopping=false,stdin_open=interactive,degraded=false,idle_reported=false;
    size_t completed=0,failed=0; std::string input;
    auto deadline=Clock::now();
    auto begin_shutdown=[&]() {
        if(stopping) return;
        stopping=true; deadline=Clock::now()+std::chrono::seconds(3);
        log.put("SHUTDOWN_BEGIN pending="+std::to_string(queue.size()));
        if(!queue.empty()) degraded=true;
    };
    auto send_to=[&](Worker& w,const std::string& msg) {
        if(w.fd.n<0) return false;
        if(!send_packet(w.fd.n,msg)) {
            int e=errno; log.put("ERROR SEND worker="+std::to_string(w.id)+" "+error_text(e));
            // A full queue rejects this console command, but is not a dead peer.
            if(e!=EAGAIN&&e!=EWOULDBLOCK) { degraded=true; kill(w.pid,SIGKILL); w.fd.reset(); }
            return false;
        }
        log.put("COMMAND worker="+std::to_string(w.id)+" "+msg); return true;
    };
    auto command=[&](const std::string& line) {
        auto v=words(line); if(v.empty()) return;
        if(v.size()==1&&v[0]=="shutdown") { begin_shutdown(); return; }
        if(v.size()==1&&v[0]=="status") {
            for(auto& w:ws) log.put("STATUS worker="+std::to_string(w.id)+" pid="+std::to_string(w.pid)+" state="+(w.pid<0?"EXITED":w.busy?"BUSY":w.ready?"READY":"STARTING")+" start="+w.start+" end="+w.end);
            return;
        }
        if(v.size()==3&&v[0]=="priority"&&!stopping) {
            try {
                int id=number(v[1],1,c.workers); nice_value(v[2]); auto& w=ws[static_cast<size_t>(id-1)];
                if(w.pid<0||w.fd.n<0) throw std::runtime_error("worker is not alive");
                send_to(w,"SET_PRIORITY "+v[2]);
            } catch(const std::exception& e) { log.put("ERROR CONSOLE "+std::string(e.what())); }
            return;
        }
        log.put("ERROR CONSOLE use: status | priority ID LOW/NORMAL/HIGH | shutdown");
    };
    if(interactive) log.put("CONSOLE status | priority ID LOW/NORMAL/HIGH | shutdown; EOF requests shutdown");
    while(true) {
        if(interrupted) { degraded=true; begin_shutdown(); }
        if(stopping) for(auto& w:ws) if(w.pid>0&&!w.shutdown&&w.fd.n>=0) w.shutdown=send_to(w,"SHUTDOWN");
        std::vector<pollfd> pf;
        for(auto& w:ws) pf.push_back({w.fd.n,POLLIN,0});
        pf.push_back({stdin_open?STDIN_FILENO:-1,POLLIN,0});
        int pr=poll(pf.data(),pf.size(),100);
        if(pr<0&&errno!=EINTR) fail("poll");
        for(size_t i=0;i<ws.size();++i) {
            auto& w=ws[i];
            if(w.fd.n<0||!(pf[i].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL))) continue;
            try {
                for(;;) {
                    std::string msg; int r=receive_packet(w.fd.n,msg);
                    if(r<0) break;
                    if(r==0) { w.fd.reset(); break; }
                    w.activity=Clock::now(); log.put("RECV worker="+std::to_string(w.id)+" "+msg+(w.busy?" path_hex="+hex(w.job):""));
                    auto v=words(msg);
                    if(v.size()==3&&v[0]=="READY"&&!w.ready) {
                        if(number(v[1],1,5)!=w.id||number(v[2],1,2147483647)!=w.pid) throw std::runtime_error("READY identity mismatch");
                        w.ready=true;
                        if(!stopping) send_to(w,"SET_PRIORITY "+c.priorities[i]);
                    } else if(v.size()==4&&v[0]=="RESULT"&&w.busy) { ++completed; w.busy=false; w.job.clear(); }
                    else if(v.size()==4&&v[0]=="ERROR"&&v[1]=="PROCESS"&&w.busy) { ++failed; degraded=true; w.busy=false; w.job.clear(); }
                    else if(v.size()==4&&v[0]=="ERROR"&&v[1]=="PRIORITY") { /* permission refusal is an expected experiment */ }
                    else if(v.size()==3&&v[0]=="PRIORITY") { /* OS-confirmed nice value */ }
                    else if(v.size()==2&&v[0]=="STOPPED"&&w.shutdown) { w.stopped=true; }
                    else throw std::runtime_error("unexpected worker message");
                }
            } catch(const std::exception& e) {
                degraded=true; log.put("ERROR IPC worker="+std::to_string(w.id)+" "+e.what());
                w.fd.reset(); if(w.pid>0) kill(w.pid,SIGKILL);
            }
        }
        // Drain buffered packets before waitpid, so a last RESULT is not lost.
        for(auto& w:ws) if(w.pid>0 && w.fd.n<0) {
            int status=0; pid_t r=waitpid(w.pid,&status,WNOHANG);
            if(r<0&&errno!=EINTR) fail("waitpid");
            if(r>0) {
                bool ok=w.shutdown&&w.stopped&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
                std::string detail=WIFSIGNALED(status)?"signal="+std::to_string(WTERMSIG(status)):"exit="+std::to_string(WEXITSTATUS(status));
                w.end=timestamp(); log.put(std::string(ok?"EXIT":"UNEXPECTED_EXIT")+" worker="+std::to_string(w.id)+" pid="+std::to_string(w.pid)+" "+detail+" start="+w.start+" end="+w.end);
                if(!ok) degraded=true;
                if(w.busy) { if(!stopping) { queue.push_front(w.job); log.put("REQUEUE path_hex="+hex(w.job)); } else ++failed; }
                w.busy=false; w.job.clear(); w.pid=-1; w.fd.reset();
            }
        }
        if(stdin_open&&(pf.back().revents&(POLLIN|POLLHUP|POLLERR))) {
            char b[1024]; ssize_t n=read(STDIN_FILENO,b,sizeof b);
            if(n>0) {
                input.append(b,static_cast<size_t>(n)); size_t p;
                while((p=input.find('\n'))!=std::string::npos) { command(input.substr(0,p)); input.erase(0,p+1); }
                if(input.size()>4096) { log.put("ERROR CONSOLE line too long"); input.clear(); }
            } else if(n==0) { if(!input.empty()) command(input); stdin_open=false; begin_shutdown(); }
            else if(errno!=EINTR&&errno!=EAGAIN) { log.put("ERROR STDIN "+error_text(errno)); stdin_open=false; begin_shutdown(); }
        }
        int alive=0; bool busy=false,all_ready=true;
        for(auto& w:ws) if(w.pid>0) {
            ++alive; all_ready=all_ready&&w.ready;
            if(!stopping&&w.ready&&!w.busy&&w.fd.n>=0&&!queue.empty()) {
                const auto& path=queue.front();
                if(send_to(w,"PROCESS "+hex(path))) { w.job=path; queue.pop_front(); w.busy=true; w.activity=Clock::now(); idle_reported=false; }
            }
            busy=busy||w.busy;
            auto age=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-w.activity).count();
            if(!stopping&&(!w.ready||w.busy||w.fd.n<0)&&age>=timeout_seconds) {
                degraded=true; log.put("ERROR TIMEOUT worker="+std::to_string(w.id)); kill(w.pid,SIGKILL); w.activity=Clock::now();
            }
            if(stopping&&Clock::now()>=deadline) { log.put("ERROR SHUTDOWN_TIMEOUT worker="+std::to_string(w.id)); degraded=true; kill(w.pid,SIGKILL); }
        }
        if(!alive) { if(!queue.empty()) { degraded=true; log.put("ERROR NO_WORKERS remaining="+std::to_string(queue.size())); } break; }
        if(!stopping&&queue.empty()&&!busy&&all_ready) {
            if(!interactive) begin_shutdown();
            else if(!idle_reported) { log.put("IDLE all jobs completed; console remains active"); idle_reported=true; }
        }
    }
    log.put("SUMMARY completed="+std::to_string(completed)+" failed="+std::to_string(failed)+" pending="+std::to_string(queue.size())+" children_alive=0 exit_code="+(degraded?"2":"0"));
    return degraded?2:0;
}
