/* v0.55 host test: range rule with the optional extra fields "MINSIDE MAXSIDE SHAPE" (smallest / largest texture to hide, shape). */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
int main(){
  const uint64_t H=0x1234;
  /* old 5-field line still works, extra fields default to 0 */
  wr(GRASSF,"range 0 ffffffffffffffff 37497 512 0\n"); load_cfg();
  CHECK(g_range.size()==1); CHECK(g_range[0].minside==0 && g_range[0].maxside==0 && g_range[0].shape==0);
  CHECK(grass_rule_match(37497,512,512,H)); CHECK(!grass_rule_match(37497,256,256,H));
  /* smallest texture to hide = 256 : 128 stays, 256 and 512 go */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0 256 0 0\n"); load_cfg();
  CHECK(g_range.size()==1 && g_range[0].minside==256);
  CHECK(!grass_rule_match(37497,128,128,H)); CHECK(grass_rule_match(37497,256,256,H)); CHECK(grass_rule_match(37497,512,512,H));
  /* largest = 256 : 512 stays (512 and smaller style search) */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0 0 256 0\n"); load_cfg();
  CHECK(grass_rule_match(37497,64,64,H)); CHECK(grass_rule_match(37497,256,128,H)); CHECK(!grass_rule_match(37497,512,512,H));
  /* both = 512 means exactly 512 (longest side) */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0 512 512 0\n"); load_cfg();
  CHECK(!grass_rule_match(37497,256,256,H)); CHECK(grass_rule_match(37497,512,256,H)); CHECK(!grass_rule_match(37497,1024,512,H));
  /* shape 1 = square only, 2 = not square (512xN) */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0 0 0 1\n"); load_cfg();
  CHECK(grass_rule_match(37497,512,512,H)); CHECK(!grass_rule_match(37497,512,256,H));
  wr(GRASSF,"range 0 ffffffffffffffff 0 0 0 0 0 2\n"); load_cfg();
  CHECK(!grass_rule_match(37497,512,512,H)); CHECK(grass_rule_match(37497,512,256,H));
  /* a line with 4 numbers (cannot be read) is ignored, 6 numbers works with maxside/shape 0 */
  wr(GRASSF,"range 0 ffffffffffffffff 0 0\nrange 0 ffffffffffffffff 0 0 0 128\n"); load_cfg();
  CHECK(g_range.size()==1 && g_range[0].minside==128 && g_range[0].maxside==0);
  wr(GRASSF,""); load_cfg(); CHECK(g_range.empty());
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
