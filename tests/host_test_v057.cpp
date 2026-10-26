/* v0.57 host test: grass.txt = exact rules only (max 4096 unique), range rules removed, sorted exact lookup. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static void wr(const char*p,const std::string&t){unlink(p);FILE*f=fopen(p,"w");fwrite(t.data(),1,t.size(),f);fclose(f);}
static std::vector<uint8_t> cap;
static void fake_ctsi2(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLsizei n,const void*p){cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_cti2(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei n,const void*p){cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_tp(GLenum,GLenum,GLint){}
static void fake_giv(GLenum, GLint *v){ *v = 0; }
static std::vector<uint8_t> mk(int n,int seed){std::vector<uint8_t> v(n);uint32_t x=seed*2654435761u+1;for(auto&b:v){x=x*1664525u+1013904223u;b=(uint8_t)(x>>24);}return v;}
static bool allz(){for(auto v:cap) if(v) return false; return true;}
static std::string R(int fmt,int w,int h,uint64_t hh){char b[96];snprintf(b,sizeof b,"%d %d %d %016llx\n",fmt,w,h,(unsigned long long)hh);return b;}
/* n distinct exact rules: index i -> unique (fmt,w,h,hash) with the same format and size for many of them */
static std::string many(int n,int start=0){std::string s;for(int i=start;i<start+n;i++) s+=R(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(i+1));return s;}
int main(){
  g_giv_hook=fake_giv; o_ctsi2=fake_ctsi2; o_cti2=fake_cti2; o_st=fake_st; real_tp=fake_tp; g_near=1; g_testtex=0; tl_unit=0;
  CHECK(GRASS_MAX_RULES==4096);
  /* 1: parsing - comments, blanks, CRLF, tabs, 0x forms, trailing comment, junk */
  wr(GRASSF,"# header\n\n   \n37497 512 512 00000000000000aa\r\n37497\t256\t256\t0xBB\n0x9279 128 128 cc # note\n37497 512 512 dd extra\n37497 512 512 zz\n37497 512 512 11112222333344445\n37497 0 512 1\n-1 512 512 1\n037 512 512 1\n37497 512 512\nbad\n");
  load_cfg();
  CHECK(g_grass.size()==4); CHECK(g_grass_on.load()==1);
  CHECK(grass_rule_match(37497,512,512,0xaa)); CHECK(grass_rule_match(37497,256,256,0xbb)); CHECK(grass_rule_match(0x9279,128,128,0xcc));
  CHECK(!grass_rule_match(37497,512,512,0xdd));
  CHECK(g_grass_bad>=7);
  /* 2: exact identity: same hash with a different format / width / height / hash never matches */
  CHECK(!grass_rule_match(37496,512,512,0xaa)); CHECK(!grass_rule_match(37497,256,512,0xaa)); CHECK(!grass_rule_match(37497,512,256,0xaa)); CHECK(!grass_rule_match(37497,512,512,0xab));
  CHECK(!grass_rule_match(37497,512,512,0xaa00000000000000ULL));
  /* 3: duplicates collapse, different textures with the same format and size stay separate */
  wr(GRASSF,R(37497,512,512,1)+R(37497,512,512,1)+R(37497,512,512,2)+R(37497,512,256,1)+"37497 512 512 0x1\n"); load_cfg();
  CHECK(g_grass.size()==3); CHECK(g_grass_dups==2);
  /* 4: obsolete range lines are ignored and never applied, exact lines next to them still work */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0\nrange 0 ffffffffffffffff 37497 512 0 0 0 0\n"+R(37497,512,512,5)); load_cfg();
  CHECK(g_grass.size()==1); CHECK(g_grass_range_old==2); CHECK(grass_rule_match(37497,512,512,5)); CHECK(!grass_rule_match(37497,512,512,6)); CHECK(!grass_rule_match(37497,256,256,5));
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0\n"); load_cfg(); CHECK(g_grass.empty()); CHECK(g_grass_on.load()==0); CHECK(g_grass_range_old==1);   /* only a range = feature off */
  /* a range line must not blank anything in the upload path */
  {
    const int N=256*256; auto A=mk(N,11); tl_bound[0]=10; h_st(GL_TEXTURE_2D,6,37497,256,256); cap.clear(); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,A.data());
    CHECK(g_blank[10].load()==0); CHECK(cap==A);
  }
  wr(GRASSF,"37497 512\n"); load_cfg(); CHECK(g_grass.empty()); CHECK(g_grass_on.load()==0);   /* v0.44 broad rule still rejected */
  /* 5: the cap: 4095 / 4096 / 4097 / 5000 unique rules, file order decides which ones are kept */
  for (int n : {4095,4096,4097,5000}) {
    wr(GRASSF,many(n)); load_cfg();
    int want=std::min(n,4096);
    CHECK((int)g_grass.size()==want); CHECK(g_grass_over==std::max(0,n-4096));
    CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*1));                          /* first */
    CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)want));            /* last kept */
    CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*2048));                      /* a middle one */
    CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(want>4000?4000:want)));
    CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(n+1))==false);  /* never written */
    if (n>4096) CHECK(!grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*4097ULL));     /* over the cap: not used */
    for (size_t i=1;i<g_grass.size();i++) CHECK(!grass_rule_less(g_grass[i],g_grass[i-1]));   /* sorted */
  }
  /* duplicates do not use up capacity: 4096 unique + 100 repeated lines + one more unique = 4096 kept, the extra unique counted over */
  wr(GRASSF,many(4096)+many(100)+R(37497,512,512,0x123)); load_cfg();
  CHECK(g_grass.size()==4096); CHECK(g_grass_dups==100); CHECK(g_grass_over==1);
  /* shuffled input gives the same lookups */
  { std::string s; for(int i=4095;i>=0;i--) s+=R(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(i+1)); wr(GRASSF,s); load_cfg();
    CHECK(g_grass.size()==4096); for(int i=0;i<4096;i+=37) CHECK(grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(i+1))); }
  /* a very long line is skipped as a whole and the next line is still read */
  wr(GRASSF,std::string(1000,'7')+"\n"+R(37497,512,512,9)); load_cfg(); CHECK(g_grass.size()==1); CHECK(grass_rule_match(37497,512,512,9));
  wr(GRASSF,R(37497,512,512,9)+"37497 512 512 abc"); load_cfg(); CHECK(g_grass.size()==2);   /* last line without newline */
  /* 6: upload path with a large rule set: hit and miss, 61 and more rules work, guard still wins */
  const int N=256*256; auto A=mk(N,11), B=mk(N,12), C=mk(N,13);
  uint64_t hA=tex_hash(A.data(),N), hB=tex_hash(B.data(),N), hC=tex_hash(C.data(),N);
  wr(GRASSF,many(3000)+R(37497,256,256,hB)+R(37497,256,256,hA)); load_cfg(); CHECK(g_grass.size()==3002);
  tl_bound[0]=20; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,A.data()); CHECK(g_blank[20].load()==1); CHECK(allz());
  tl_bound[0]=21; h_st(GL_TEXTURE_2D,6,37497,256,256); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,B.data()); CHECK(g_blank[21].load()==1); CHECK(allz());
  tl_bound[0]=22; h_st(GL_TEXTURE_2D,6,37497,256,256); cap.clear(); h_cti2(GL_TEXTURE_2D,0,37497,256,256,0,N,C.data()); CHECK(g_blank[22].load()==2); CHECK(cap==C);
  tl_bound[0]=23; h_st(GL_TEXTURE_2D,6,37497,256,256); cap.clear(); h_cti2(GL_TEXTURE_2D,0,37496,256,256,0,N,A.data()); CHECK(g_blank[23].load()==2); CHECK(cap==A);   /* same bytes, other format */
  auto G=mk(512*256,9); uint64_t hG=tex_hash(G.data(),512*256);
  wr(GRASSF,R(37497,512,256,hG)); load_cfg(); tl_bound[0]=24; h_cti2(GL_TEXTURE_2D,0,37497,512,256,0,512*256,G.data()); CHECK(g_blank[24].load()==2); CHECK(cap==G);   /* guard */
  /* RGB-only formats are never blanked */
  wr(GRASSF,R(0x9274,256,256,tex_hash(A.data(),N/2))); load_cfg(); tl_bound[0]=25; h_cti2(GL_TEXTURE_2D,0,0x9274,256,256,0,N/2,A.data()); CHECK(g_blank[25].load()==0);
  /* an empty / missing file switches everything off */
  wr(GRASSF,""); load_cfg(); CHECK(g_grass_on.load()==0); CHECK(g_grass.empty());
  unlink(GRASSF); load_cfg(); CHECK(g_grass_on.load()==0);
  /* 7: lookup speed sanity: 819k lookups against 4096 rules stays far below a millisecond per thousand */
  wr(GRASSF,many(4096)); load_cfg();
  auto t0=std::chrono::steady_clock::now(); int hits=0; for(int i=0;i<819200;i++) hits+=grass_rule_match(37497,512,512,0x9e3779b97f4a7c15ULL*(uint64_t)(i%8192+1));
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  CHECK(hits==409600); printf("819k lookups: %.1f ms\n",ms); CHECK(ms<2000);
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
