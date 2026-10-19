/* v0.40 host test: run-time settings folder, live FPS cap, crash-in-hook breaker. Build like the others (run_host_tests.sh). */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";char b[512]={0};size_t n=fread(b,1,511,f);fclose(f);b[n]=0;return b;}
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
int main(){
  std::string base = std::string(g_dir);                       /* the sed-patched default folder (exists, writable) */
  /* 1: set_dir with a clone path that exists -> that folder; one that does not exist -> falls back to the default */
  std::string clone = base + "clone_data";                      /* pretend app_data_dir; we need <app_data_dir>/files/ */
  std::string cfiles = clone + "/files";
  mkdir(clone.c_str(),0777); mkdir(cfiles.c_str(),0777);
  set_dir(clone.c_str());
  CHECK(std::string(g_dir)==cfiles+"/");
  CHECK(std::string(FPSF)==cfiles+"/fps.txt");
  set_dir((clone+"/").c_str());                                 /* trailing slash tolerated */
  CHECK(std::string(g_dir)==cfiles+"/");
  set_dir("/nonexistent/user/999/pkg");
  CHECK(std::string(g_dir)==base);                              /* default is writable -> fallback */
  set_dir(nullptr); CHECK(std::string(g_dir)==base);
  set_dir(clone.c_str());
  /* settings written in the clone folder are the ones read */
  wr(DLYF,"7\n"); load_cfg(); CHECK(g_delay.load()==7); CHECK(rd((cfiles+"/delay.txt").c_str())=="7\n");
  st_set("stage","hooking"); CHECK(read_status_key("stage")=="hooking");
  CHECK(rd((cfiles+"/codm_tex.status").c_str()).find("stage=hooking")!=std::string::npos);
  /* 2: crash breaker */
  wr(HSAFEF,"0\n"); decide_safe("hooked"); CHECK(g_safe==0);
  decide_safe("hooking"); CHECK(g_safe==1); CHECK(rd(HSAFEF)=="1\n"); CHECK(g_safe_why=="crash_in_hooking");
  decide_safe("started"); CHECK(g_safe==1); CHECK(g_safe_why=="hooksafe_file");   /* stays on until the UI resets it */
  wr(HSAFEF,"0\n"); decide_safe("nl_hooking"); CHECK(g_safe==1); CHECK(g_safe_why=="crash_in_nl_hooking");
  wr(HSAFEF,"0\n"); decide_safe(""); CHECK(g_safe==0); CHECK(read_status_key("hooksafe")=="0");
  /* 3: live FPS: first look only records, a changed file is picked up, garbage is ignored, out-of-range = off */
  wr(FPSF,"60\n"); g_fps=60;
  int64_t t=now_ns();
  live_fps_poll(t);                                              /* records mtime */
  CHECK(g_fps.load()==60);
  usleep(20000); wr(FPSF,"90\n");
  live_fps_poll(t+500000000LL);                                  /* inside the 1 s window: not looked at */
  CHECK(g_fps.load()==60);
  live_fps_poll(t+1100000000LL);
  CHECK(g_fps.load()==90); CHECK(read_status_key("live_fps")=="90");
  usleep(20000); wr(FPSF,"zzz\n"); live_fps_poll(t+2300000000LL); CHECK(g_fps.load()==90);
  usleep(20000); wr(FPSF,"5\n");   live_fps_poll(t+3500000000LL); CHECK(g_fps.load()==0);
  usleep(20000); wr(FPSF,"30\n");  live_fps_poll(t+4700000000LL); CHECK(g_fps.load()==30);
  printf(fails?"TESTS FAILED %d\n":"ALL TESTS PASSED\n",fails); return fails!=0;
}
