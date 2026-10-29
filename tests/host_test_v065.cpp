/* v0.65: Restore finder candidate list is separate from Hide/GRASSCAND, covers compressed formats
   tex_post may change, deduplicates by fingerprint, and never reads data while a PBO is bound. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;}}while(0)
static void wr(const char *p,const char *s){FILE*f=fopen(p,"w");if(f){fputs(s,f);fclose(f);}}
static std::vector<uint8_t> mk(int n,int seed){std::vector<uint8_t> v(n);uint32_t x=(uint32_t)seed*2654435761u+1;for(auto&b:v){x=x*1664525u+1013904223u;b=(uint8_t)(x>>24);}return v;}
static int g_pbo=0;
static void fake_giv(GLenum,GLint *v){*v=g_pbo;}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_ctsi2(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLsizei,const void*){}
static std::string rd(const char *p){FILE*f=fopen(p,"r");if(!f)return "";std::string s;char b[2048];while(size_t n=fread(b,1,sizeof b,f))s.append(b,n);fclose(f);return s;}
static int count(const std::string&s,const std::string&needle){int n=0;size_t p=0;while((p=s.find(needle,p))!=std::string::npos){++n;p+=needle.size();}return n;}
int main(){
  wr(GRASSF,""); wr(ORIGF,""); load_cfg(); log_start(4);
  g_giv_hook=fake_giv; o_st=fake_st; o_ctsi2=fake_ctsi2; tl_unit=0;
  g_near=0; g_testtex=0; g_style=0; g_flat=0; g_blk=0; g_alpha=0; g_grass_on=0; g_orig_on=0; g_minsz=64; g_delay=0;
  /* ETC2 RGB 0x9274 is affected by tex_post, but has no transparent block and must not leak into GRASSCAND. */
  auto A=mk(32768,41); tl_bound[0]=40; h_st(GL_TEXTURE_2D,8,0x9274,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,0x9274,(GLsizei)A.size(),A.data());
  if(lf)fflush(lf); std::string log1=rd(LOGF);
  CHECK(log1.find("RESTORECAND fmt=37492 w=256 h=256")!=std::string::npos);
  CHECK(log1.find("GRASSCAND fmt=37492")==std::string::npos);
  /* The same texture fingerprint on another texture id is listed only once. */
  tl_bound[0]=41; h_st(GL_TEXTURE_2D,8,0x9274,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,0x9274,(GLsizei)A.size(),A.data());
  if(lf)fflush(lf); std::string log2=rd(LOGF);
  CHECK(count(log2,"RESTORECAND fmt=37492 w=256 h=256") == 1);
  /* PBO binding means data is an offset; the distinct second fingerprint must not be inspected or logged. */
  auto B=mk(32768,42); g_pbo=9; tl_bound[0]=42; h_st(GL_TEXTURE_2D,8,0x9274,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,0x9274,(GLsizei)B.size(),B.data()); g_pbo=0;
  if(lf)fflush(lf); std::string log3=rd(LOGF);
  CHECK(log3.find("TEXTURE finder cannot fingerprint an upload from a pixel buffer")!=std::string::npos);
  CHECK(count(log3,"RESTORECAND fmt=37492 w=256 h=256") == 1);
  CHECK(log3.find("GRASSCAND fmt=37492")==std::string::npos);
  if(lf){fclose(lf);lf=nullptr;}
  printf(fails?"FAILED %d\n":"ALL TESTS PASSED\n",fails); return fails?1:0;
}
