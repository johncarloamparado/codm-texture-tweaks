/* v0.47 host test: "range LO HI FMT SIDE MINLV" rules in grass.txt (bisecting by fingerprint). */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
static std::vector<uint8_t> cap;
static void fake_ctsi2(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLsizei n,const void*p){cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_cti2(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei n,const void*p){cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_tp(GLenum,GLenum,GLint){}
static void fake_giv(GLenum, GLint *v){ *v = 0; }
static std::vector<uint8_t> mk(int n,int seed){std::vector<uint8_t> v(n);uint32_t x=seed*2654435761u+1;for(auto&b:v){x=x*1664525u+1013904223u;b=(uint8_t)(x>>24);}return v;}
static bool allz(){for(auto v:cap) if(v) return false; return true;}
int main(){
  g_giv_hook=fake_giv; o_ctsi2=fake_ctsi2; o_cti2=fake_cti2; o_st=fake_st; real_tp=fake_tp; g_near=1; g_testtex=0; tl_unit=0;
  const int N=256*256;
  auto A=mk(N,11), B=mk(N,12), C=mk(N,13);
  uint64_t hA=tex_hash(A.data(),N), hB=tex_hash(B.data(),N), hC=tex_hash(C.data(),N);
  char line[512];
  /* parsing: exact rule + range line; bad range ignored; hi < lo ignored */
  snprintf(line,sizeof line,"37497 256 256 %016llx\nrange %016llx %016llx 37497 256 2\nrange 10 5 0 0 0\nrange zz\n",(unsigned long long)hC,(unsigned long long)hA,(unsigned long long)hA);
  wr(GRASSF,line); load_cfg();
  CHECK(g_grass.size()==1); CHECK(g_range.size()==1); CHECK(g_grass_on.load()==1);
  CHECK(g_range[0].lo==hA && g_range[0].hi==hA && g_range[0].fmt==37497 && g_range[0].side==256 && g_range[0].minlv==2);
  /* only a range line is enough to switch the feature on */
  snprintf(line,sizeof line,"range %016llx %016llx 0 0 0\n",(unsigned long long)std::min(hA,hB),(unsigned long long)std::max(hA,hB)); wr(GRASSF,line); load_cfg();
  CHECK(g_grass.empty()); CHECK(g_range.size()==1); CHECK(g_grass_on.load()==1);
  /* range over A..B (inclusive) blanks textures whose fingerprint lies inside it */
  tl_bound[0]=10; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,A.data()); CHECK(g_blank[10].load()==1); CHECK(allz());
  tl_bound[0]=11; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,B.data()); CHECK(g_blank[11].load()==1); CHECK(allz());
  bool cin = hC>=std::min(hA,hB) && hC<=std::max(hA,hB);
  tl_bound[0]=12; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,C.data()); CHECK((g_blank[12].load()==1)==cin);
  /* filters: wrong format, wrong side, too few mip levels */
  snprintf(line,sizeof line,"range 0 ffffffffffffffff 37497 512 0\n"); wr(GRASSF,line); load_cfg();
  tl_bound[0]=13; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,A.data()); CHECK(g_blank[13].load()==2);   /* side 256 != 512 */
  snprintf(line,sizeof line,"range 0 ffffffffffffffff 37496 0 0\n"); wr(GRASSF,line); load_cfg();
  tl_bound[0]=14; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,A.data()); CHECK(g_blank[14].load()==2);   /* fmt differs */
  snprintf(line,sizeof line,"range 0 ffffffffffffffff 0 0 2\n"); wr(GRASSF,line); load_cfg();
  tl_bound[0]=15; h_st(GL_TEXTURE_2D,1,37497,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,37497,N,A.data()); CHECK(g_blank[15].load()==2);   /* lv=1 menu image: kept */
  /* NOTE: h_cti2 level 0 resets lv to unknown (0), so a texture made through glCompressedTexImage2D is never excluded by minlv */
  tl_bound[0]=16; h_st(GL_TEXTURE_2D,6,37497,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,37497,N,A.data()); CHECK(g_blank[16].load()==1); CHECK(allz());   /* lv=6 passes */
  tl_bound[0]=17; h_st(GL_TEXTURE_2D,1,37497,256,256); h_ctsi2(GL_TEXTURE_2D,0,0,0,256,256,37497,N,A.data()); CHECK(g_blank[17].load()==2); CHECK(cap==A);
  /* the guard still wins over a range that covers everything */
  snprintf(line,sizeof line,"range 0 ffffffffffffffff 0 0 0\n"); wr(GRASSF,line); load_cfg();
  auto G=mk(512*256,9); tl_bound[0]=18; h_cti2(GL_TEXTURE_2D,0,37497,512,256,0,512*256,G.data()); CHECK(g_blank[18].load()==2); CHECK(cap==G);
  /* RGB-only formats are never blanked by a range */
  tl_bound[0]=19; h_cti2(GL_TEXTURE_2D,0,0x9274,256,256,0,N/2,A.data()); CHECK(g_blank[19].load()==0);
  /* an empty file switches everything off */
  wr(GRASSF,""); load_cfg(); CHECK(g_grass_on.load()==0); CHECK(g_range.empty());
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
