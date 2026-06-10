#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
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
enum hwbp_reg_idx { IDX_PC=0, IDX_HIT_COUNT, IDX_LR, IDX_SP, IDX_ORIG_X0, IDX_SYSCALLNO, IDX_PSTATE, IDX_X0, IDX_X1, IDX_X2, IDX_X3, IDX_X4, IDX_X5, IDX_X6, IDX_X7, IDX_X8, IDX_X9, IDX_X10, IDX_X11, IDX_X12, IDX_X13, IDX_X14, IDX_X15, IDX_X16, IDX_X17, IDX_X18, IDX_X19, IDX_X20, IDX_X21, IDX_X22, IDX_X23, IDX_X24, IDX_X25, IDX_X26, IDX_X27, IDX_X28, IDX_X29, IDX_FPSR, IDX_FPCR, IDX_Q0, IDX_Q1, IDX_Q2, IDX_Q3, IDX_Q4, IDX_Q5, IDX_Q6, IDX_Q7, IDX_Q8, IDX_Q9, IDX_Q10, IDX_Q11, IDX_Q12, IDX_Q13, IDX_Q14, IDX_Q15, IDX_Q16, IDX_Q17, IDX_Q18, IDX_Q19, IDX_Q20, IDX_Q21, IDX_Q22, IDX_Q23, IDX_Q24, IDX_Q25, IDX_Q26, IDX_Q27, IDX_Q28, IDX_Q29, IDX_Q30, IDX_Q31, MAX_REG_COUNT };

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

static req_obj* g_req=nullptr; static volatile sig_atomic_t g_stop=0; static void sigint(int){g_stop=1;}

static uint64_t parse_u64(const char* s){ char* e=nullptr; errno=0; uint64_t v=strtoull(s,&e,0); if(errno||!e||*e){fprintf(stderr,"invalid number: %s\n",s); exit(2);} return v; }
static hwbp_type parse_type(const std::string& s){ if(s=="x")return HWBP_BREAKPOINT_X; if(s=="r")return HWBP_BREAKPOINT_R; if(s=="w")return HWBP_BREAKPOINT_W; if(s=="rw"||s=="wr")return HWBP_BREAKPOINT_RW; fprintf(stderr,"invalid type: %s\n",s.c_str()); exit(2); }
static hwbp_scope parse_scope(const std::string& s){ if(s=="main")return SCOPE_MAIN_THREAD; if(s=="other")return SCOPE_OTHER_THREADS; if(s=="all")return SCOPE_ALL_THREADS; fprintf(stderr,"invalid scope: %s\n",s.c_str()); exit(2); }
static const char* type_name(hwbp_type t){ switch(t){case HWBP_BREAKPOINT_R:return"r";case HWBP_BREAKPOINT_W:return"w";case HWBP_BREAKPOINT_RW:return"rw";case HWBP_BREAKPOINT_X:return"x";default:return"?";} }
static const char* scope_name(hwbp_scope s){ switch(s){case SCOPE_MAIN_THREAD:return"main";case SCOPE_OTHER_THREADS:return"other";case SCOPE_ALL_THREADS:return"all";default:return"?";} }

static bool wait_user(int ms){ for(int i=0;i<ms;i++){ if(g_req->user){ g_req->user=false; __sync_synchronize(); return true; } usleep(1000); } return false; }
static bool commit(sm_req_op op,int ms=5000){ g_req->op=op; __sync_synchronize(); g_req->kernel=true; __sync_synchronize(); return wait_user(ms); }

static bool connect_driver(){
    if(prctl(PR_SET_NAME,"LS",0,0,0)!=0){ fprintf(stderr,"prctl failed: %s\n",strerror(errno)); return false; }
    void* fixed=(void*)LS_SHARED_ADDR; size_t sz=sizeof(req_obj);
    void* p=mmap(fixed,sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p==MAP_FAILED){ fprintf(stderr,"mmap 0x%" PRIxPTR " failed: %s\n",LS_SHARED_ADDR,strerror(errno)); return false; }
    g_req=(req_obj*)p; memset(g_req,0,sz);
    printf("shared memory %p size %zu\n",p,sz);
    printf("waiting lsdriver connector...\n");
    if(!wait_user(10000)){ g_req->op=op_o; g_req->kernel=true; if(!wait_user(10000)){ fprintf(stderr,"connect timeout. loaded? another LS client connected?\n"); return false; }}
    return true;
}

static void usage(){
    puts("Usage:");
    puts("  ls-hwbp ping");
    puts("  ls-hwbp info");
    puts("  ls-hwbp remove");
    puts("  ls-hwbp monitor --pid PID --type x|r|w|rw --addr ADDR --len 1..8 --scope main|other|all [--interval MS]");
}

static int cmd_ping(){ if(!connect_driver())return 1; if(!commit(op_o)){fprintf(stderr,"ping timeout\n");return 1;} puts("pong"); return 0; }
static int cmd_info(){ if(!connect_driver())return 1; memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); if(!commit(op_brps_weps_info)){fprintf(stderr,"info timeout\n");return 1;} printf("BRP execute slots: %" PRIu64 "\n",g_req->bp_info.num_brps); printf("WRP watch slots:   %" PRIu64 "\n",g_req->bp_info.num_wrps); return 0; }
static int cmd_remove(){ if(!connect_driver())return 1; if(!commit(op_remove_process_hwbp)){fprintf(stderr,"remove timeout\n");return 1;} puts("removed"); return 0; }

struct Args{ int pid=-1; uint64_t addr=0; hwbp_type type=HWBP_BREAKPOINT_EMPTY; int len=0; hwbp_scope scope=SCOPE_ALL_THREADS; int interval=1000; };
static Args parse_args(int argc,char**argv){ Args a; for(int i=2;i<argc;i++){ std::string k=argv[i]; auto val=[&](){ if(i+1>=argc){fprintf(stderr,"%s needs value\n",k.c_str());exit(2);} return argv[++i];}; if(k=="--pid")a.pid=(int)parse_u64(val()); else if(k=="--addr")a.addr=parse_u64(val()); else if(k=="--type")a.type=parse_type(val()); else if(k=="--len")a.len=(int)parse_u64(val()); else if(k=="--scope")a.scope=parse_scope(val()); else if(k=="--interval")a.interval=(int)parse_u64(val()); else {fprintf(stderr,"unknown arg: %s\n",k.c_str());exit(2);} } if(a.pid<=0||!a.addr||a.type==HWBP_BREAKPOINT_EMPTY||a.len<1||a.len>8){usage();exit(2);} if(a.interval<50)a.interval=50; return a; }

static void print_record(int i,const hwbp_record&r){
    printf("#%03d hits=%" PRIu64 " pc=0x%016" PRIx64 " lr=0x%016" PRIx64 " sp=0x%016" PRIx64 " pstate=0x%016" PRIx64 "\n",i,r.hit_count,r.pc,r.lr,r.sp,r.pstate);
    printf("     x0=0x%016" PRIx64 " x1=0x%016" PRIx64 " x2=0x%016" PRIx64 " x3=0x%016" PRIx64 "\n",r.x0,r.x1,r.x2,r.x3);
    printf("     x4=0x%016" PRIx64 " x5=0x%016" PRIx64 " x6=0x%016" PRIx64 " x7=0x%016" PRIx64 "\n",r.x4,r.x5,r.x6,r.x7);
}

static int cmd_monitor(int argc,char**argv){
    Args a=parse_args(argc,argv); if(a.type==HWBP_BREAKPOINT_X&&a.len!=4) fprintf(stderr,"warning: AArch64 execute breakpoint normally uses len 4\n");
    if(!connect_driver())return 1; memset(&g_req->bp_info,0,sizeof(g_req->bp_info)); g_req->pid=a.pid;
    hwbp_point& p=g_req->bp_info.points[0]; p.bt=a.type; p.bl=(hwbp_len)a.len; p.bs=a.scope; p.hit_addr=a.addr; p.record_count=0;
    if(!commit(op_set_process_hwbp)){fprintf(stderr,"set timeout\n");return 1;} if(g_req->status!=0){fprintf(stderr,"set failed status=%d\n",g_req->status);return 1;}
    printf("monitor pid=%d type=%s addr=0x%016" PRIx64 " len=%d scope=%s interval=%dms\n",a.pid,type_name(a.type),a.addr,a.len,scope_name(a.scope),a.interval); puts("Ctrl+C to stop");
    signal(SIGINT,sigint); signal(SIGTERM,sigint); int last_count=-1; uint64_t last_hits[0x100]{};
    while(!g_stop){ int c=p.record_count; if(c<0)c=0; if(c>0x100)c=0x100; bool changed=(c!=last_count); for(int i=0;i<c;i++){ if(p.records[i].hit_count!=last_hits[i]){changed=true; last_hits[i]=p.records[i].hit_count;}} if(changed){ printf("\nrecords=%d\n",c); for(int i=0;i<c;i++)print_record(i,p.records[i]); fflush(stdout); last_count=c;} std::this_thread::sleep_for(std::chrono::milliseconds(a.interval)); }
    puts("\nstopping..."); commit(op_remove_process_hwbp,2000); return 0;
}

int main(int argc,char**argv){ if(argc<2){usage();return 2;} std::string c=argv[1]; if(c=="ping")return cmd_ping(); if(c=="info")return cmd_info(); if(c=="remove")return cmd_remove(); if(c=="monitor")return cmd_monitor(argc,argv); usage(); return 2; }
