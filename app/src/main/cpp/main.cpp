#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

static constexpr uintptr_t LS_SHARED_ADDR = 0x2025827000ULL;

enum hwbp_type { HWBP_BREAKPOINT_EMPTY=0, HWBP_BREAKPOINT_R=1, HWBP_BREAKPOINT_W=2, HWBP_BREAKPOINT_RW=3, HWBP_BREAKPOINT_X=4, HWBP_BREAKPOINT_INVALID=7 };
enum hwbp_len { HWBP_BREAKPOINT_LEN_1=1, HWBP_BREAKPOINT_LEN_2, HWBP_BREAKPOINT_LEN_3, HWBP_BREAKPOINT_LEN_4, HWBP_BREAKPOINT_LEN_5, HWBP_BREAKPOINT_LEN_6, HWBP_BREAKPOINT_LEN_7, HWBP_BREAKPOINT_LEN_8 };
enum hwbp_scope { SCOPE_MAIN_THREAD, SCOPE_OTHER_THREADS, SCOPE_ALL_THREADS };
enum sm_req_op { op_o, op_r, op_w, op_m, op_down, op_move, op_up, op_init_touch, op_brps_weps_info, op_set_process_hwbp, op_remove_process_hwbp, op_kexit };

struct hwbp_record {
    uint8_t mask[18];
    uint64_t hit_count, pc, lr, sp, orig_x0, syscallno, pstate;
    uint64_t x0,x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16,x17,x18,x19,x20,x21,x22,x23,x24,x25,x26,x27,x28,x29;
    uint32_t fpsr, fpcr;
    unsigned __int128 q0,q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,q11,q12,q13,q14,q15,q16,q17,q18,q19,q20,q21,q22,q23,q24,q25,q26,q27,q28,q29,q30,q31;
};
struct hwbp_point { hwbp_type bt; hwbp_len bl; hwbp_scope bs; uint64_t hit_addr; int record_count; hwbp_record records[0x100]; };
struct hwbp_info { uint64_t num_brps; uint64_t num_wrps; hwbp_point points[16]; };

static constexpr int MAX_MODULES=1024, MAX_SCAN_REGIONS=16534, MOD_NAME_LEN=256, MAX_SEGS_PER_MODULE=10;
struct segment_info { short index; uint8_t prot; uint64_t start; uint64_t end; };
struct module_info { char name[MOD_NAME_LEN]; int seg_count; segment_info segs[MAX_SEGS_PER_MODULE]; };
struct region_info { uint64_t start; uint64_t end; };
struct memory_info { int module_count; module_info modules[MAX_MODULES]; int region_count; region_info regions[MAX_SCAN_REGIONS]; };
struct virtual_input { int POSITION_X, POSITION_Y; int slot; int x, y; };
struct memory_rw { uint64_t rw_addr; uint8_t user_buffer[0x1000]; int size; };
struct req_obj { bool kernel; bool user; sm_req_op op; int status; int pid; memory_rw rw_info; memory_info mem_info; virtual_input vinput_info; hwbp_info bp_info; };

static req_obj* g_req=nullptr;
static volatile sig_atomic_t g_stop=0;
static void on_sig(int){ g_stop=1; }

static uint64_t parse_u64(const char* s){
    char* e=nullptr; errno=0;
    uint64_t v=strtoull(s,&e,0);
    if(errno || !e || *e){ fprintf(stderr,"数字无效: %s\n",s); exit(2); }
    return v;
}
static bool is_digits(const char* s){ if(!s||!*s) return false; for(;*s;s++) if(!isdigit((unsigned char)*s)) return false; return true; }
static hwbp_type parse_type(const std::string& s){
    if(s=="x") return HWBP_BREAKPOINT_X;
    if(s=="r") return HWBP_BREAKPOINT_R;
    if(s=="w") return HWBP_BREAKPOINT_W;
    if(s=="rw"||s=="wr") return HWBP_BREAKPOINT_RW;
    fprintf(stderr,"类型无效: %s\n",s.c_str()); exit(2);
}
static hwbp_scope parse_scope(const std::string& s){
    if(s=="main") return SCOPE_MAIN_THREAD;
    if(s=="other") return SCOPE_OTHER_THREADS;
    if(s=="all") return SCOPE_ALL_THREADS;
    fprintf(stderr,"线程范围无效: %s\n",s.c_str()); exit(2);
}
static const char* type_name(hwbp_type t){ switch(t){case HWBP_BREAKPOINT_R:return"r";case HWBP_BREAKPOINT_W:return"w";case HWBP_BREAKPOINT_RW:return"rw";case HWBP_BREAKPOINT_X:return"x";default:return"?";} }
static const char* scope_name(hwbp_scope s){ switch(s){case SCOPE_MAIN_THREAD:return"main";case SCOPE_OTHER_THREADS:return"other";case SCOPE_ALL_THREADS:return"all";default:return"?";} }

static bool wait_user(int ms){
    for(int i=0;i<ms;i++){
        if(g_req->user){ g_req->user=false; __sync_synchronize(); return true; }
        usleep(1000);
    }
    return false;
}
static bool commit_req(sm_req_op op,int ms=5000){
    g_req->op=op; __sync_synchronize();
    g_req->kernel=true; __sync_synchronize();
    return wait_user(ms);
}

static bool connect_driver(){
    if(prctl(PR_SET_NAME,"LS",0,0,0)!=0){ fprintf(stderr,"设置进程名失败: %s\n",strerror(errno)); return false; }
    void* fixed=(void*)LS_SHARED_ADDR;
    size_t sz=sizeof(req_obj);
    void* p=mmap(fixed,sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p==MAP_FAILED){ fprintf(stderr,"映射共享内存 0x%" PRIxPTR " 失败: %s\n",LS_SHARED_ADDR,strerror(errno)); return false; }
    g_req=(req_obj*)p;
    memset(g_req,0,sz);
    printf("共享内存: %p  大小: %zu\n",p,sz);
    printf("等待 lsdriver 连接线程...\n");
    if(!wait_user(10000)){
        g_req->op=op_o; g_req->kernel=true;
        if(!wait_user(10000)){
            fprintf(stderr,"连接超时：驱动未加载，或已有其它 LS 客户端占用\n");
            return false;
        }
    }
    return true;
}

static bool prompt_line(const char* text,char* buf,size_t size){
    printf("%s",text); fflush(stdout);
    if(!fgets(buf,size,stdin)) return false;
    size_t n=strlen(buf); if(n && buf[n-1]=='\n') buf[n-1]=0;
    return true;
}
static int prompt_int(const char* text,int defv){ char b[128]; if(!prompt_line(text,b,sizeof(b))||!b[0]) return defv; return (int)parse_u64(b); }
static uint64_t prompt_u64(const char* text,uint64_t defv){ char b[128]; if(!prompt_line(text,b,sizeof(b))||!b[0]) return defv; return parse_u64(b); }
static std::string prompt_str(const char* text,const char* defv){ char b[256]; if(!prompt_line(text,b,sizeof(b))||!b[0]) return defv; return std::string(b); }

static std::string read_cmdline(int pid){
    char path[64]; snprintf(path,sizeof(path),"/proc/%d/cmdline",pid);
    FILE* f=fopen(path,"rb"); if(!f) return {};
    char buf[512]; size_t n=fread(buf,1,sizeof(buf)-1,f); fclose(f);
    if(!n) return {};
    buf[n]=0;
    for(size_t i=0;i<n;i++) if(buf[i]=='\0') buf[i]=' ';
    while(n>0 && buf[n-1]==' ') buf[--n]=0;
    return std::string(buf);
}
static int find_pid_by_name(const std::string& name){
    if(name.empty()) return -1;
    if(is_digits(name.c_str())) return (int)parse_u64(name.c_str());
    DIR* d=opendir("/proc");
    if(!d){ perror("打开 /proc 失败"); return -1; }
    int found=-1;
    dirent* e;
    while((e=readdir(d))){
        if(!is_digits(e->d_name)) continue;
        int pid=atoi(e->d_name); if(pid<=0) continue;
        std::string cmd=read_cmdline(pid); if(cmd.empty()) continue;
        if(cmd==name || cmd.find(name)!=std::string::npos){
            printf("找到进程: PID=%d  CMD=%s\n",pid,cmd.c_str());
            found=pid; break;
        }
    }
    closedir(d);
    return found;
}
static int prompt_pid(){
    std::string target=prompt_str("包名/进程名/PID，留空手动输入 PID: ","");
    int pid=find_pid_by_name(target);
    if(pid>0) return pid;
    if(!target.empty()) printf("没找到进程: %s\n",target.c_str());
    return prompt_int("PID: ",-1);
}

static void usage(){
    puts("用法:");
    puts("  ls-hwbp                       进入中文交互界面");
    puts("  ls-hwbp ping                  测试驱动连接");
    puts("  ls-hwbp info                  查看 BRP/WRP 数量");
    puts("  ls-hwbp remove                删除断点/观察点");
    puts("  ls-hwbp monitor --pid PID --type x|r|w|rw --addr ADDR --len 1..8 --scope main|other|all [--interval MS]");
}

static void print_reg_pair(const char* a,uint64_t av,const char* b,uint64_t bv){
    printf("  %-3s = 0x%016" PRIx64 "    %-3s = 0x%016" PRIx64 "\n",a,av,b,bv);
}
static void print_record(int idx,const hwbp_record& r){
    printf("\n命中记录 #%03d\n",idx);
    printf("  hits = %-10" PRIu64 " pc = 0x%016" PRIx64 "  lr = 0x%016" PRIx64 "\n",r.hit_count,r.pc,r.lr);
    printf("  sp   = 0x%016" PRIx64 "  pstate = 0x%016" PRIx64 "  syscall = 0x%016" PRIx64 "\n",r.sp,r.pstate,r.syscallno);
    print_reg_pair("x0",r.x0,"x1",r.x1);
    print_reg_pair("x2",r.x2,"x3",r.x3);
    print_reg_pair("x4",r.x4,"x5",r.x5);
    print_reg_pair("x6",r.x6,"x7",r.x7);
    print_reg_pair("x8",r.x8,"x9",r.x9);
    print_reg_pair("x10",r.x10,"x11",r.x11);
    print_reg_pair("x12",r.x12,"x13",r.x13);
    print_reg_pair("x14",r.x14,"x15",r.x15);
    print_reg_pair("x16",r.x16,"x17",r.x17);
    print_reg_pair("x18",r.x18,"x19",r.x19);
    print_reg_pair("x20",r.x20,"x21",r.x21);
    print_reg_pair("x22",r.x22,"x23",r.x23);
    print_reg_pair("x24",r.x24,"x25",r.x25);
    print_reg_pair("x26",r.x26,"x27",r.x27);
    print_reg_pair("x28",r.x28,"x29",r.x29);
}

static int info_connected(){
    memset(&g_req->bp_info,0,sizeof(g_req->bp_info));
    if(!commit_req(op_brps_weps_info)){ fprintf(stderr,"读取硬件断点信息超时\n"); return 1; }
    printf("执行断点槽位 BRP: %" PRIu64 "\n",g_req->bp_info.num_brps);
    printf("访问观察点槽位 WRP: %" PRIu64 "\n",g_req->bp_info.num_wrps);
    return 0;
}
static int remove_connected(){
    if(!commit_req(op_remove_process_hwbp)){ fprintf(stderr,"删除超时\n"); return 1; }
    puts("已删除当前断点/观察点");
    return 0;
}

static void monitor_loop(hwbp_point& p,int interval,int duration_sec){
    g_stop=0; signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
    int last_count=-1; uint64_t last_hits[0x100]{}; int elapsed=0;
    while(!g_stop){
        int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100;
        bool changed=(c!=last_count);
        for(int i=0;i<c;i++) if(p.records[i].hit_count!=last_hits[i]){ changed=true; last_hits[i]=p.records[i].hit_count; }
        if(changed){
            printf("\n========== 命中记录数: %d ==========" "\n",c);
            for(int i=0;i<c;i++) print_record(i,p.records[i]);
            fflush(stdout); last_count=c;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        elapsed+=interval;
        if(duration_sec>0 && elapsed>=duration_sec*1000) break;
    }
}

static int set_monitor_connected(int pid,uint64_t addr,hwbp_type type,int len,hwbp_scope scope,int interval,int duration){
    if(pid<=0 || !addr || len<1 || len>8){ puts("输入无效"); return 1; }
    if(interval<50) interval=50;
    if(type==HWBP_BREAKPOINT_X && len!=4) fprintf(stderr,"提示: ARM64 执行断点通常长度为 4\n");
    memset(&g_req->bp_info,0,sizeof(g_req->bp_info));
    g_req->pid=pid;
    hwbp_point& p=g_req->bp_info.points[0];
    p.bt=type; p.bl=(hwbp_len)len; p.bs=scope; p.hit_addr=addr; p.record_count=0;
    if(!commit_req(op_set_process_hwbp)){ fprintf(stderr,"设置断点超时\n"); return 1; }
    if(g_req->status!=0){ fprintf(stderr,"设置失败 status=%d\n",g_req->status); return 1; }
    printf("已开始监控: PID=%d 类型=%s 地址=0x%016" PRIx64 " 长度=%d 范围=%s 刷新=%dms\n",pid,type_name(type),addr,len,scope_name(scope),interval);
    puts(duration>0 ? "正在监控..." : "按 Ctrl+C 停止监控");
    monitor_loop(p,interval,duration);
    puts("\n正在停止并删除断点...");
    commit_req(op_remove_process_hwbp,2000);
    return 0;
}

static int cmd_ping(){ if(!connect_driver()) return 1; if(!commit_req(op_o)){fprintf(stderr,"ping 超时\n");return 1;} puts("驱动响应正常"); return 0; }
static int cmd_info(){ if(!connect_driver()) return 1; return info_connected(); }
static int cmd_remove(){ if(!connect_driver()) return 1; return remove_connected(); }

struct Args { int pid=-1; uint64_t addr=0; hwbp_type type=HWBP_BREAKPOINT_EMPTY; int len=0; hwbp_scope scope=SCOPE_ALL_THREADS; int interval=1000; };

static Args parse_args(int argc,char** argv){
    Args a;
    for(int i=2;i<argc;i++){
        std::string k=argv[i];
        auto val=[&](){ if(i+1>=argc){fprintf(stderr,"%s 缺少参数\n",k.c_str());exit(2);} return argv[++i]; };
        if(k=="--pid") a.pid=(int)parse_u64(val());
        else if(k=="--addr") a.addr=parse_u64(val());
        else if(k=="--type") a.type=parse_type(val());
        else if(k=="--len") a.len=(int)parse_u64(val());
        else if(k=="--scope") a.scope=parse_scope(val());
        else if(k=="--interval") a.interval=(int)parse_u64(val());
        else { fprintf(stderr,"未知参数: %s\n",k.c_str()); exit(2); }
    }
    return a;
}
static int cmd_monitor(int argc,char** argv){
    Args a=parse_args(argc,argv);
    if(!connect_driver()) return 1;
    return set_monitor_connected(a.pid,a.addr,a.type,a.len,a.scope,a.interval,0);
}

static void menu(){
    puts("");
    puts("==============================");
    puts(" lsdriver 硬件断点工具");
    puts("==============================");
    puts("1) 测试驱动连接");
    puts("2) 查看硬件断点/观察点数量");
    puts("3) 设置并监控断点/观察点");
    puts("4) 删除当前断点/观察点");
    puts("5) 退出");
}
static int interactive_monitor(){
    int pid=prompt_pid();
    uint64_t addr=prompt_u64("监控地址，支持十六进制，例如 0x1234: ",0);
    hwbp_type type=parse_type(prompt_str("类型 [x执行/r读/w写/rw读写] 默认 x: ","x"));
    int len=prompt_int("长度 [1..8]，执行断点通常填 4，默认 4: ",4);
    hwbp_scope scope=parse_scope(prompt_str("线程范围 [main主线程/other子线程/all全部] 默认 all: ","all"));
    int interval=prompt_int("刷新间隔毫秒，默认 500: ",500);
    int duration=prompt_int("监控秒数，0 表示一直监控直到 Ctrl+C，默认 0: ",0);
    return set_monitor_connected(pid,addr,type,len,scope,interval,duration);
}
static int interactive(){
    puts("启动中文交互界面...");
    if(!connect_driver()) return 1;
    puts("已连接驱动。");
    for(;;){
        menu();
        int c=prompt_int("请选择: ",0);
        if(c==1){ if(!commit_req(op_o)) fprintf(stderr,"ping 超时\n"); else puts("驱动响应正常"); }
        else if(c==2) info_connected();
        else if(c==3) interactive_monitor();
        else if(c==4) remove_connected();
        else if(c==5) break;
        else puts("未知选项");
    }
    puts("退出");
    return 0;
}

int main(int argc,char** argv){
    if(argc<2) return interactive();
    std::string c=argv[1];
    if(c=="menu"||c=="interactive") return interactive();
    if(c=="ping") return cmd_ping();
    if(c=="info") return cmd_info();
    if(c=="remove") return cmd_remove();
    if(c=="monitor") return cmd_monitor(argc,argv);
    usage(); return 2;
}
