/* v0.41 host test: Force OpenGL ES (forcegl.txt). Build like the others (run_host_tests.sh). */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";char b[512]={0};size_t n=fread(b,1,511,f);fclose(f);b[n]=0;return b;}
static void wr(const char*p,const char*t){unlink(p);FILE*f=fopen(p,"w");fputs(t,f);fclose(f);}
int main(){
  /* missing file -> default ON and the file is created */
  unlink(FGLF); g_forcegl=0; load_cfg();
  CHECK(g_forcegl.load()==1); CHECK(rd(FGLF)=="1\n");
  /* explicit 0 / 1 are respected; garbage keeps the default 1 */
  wr(FGLF,"0\n"); load_cfg(); CHECK(g_forcegl.load()==0);
  wr(FGLF,"1\n"); load_cfg(); CHECK(g_forcegl.load()==1);
  wr(FGLF,"abc\n"); g_forcegl=1; load_cfg(); CHECK(g_forcegl.load()==1);
  /* only libvulkan* is recognised */
  CHECK(is_vulkan_lib("libvulkan.so")); CHECK(is_vulkan_lib("/system/lib64/libvulkan.so"));
  CHECK(is_vulkan_lib("/vendor/lib64/hw/libvulkan_adreno.so")); CHECK(is_vulkan_lib("libvulkan.adreno.so"));
  CHECK(!is_vulkan_lib("libEGL.so")); CHECK(!is_vulkan_lib("libGLESv3.so")); CHECK(!is_vulkan_lib("/data/app/libunity.so")); CHECK(!is_vulkan_lib(nullptr));
  /* dlopen hooks refuse Vulkan when ON, and let it through the real call path when OFF */
  g_forcegl=1; g_vk_blocked=0;
  CHECK(h_dlopen_u("libvulkan.so", 2)==nullptr); CHECK(h_adle_u("/system/lib64/libvulkan.so", 2, nullptr)==nullptr);
  CHECK(g_vk_blocked.load()==2); CHECK(read_status_key("gfx")=="gles_forced"); CHECK(read_status_key("vk_blocked")=="2");
  void *h = h_dlopen_u("libm.so.6", 2); CHECK(h!=nullptr);                 /* non-Vulkan libraries are untouched */
  /* symbol lookups: vk* is hidden when ON, passed through when OFF; non-vk symbols are untouched */
  int dummy=0; void *p=&dummy;
  g_forcegl=1; g_vk_blocked=0;
  CHECK(wrap("vkCreateInstance", p)==nullptr); CHECK(wrap("vkGetInstanceProcAddr", p)==nullptr); CHECK(g_vk_blocked.load()==2);
  CHECK(wrap("glClear", p)==p);
  g_forcegl=0;
  CHECK(wrap("vkCreateInstance", p)==p);
  printf(fails?"FAILED %d\n":"ok\n", fails);
  return fails?1:0;
}
