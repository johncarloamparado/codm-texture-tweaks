/* v0.61 host test: orig.txt = Restore original (exact rules, max 4096 unique), Hide wins on a conflict, restore_texture puts back the
   parameters this module changed, never blanks, never reads while a pixel unpack buffer is bound, revisit leaves a restored texture alone. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static void wr(const char*p,const std::string&t){unlink(p);FILE*f=fopen(p,"w");fwrite(t.data(),1,t.size(),f);fclose(f);}
static std::vector<uint8_t> cap;
static void fake_cti2(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei n,const void*p){cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_del(GLsizei,const GLuint*){}
static std::vector<std::pair<int,int>> calls;                 /* real glTexParameteri calls: (pname, value) */
static void fake_tp(GLenum,GLenum p,GLint v){calls.push_back({(int)p,(int)v});}
static int g_pbo=0;
static void fake_giv(GLenum, GLint *v){ *v = g_pbo; }
static std::vector<uint8_t> mk(int n,int seed){std::vector<uint8_t> v(n);uint32_t x=seed*2654435761u+1;for(auto&b:v){x=x*1664525u+1013904223u;b=(uint8_t)(x>>24);}return v;}
static std::string R(int fmt,int w,int h,uint64_t hh){char b[96];snprintf(b,sizeof b,"%d %d %d %016llx\n",fmt,w,h,(unsigned long long)hh);return b;}
static std::string many(int n){std::string s;for(int i=0;i<n;i++) s+=R(37808,256,256,0x9e3779b97f4a7c15ULL*(uint64_t)(i+1));return s;}
static int has(int p,int v){int n=0;for(auto&c:calls) if(c.first==p&&c.second==v) n++;return n;}
/* load_cfg() reads the look settings from the files (defaults when missing): set the ones this test needs again afterwards */
static void LOAD(){ load_cfg(); g_near=1; g_testtex=1; g_ttsize=0; g_ttsm=0; g_alpha=0; g_minsz=64; g_delay=0; g_flat=0; g_style=0; g_sedge=1; g_bias=0; g_blk=0; }
static uint64_t H(const std::vector<uint8_t>&A){return tex_hash(A.data(),(int)A.size());}
int main(){
  g_giv_hook=fake_giv; o_cti2=fake_cti2; o_st=fake_st; o_del=fake_del; real_tp=fake_tp; g_near=1; g_testtex=1; g_ttsize=0; g_ttsm=0; g_alpha=0; g_minsz=64; g_delay=0; tl_unit=0;
  CHECK(ORIG_MAX_RULES==4096);
  /* 1: parsing: comments, blanks, CRLF, 0x forms, junk, range and old lines are ignored */
  wr(GRASSF,""); wr(ORIGF,"# header\n\n37808 256 256 00000000000000aa\r\n37808\t256\t256\t0xBB\nrange 0 ffffffffffffffff 0 0 0\n37808 256\n37808 256 256 zz\nbad\n"); LOAD();
  CHECK(g_orig.size()==2); CHECK(g_orig_on.load()==1); CHECK(g_grass_on.load()==0); CHECK(g_orig_range_old==1); CHECK(g_orig_bad>=3);
  CHECK(orig_rule_match(37808,256,256,0xaa)); CHECK(orig_rule_match(37808,256,256,0xbb));
  CHECK(!orig_rule_match(37496,256,256,0xaa)); CHECK(!orig_rule_match(37808,256,128,0xaa)); CHECK(!orig_rule_match(37808,128,256,0xaa)); CHECK(!orig_rule_match(37808,256,256,0xab));
  CHECK(!grass_rule_match(37808,256,256,0xaa));                /* the two lists are separate */
  CHECK(tex_track());
  /* 2: duplicates, the 4096 cap (4095 / 4096 / 4097 / 5000), sorted */
  wr(ORIGF,R(37808,256,256,1)+R(37808,256,256,1)+R(37808,256,256,2)); LOAD(); CHECK(g_orig.size()==2); CHECK(g_orig_dups==1);
  for (int n : {4095,4096,4097,5000}) {
    wr(ORIGF,many(n)); LOAD(); int want=std::min(n,4096);
    CHECK((int)g_orig.size()==want); CHECK(g_orig_over==std::max(0,n-4096));
    CHECK(orig_rule_match(37808,256,256,0x9e3779b97f4a7c15ULL*1)); CHECK(orig_rule_match(37808,256,256,0x9e3779b97f4a7c15ULL*(uint64_t)want));
    if (n>4096) CHECK(!orig_rule_match(37808,256,256,0x9e3779b97f4a7c15ULL*4097ULL));
    for (size_t i=1;i<g_orig.size();i++) CHECK(!grass_rule_less(g_orig[i],g_orig[i-1]));
  }
  wr(ORIGF,""); LOAD(); CHECK(g_orig.empty()); CHECK(g_orig_on.load()==0);
  unlink(ORIGF); LOAD(); CHECK(g_orig.empty()); CHECK(g_orig_on.load()==0);     /* missing file = off */
  /* 3: restore: a treated texture gets back the game's own filters, base level, max level and min LOD; the data is not touched */
  const int N=256*256; auto A=mk(N,21); auto B=mk(N,22); uint64_t hA=H(A), hB=H(B);
  wr(GRASSF,""); wr(ORIGF,R(37808,256,256,hA)); LOAD(); CHECK(g_orig_on.load()==1);
  tl_bound[0]=20; calls.clear();
  h_st(GL_TEXTURE_2D,6,37808,256,256);                                   /* the module treats it (True Pixelated): NEAREST + flag */
  CHECK(g_tf[20].load()==1); CHECK(g_tr[20].load()==1);
  h_tp(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2703);                      /* the game asks for LINEAR_MIPMAP_LINEAR: recorded, overridden */
  h_tp(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,0x2600);                      /* the game asks for NEAREST */
  CHECK(g_omin[20].load()==0x2703); CHECK(g_omag[20].load()==0x2600);
  calls.clear(); cap.clear(); int n0=g_orig_n.load();
  h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,A.data());
  CHECK(cap==A); CHECK(g_blank[20].load()==4); CHECK(g_orig_n.load()==n0+1); CHECK(g_tf[20].load()==0); CHECK(g_tr[20].load()==0);
  CHECK(has(GL_TEXTURE_MIN_FILTER,0x2703)==1); CHECK(has(GL_TEXTURE_MAG_FILTER,0x2600)==1);
  CHECK(has(GL_TEXTURE_BASE_LEVEL,0)==1); CHECK(has(TEX_MAX_LEVEL,1000)==1); CHECK(has(TEX_MIN_LOD,-1000)==1);
  /* the game's later filter calls are no longer overridden */
  calls.clear(); h_tp(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2701); CHECK(has(GL_TEXTURE_MIN_FILTER,0x2701)==1);
  /* 3b: the game never set a filter: GL defaults come back */
  tl_bound[0]=21; h_st(GL_TEXTURE_2D,6,37808,256,256); CHECK(g_tr[21].load()==1);
  calls.clear(); cap.clear(); h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,A.data());
  CHECK(has(GL_TEXTURE_MIN_FILTER,0x2702)==1); CHECK(has(GL_TEXTURE_MAG_FILTER,0x2601)==1); CHECK(cap==A);
  /* 3c: a texture that is NOT in orig.txt stays treated and is not restored */
  tl_bound[0]=22; h_st(GL_TEXTURE_2D,6,37808,256,256); calls.clear(); cap.clear();
  h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,B.data());
  CHECK(g_tf[22].load()==1); CHECK(g_tr[22].load()==1); CHECK(g_blank[22].load()==2); CHECK(calls.empty()); CHECK(cap==B);
  /* 3d: in orig.txt but never changed by the module (skip rule / too small / alpha kept): nothing is written, still not blanked */
  g_testtex=0; g_near=0; g_flat=0; g_style=0; tl_bound[0]=23; h_st(GL_TEXTURE_2D,6,37808,256,256); CHECK(g_tr[23].load()==0);
  calls.clear(); cap.clear(); h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,A.data()); CHECK(calls.empty()); CHECK(cap==A); CHECK(g_blank[23].load()==4);
  g_testtex=1; g_near=1;
  /* 4: a pixel unpack buffer is bound: nothing is read, nothing is restored */
  g_pbo=7; tl_bound[0]=24; h_st(GL_TEXTURE_2D,6,37808,256,256); calls.clear(); cap.clear();
  h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,A.data()); CHECK(g_blank[24].load()==0); CHECK(calls.empty()); CHECK(g_tr[24].load()==1); g_pbo=0;
  /* 5: revisit (after the start delay) must not treat a restored texture again; an ordinary texture is still treated */
  tl_bound[0]=20; CHECK(g_blank[20].load()==4); calls.clear(); tex_post(GL_TEXTURE_2D,6,37808,256,256,true); CHECK(calls.empty()); CHECK(g_tf[20].load()==0);
  tl_bound[0]=25; g_blank[25]=2; calls.clear(); tex_post(GL_TEXTURE_2D,6,37808,256,256,true); CHECK(g_tf[25].load()==1);
  /* 6: Hide wins when a key is in both files: blanked, not restored, one conflict log */
  wr(GRASSF,R(37808,256,256,hA)); wr(ORIGF,R(37808,256,256,hA)+R(37808,256,256,hB)); LOAD(); CHECK(g_grass_on.load()==1); CHECK(g_orig_on.load()==1);
  g_orig_conflict=0; tl_bound[0]=30; h_st(GL_TEXTURE_2D,6,37808,256,256); calls.clear(); cap.clear();
  h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,A.data());
  CHECK(g_blank[30].load()==1); CHECK(cap!=A && cap.size()==A.size() && cap[0]==0xFC); CHECK(g_orig_conflict.load()==1); CHECK(g_tf[30].load()==1);
  tl_bound[0]=31; h_st(GL_TEXTURE_2D,6,37808,256,256); calls.clear(); cap.clear();
  h_cti2(GL_TEXTURE_2D,0,37808,256,256,0,N,B.data());                    /* only in orig.txt: restored, not blanked, even with Hide on for another key */
  CHECK(g_blank[31].load()==4); CHECK(cap==B); CHECK(g_tf[31].load()==0);
  /* 7: delete clears everything about the texture id */
  h_tp(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2703); GLuint d=31; g_tr[31]=1; h_del(1,&d);
  CHECK(g_omin[31].load()==0); CHECK(g_tr[31].load()==0); CHECK(g_blank[31].load()==0);
  /* 8: only the Hide list (no orig.txt): old behaviour, nothing recorded */
  unlink(ORIGF); LOAD(); CHECK(g_orig_on.load()==0); tl_bound[0]=40; h_tp(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2703); CHECK(g_omin[40].load()==0);
  printf(fails?"FAILED %d\n":"ALL TESTS PASSED\n",fails);
  return fails?1:0;
}
