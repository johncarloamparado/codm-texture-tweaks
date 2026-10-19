#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";char b[512]={0};size_t n=fread(b,1,511,f);fclose(f);b[n]=0;return b;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static void wr(const char*p,const char*t){FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
int main(){
  /* 1: fresh install -> defaults, delay 0, files created, status written */
  load_cfg(); log_start(4);
  CHECK(g_delay.load()==0); CHECK(g_scale.load()==0); CHECK(g_minsz.load()==64);
  CHECK(rd(DLYF)=="0\n"); CHECK(rd(SCLF)=="0\n"); CHECK(g_cfg_note.empty()); CHECK(g_cfg_missing>=15);
  CHECK(rd(STATF).find("stage=started")!=std::string::npos); CHECK(rd(STATF).find("ver=v0.42")!=std::string::npos);
  CHECK(rd(LOGF).find("=== start v0.42")!=std::string::npos);
  /* 2: empty / invalid files -> defaults + repaired + reported */
  wr(DLYF,""); wr(SCLF,"abc\n"); wr(MINF,"   \n"); wr(FZF,"0\n");
  g_delay=20; g_scale=70;
  load_cfg();
  CHECK(g_delay.load()==0); CHECK(g_scale.load()==0); CHECK(g_minsz.load()==64); CHECK(g_fz.load()==0);
  CHECK(g_cfg_note.find("delay=empty")!=std::string::npos); CHECK(g_cfg_note.find("scale=invalid")!=std::string::npos); CHECK(g_cfg_note.find("minsz=empty")!=std::string::npos);
  CHECK(g_cfg_note.find("fz")==std::string::npos);
  CHECK(rd(DLYF)=="0\n"); CHECK(rd(SCLF)=="0\n"); CHECK(rd(MINF)=="64\n"); CHECK(rd(FZF)=="0\n");
  /* 3: valid user settings are NOT overwritten */
  wr(DLYF,"10\n"); wr(SCLF,"70\n"); wr(BLKF,"8\n");
  load_cfg();
  CHECK(g_delay.load()==10); CHECK(g_scale.load()==70); CHECK(g_blk.load()==8); CHECK(rd(DLYF)=="10\n"); CHECK(g_cfg_note.empty());
  /* 4: unreadable (root-owned 0644-like) log -> recreated; simulate with dir-level unwritable file */
  unlink(LOGF); wr(LOGF,"old\n"); chmod(LOGF,0444);
  if (lf){fclose(lf);lf=nullptr;} if (geteuid()!=0) { L("hello\n"); CHECK(rd(LOGF).find("hello")!=std::string::npos); }
  /* 5: log rotation on version change keeps the new log */
  unlink(LOGF); wr(LOGF,"=== start v0.35 build=x ===\nold\n"); if (lf){fclose(lf);lf=nullptr;}
  log_rotate_if_new_version(); L("=== start v0.42 build=y ===\n");
  CHECK(rd(LOGF).find("v0.35")==std::string::npos); CHECK(rd(LOGF).find("v0.42")!=std::string::npos);
  /* 6: why0 */
  g_hook_state=0; CHECK(why0_reason().find("hook_not_active")!=std::string::npos);
  g_hook_state=1; g_cand=5; g_sk_delay=5; CHECK(why0_reason()=="delay(5)");
  g_applied=1; CHECK(why0_reason()=="none");
  printf(fails?"TESTS FAILED %d\n":"ALL TESTS PASSED\n",fails); return fails!=0;
}
