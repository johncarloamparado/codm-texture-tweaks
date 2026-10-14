#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";char b[512]={0};size_t n=fread(b,1,511,f);fclose(f);b[n]=0;return b;}
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
int main(){
  int w,h;
  CHECK(scaled_pair(1600,720,70,&w,&h) && w==1120 && h==504);
  /* aspect checked below */
  CHECK(scaled_pair(1600,720,20,&w,&h) && h>=270 && w>h);
  CHECK(scaled_pair(1600,720,99,&w,&h) && w<=1600 && h<=720 && w%2==0 && h%2==0);
  CHECK(scaled_pair(200,100,50,&w,&h) && w==200 && h==100);       /* already tiny: never grows, never below original */
  CHECK(!scaled_pair(0,720,70,&w,&h)); CHECK(!scaled_pair(1600,-1,70,&w,&h));
  CHECK(!scaled_pair(100000,720,70,&w,&h)); CHECK(!scaled_pair(1600,720,10,&w,&h)); CHECK(!scaled_pair(1600,720,100,&w,&h));
  /* 0x0 request preserved by h_sbg (no real o_sbg: stub returns via ANativeWindow_setBuffersGeometry, never called with nonzero) */
  /* safety: marker present -> scale forced off, ONLY scale.txt changes */
  load_cfg();
  wr(SCLF,"70\n"); wr(DLYF,"15\n"); wr(BLKF,"8\n"); wr(SCRASHF,"70\n");
  load_cfg();
  CHECK(g_scale.load()==0); CHECK(rd(SCLF)=="0\n"); CHECK(rd(DLYF)=="15\n"); CHECK(rd(BLKF)=="8\n");
  CHECK(access(SCRASHF,F_OK)!=0); CHECK(g_scale_safety==70);
  /* no marker -> scale kept */
  wr(SCLF,"70\n"); load_cfg(); CHECK(g_scale.load()==70); CHECK(g_scale_safety==0);
  /* marker written with active scale */
  g_scale=65; g_scale_marked=0; mark_scale_in_use(); CHECK(rd(SCRASHF)=="65\n");
  printf(fails?"T2 FAILED %d\n":"T2 ALL PASSED\n",fails); return fails!=0;
}
