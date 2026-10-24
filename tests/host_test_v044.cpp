/* v0.44 host test: Keep alpha soft (alpha.txt) and the blank blocks. Remove grass rules are tested in host_test_v045.cpp. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";char b[512]={0};size_t n=fread(b,1,511,f);fclose(f);b[n]=0;return b;}
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
static GLenum cap_fmt; static GLsizei cap_n; static std::vector<uint8_t> cap;
static void fake_ctsi2(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum f,GLsizei n,const void*p){cap_fmt=f;cap_n=n;cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_cti2(GLenum,GLint,GLenum f,GLsizei,GLsizei,GLint,GLsizei n,const void*p){cap_fmt=f;cap_n=n;cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_tp(GLenum,GLenum,GLint){}
int main(){
  /* alpha.txt: missing -> default 1 and file created; 0 respected; garbage repaired */
  unlink(ALPHAF); unlink(GRASSF); g_alpha=0; load_cfg();
  CHECK(g_alpha.load()==1); CHECK(rd(ALPHAF)=="1\n"); CHECK(g_grass_on.load()==0);
  wr(ALPHAF,"0\n"); load_cfg(); CHECK(g_alpha.load()==0);
  wr(ALPHAF,"zz\n"); load_cfg(); CHECK(g_alpha.load()==1); CHECK(rd(ALPHAF)=="1\n");
  /* blank blocks */
  uint8_t b[16]; CHECK(blank_block(0x9279,b)==16); for(int i=0;i<16;i++) CHECK(b[i]==0);
  CHECK(blank_block(0x9277,b)==8); CHECK(b[4]==0xFF&&b[5]==0xFF&&b[3]==0);
  CHECK(blank_block(0x93B0,b)==16); CHECK(b[0]==0xFC&&b[1]==0xFD&&b[7]==0xFF&&b[8]==0);
  CHECK(blank_block(0x9274,b)==0); CHECK(blank_block(0x9275,b)==0); CHECK(blank_block(0x1908,b)==0);
  CHECK(fmt_alpha(0x9279)&&fmt_alpha(0x9276)&&!fmt_alpha(0x9274)&&!fmt_alpha(0x93B0));
  o_ctsi2=fake_ctsi2; o_cti2=fake_cti2; o_st=fake_st; real_tp=fake_tp; g_near=1; tl_unit=0; g_testtex=0; g_grass_on=0; g_grass.clear(); g_blank[77]=0;
  /* Keep alpha soft: alpha-format candidate is skipped (never forced NEAREST); other formats still are */
  g_alpha=1; g_testtex=1; g_near=1; g_minsz=64; g_delay=0; g_alpha_kept=0; g_blank[77]=0;
  tl_bound[0]=90; tex_post(GL_TEXTURE_2D,5,0x9279,256,256,false);
  CHECK(g_alpha_kept.load()==1); CHECK(g_tf[90].load()==0);
  tl_bound[0]=91; tex_post(GL_TEXTURE_2D,5,0x9274,256,256,false); CHECK(g_tf[91].load()==1);
  g_alpha=0; tl_bound[0]=92; tex_post(GL_TEXTURE_2D,5,0x9279,256,256,false); CHECK(g_tf[92].load()==1);
  CHECK(why0_reason().find("alpha_soft")==std::string::npos || g_applied.load()>0);
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
