/* v0.45 host test: Remove grass by texture fingerprint (grass.txt "fmt w h hash") and the GRASSCAND finder log. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
static GLint g_fake_pbo=0;
static void fake_giv(GLenum, GLint *v){ *v = g_fake_pbo; }
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "";std::string s;char b[4096];size_t n;while((n=fread(b,1,sizeof b,f))>0)s.append(b,n);fclose(f);return s;}
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
static GLenum cap_fmt; static GLsizei cap_n; static std::vector<uint8_t> cap;
struct Call{int l,w,h,n;bool z;}; static std::vector<Call> calls;
static void fake_ctsi2(GLenum,GLint l,GLint,GLint,GLsizei w,GLsizei h,GLenum f,GLsizei n,const void*p){cap_fmt=f;cap_n=n;cap.assign((const uint8_t*)p,(const uint8_t*)p+n);bool z=true;for(auto v:cap) if(v) z=false;calls.push_back({l,w,h,n,z});}
static void fake_cti2(GLenum,GLint,GLenum f,GLsizei,GLsizei,GLint,GLsizei n,const void*p){cap_fmt=f;cap_n=n;cap.assign((const uint8_t*)p,(const uint8_t*)p+n);}
static void fake_st(GLenum,GLsizei,GLenum,GLsizei,GLsizei){}
static void fake_tp(GLenum,GLenum,GLint){}
static std::vector<uint8_t> mk(int n,int seed){std::vector<uint8_t> v(n);uint32_t x=seed*2654435761u+1;for(auto&b:v){x=x*1664525u+1013904223u;b=(uint8_t)(x>>24);}return v;}
static bool allz(){for(auto v:cap) if(v) return false; return true;}
int main(){
  g_giv_hook=fake_giv; o_ctsi2=fake_ctsi2; o_cti2=fake_cti2; o_st=fake_st; real_tp=fake_tp; g_near=1; g_testtex=0; tl_unit=0;
  const int N=512*512; /* 8 bits per pixel: 262144 bytes */
  auto A=mk(N,1), B=mk(N,2);                       /* two different 512x512 ETC2 sRGBA textures: same format AND size */
  uint64_t hA=tex_hash(A.data(),N), hB=tex_hash(B.data(),N);
  CHECK(hA!=hB); CHECK(hA==tex_hash(A.data(),N));
  auto A2=A; A2[N/2]^=1; CHECK(tex_hash(A2.data(),N)!=hA);   /* a change in the sampled middle window is seen */
  /* grass.txt parsing: new 4-field lines kept (hex hash with or without 0x), old 2-field lines and junk ignored */
  char line[512]; snprintf(line,sizeof line,"37497 512 512 %016llx\n0x9279 256 256 0xdeadbeef\n37497 512\nbad\n37497 0 512 1\n# c\n",(unsigned long long)hA);
  wr(GRASSF,line); load_cfg();
  CHECK(g_grass.size()==2); CHECK(g_grass_on.load()==1);
  CHECK(g_grass[0].fmt==37497&&g_grass[0].w==512&&g_grass[0].h==512&&g_grass[0].hash==hA);
  CHECK(g_grass[1].hash==0xdeadbeefULL);
  wr(GRASSF,"37497 512\n"); load_cfg(); CHECK(g_grass.empty()); CHECK(g_grass_on.load()==0);   /* v0.44 file = off */
  /* the rule names texture A only */
  snprintf(line,sizeof line,"37497 512 512 %016llx\n",(unsigned long long)hA); wr(GRASSF,line); load_cfg(); CHECK(g_grass_on.load()==1);
  /* storage path: storage, then level-0 data */
  tl_bound[0]=77; h_st(GL_TEXTURE_2D,10,37497,512,512); CHECK(g_blank[77].load()==0);
  h_ctsi2(GL_TEXTURE_2D,0,0,0,512,512,37497,N,A.data());
  CHECK(g_blank[77].load()==1); CHECK(cap_n==N); CHECK(allz()); CHECK(A[0]==mk(N,1)[0]); CHECK(A==mk(N,1));
  h_ctsi2(GL_TEXTURE_2D,3,0,0,64,64,37497,4096,A.data());        /* a mip of the same texture is blanked too */
  CHECK(cap_n==4096); CHECK(allz());
  tl_bound[0]=78; h_st(GL_TEXTURE_2D,10,37497,512,512);            /* texture B, same format and size */
  h_ctsi2(GL_TEXTURE_2D,0,0,0,512,512,37497,N,B.data());
  CHECK(g_blank[78].load()==2); CHECK(!allz()); CHECK(cap==B);   /* characters / reticles with the same size stay as they are */
  h_ctsi2(GL_TEXTURE_2D,3,0,0,64,64,37497,4096,B.data()); CHECK(cap==std::vector<uint8_t>(B.begin(),B.begin()+4096));
  /* h_cti2 path (no storage call) */
  tl_bound[0]=79; h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,A.data()); CHECK(g_blank[79].load()==1); CHECK(allz());
  h_cti2(GL_TEXTURE_2D,1,37497,256,256,0,N/4,A.data()); CHECK(allz());
  tl_bound[0]=80; h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,B.data()); CHECK(g_blank[80].load()==2); CHECK(cap==B);
  /* a new storage on the same id starts over; deleting clears */
  tl_bound[0]=77; h_st(GL_TEXTURE_2D,10,37497,512,512); CHECK(g_blank[77].load()==0);
  h_ctsi2(GL_TEXTURE_2D,0,0,0,512,512,37497,N,B.data()); CHECK(g_blank[77].load()==2);
  h_ctsi2(GL_TEXTURE_2D,0,0,0,512,512,37497,N,A.data()); CHECK(g_blank[77].load()==1);
  GLuint d=77; h_del(1,&d); CHECK(g_blank[77].load()==0);
  /* v0.46: the log line carries the mip count from the storage call; textures made without it say lv=0; list cap is 4096 */
  { auto C=mk(N,7); tl_bound[0]=87; g_lv[87]=9; h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,C.data()); std::string lg=rd(LOGF); CHECK(lg.find(" z=")!=std::string::npos); CHECK(lg.find(" lv=10\n")!=std::string::npos); CHECK(lg.find(" lv=0\n")!=std::string::npos); }
  /* wrong format or size with the same bytes: not a match */
  tl_bound[0]=81; h_cti2(GL_TEXTURE_2D,0,0x9278,512,512,0,N,A.data()); CHECK(g_blank[81].load()==2);
  /* RGB-only formats are never touched or logged */
  int before=g_grass_cand.load(); tl_bound[0]=82; h_cti2(GL_TEXTURE_2D,0,0x9274,512,512,0,N/2,A.data()); CHECK(g_blank[82].load()==0); CHECK(g_grass_cand.load()==before);
  /* guard: reticle atlas / lobby buttons are never blanked even when a rule names them */
  auto G=mk(512*256,9); uint64_t hG=tex_hash(G.data(),512*256);
  snprintf(line,sizeof line,"37497 512 256 %016llx\n",(unsigned long long)hG); wr(GRASSF,line); load_cfg();
  tl_bound[0]=83; h_cti2(GL_TEXTURE_2D,0,37497,512,256,0,512*256,G.data()); CHECK(g_blank[83].load()==2); CHECK(cap==G); CHECK(g_grass_guard.load()>=1);
  /* grass off: nothing is replaced, but the finder log still fills */
  wr(GRASSF,""); load_cfg(); CHECK(g_grass_on.load()==0);
  tl_bound[0]=84; h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,A.data()); CHECK(cap==A); CHECK(g_blank[84].load()==0);
  /* GRASSCAND lines: one per distinct texture, small textures and repeats skipped, EAC picture computed */
  std::string log=rd(LOGF);
  char want[160]; snprintf(want,sizeof want,"GRASSCAND fmt=37497 w=512 h=512 n=%d h=%016llx",N,(unsigned long long)hA);
  CHECK(log.find(want)!=std::string::npos);
  size_t c1=0,pos=0; while((pos=log.find(want,pos))!=std::string::npos){c1++;pos++;} CHECK(c1==1);   /* A was uploaded many times, logged once */
  snprintf(want,sizeof want,"h=%016llx",(unsigned long long)hB); CHECK(log.find(want)!=std::string::npos);
  auto S=mk(32*32,3); tl_bound[0]=85; h_cti2(GL_TEXTURE_2D,0,37497,32,32,0,32*32,S.data()); CHECK(g_blank[85].load()==0);   /* < 64 px: ignored */
  std::vector<uint8_t> T(N,0);                              /* an EAC texture of fully transparent blocks */
  int m,z; eac_stats(0x9279,T.data(),N,&m,&z); CHECK(m==0&&z==100);
  std::vector<uint8_t> O(N,0); for(size_t i=0;i<O.size();i+=16) O[i]=255; eac_stats(0x9279,O.data(),N,&m,&z); CHECK(m==255&&z==0);
  eac_stats(0x93B0,O.data(),N,&m,&z); CHECK(m==-1&&z==-1);
  /* an upload that comes from a pixel buffer is never read or blanked (the pointer would be an offset) */
  wr(GRASSF,""); snprintf(line,sizeof line,"37497 512 512 %016llx\n",(unsigned long long)hA); wr(GRASSF,line); load_cfg();
  g_fake_pbo=5; tl_bound[0]=86; cap.clear(); h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,A.data()); CHECK(g_blank[86].load()==0); CHECK(cap==A); CHECK(g_grass_pbo.load()>=1); g_fake_pbo=0;
  /* mips that reach the GPU BEFORE level 0 (texture streaming): once level 0 is recognised, they are sent again as blank */
  { tl_bound[0]=95; h_st(GL_TEXTURE_2D,4,37497,128,128); calls.clear();
    auto M=mk(128*128,5); uint64_t hM=tex_hash(M.data(),128*128);
    snprintf(line,sizeof line,"37497 128 128 %016llx\n",(unsigned long long)hM); wr(GRASSF,line); load_cfg();
    h_ctsi2(GL_TEXTURE_2D,2,0,0,32,32,37497,1024,M.data());   /* a small mip first: passes through, state = 3 */
    CHECK(calls.size()==1 && !calls[0].z); CHECK(g_blank[95].load()==3);
    calls.clear(); h_ctsi2(GL_TEXTURE_2D,0,0,0,128,128,37497,128*128,M.data());
    CHECK(g_blank[95].load()==1);
    /* levels 1,2,3 re-sent blank with the right sizes, then level 0 itself blank */
    CHECK(calls.size()==4);
    if(calls.size()==4){ CHECK(calls[0].l==1&&calls[0].w==64&&calls[0].h==64&&calls[0].n==16*16*16&&calls[0].z);
      CHECK(calls[1].l==2&&calls[1].w==32&&calls[1].n==8*8*16&&calls[1].z); CHECK(calls[2].l==3&&calls[2].w==16&&calls[2].n==4*4*16&&calls[2].z);
      CHECK(calls[3].l==0&&calls[3].n==128*128&&calls[3].z); }
    /* a texture whose level 0 comes first needs no repair */
    tl_bound[0]=96; h_st(GL_TEXTURE_2D,4,37497,128,128); calls.clear();
    h_ctsi2(GL_TEXTURE_2D,0,0,0,128,128,37497,128*128,M.data()); CHECK(calls.size()==1 && calls[0].z); }
  wr(GRASSF,""); snprintf(line,sizeof line,"37497 512 512 %016llx\n",(unsigned long long)hA); wr(GRASSF,line); load_cfg();
  /* no thread_local destructor trouble: a fresh thread blanks too (this is what crashed v0.44.0 on the phone) */
  wr(GRASSF,""); snprintf(line,sizeof line,"37497 512 512 %016llx\n",(unsigned long long)hA); wr(GRASSF,line); load_cfg();
  std::thread th([&]{ tl_bound[0]=90; h_cti2(GL_TEXTURE_2D,0,37497,512,512,0,N,A.data()); }); th.join();
  CHECK(g_blank[90].load()==1); CHECK(allz());
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
