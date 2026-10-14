/* v0.37 host test: original-function safety net, wrap() loop guard, chain/maps diagnostics.
   Build like host_test_cfg.cpp (see README.txt). Linux host: libc is not a "system" path, so only structure is checked. */
#include "main_test_copy.cpp"
extern "C" int __android_log_print(int,const char*,const char*,...){return 0;}
extern "C" int __android_log_vprint(int,const char*,const char*,va_list){return 0;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
static pid_t fake_getpid_self() { return 0; }
static std::string rd(const char*p){FILE*f=fopen(p,"r");if(!f)return "<none>";std::string s;char b[512];size_t n;while((n=fread(b,1,511,f))>0)s.append(b,n);fclose(f);return s;}
int main(){
  /* orig_or: null slot -> resolved from the process; cached; never returns self */
  pid_t (*slot)() = nullptr;
  auto f1 = orig_or(slot, "getpid", fake_getpid_self);
  CHECK(f1 != nullptr); CHECK(slot == f1); CHECK(f1() == getpid());
  /* slot that points back at our own hook (loop) -> replaced by the real function */
  pid_t (*loop)() = fake_getpid_self;
  auto f2 = orig_or(loop, "getpid", fake_getpid_self);
  CHECK(f2 != nullptr); CHECK(f2 != fake_getpid_self); CHECK(loop == f2);
  /* healthy slot is left alone */
  pid_t (*ok)() = &getppid;
  CHECK(orig_or(ok, "getpid", fake_getpid_self) == &getppid);
  /* unknown symbol -> nullptr, no crash */
  pid_t (*none)() = nullptr;
  CHECK(orig_or(none, "definitely_not_a_symbol_xyz", fake_getpid_self) == nullptr);

  /* wrap(): returns our hook and records the original; a pointer that is already our hook is passed back untouched and not stored */
  o_swap = nullptr;
  CHECK(wrap("eglSwapBuffers", (void *)h_swap) == (void *)h_swap); CHECK(o_swap == nullptr);
  void *fake = (void *)&getpid;
  CHECK(wrap("eglSwapBuffers", fake) == (void *)h_swap); CHECK((void *)o_swap == fake);
  o_cws = nullptr; CHECK(wrap("eglCreateWindowSurface", (void *)h_cws) == (void *)h_cws); CHECK(o_cws == nullptr);
  CHECK(wrap("somethingElse", fake) == fake);

  /* h_dlsym / h_egl with null originals must not crash */
  o_dlsym = nullptr; void *r = h_dlsym(RTLD_DEFAULT, "getpid"); CHECK(r == (void *)&getpid || r != nullptr);
  o_egl = nullptr; (void)h_egl("eglSwapBuffers");           /* no libEGL on the host: returns null, must not crash */

  /* owner_of finds a mapped function; chain/maps reports run and write the status keys */
  MapOwner o = owner_of((void *)&getpid);
  CHECK(o.found); CHECK(!o.path.empty());
  CHECK(!is_system_path("/data/app/x/libfoo.so")); CHECK(is_system_path("/system/lib64/libEGL.so")); CHECK(is_system_path("/apex/com.android.runtime/lib64/bionic/libdl.so"));
  o_swap = (decltype(o_swap))&getpid; o_cws = nullptr;
  chain_report("test"); maps_report("test");
  std::string st = rd(STATF);
  CHECK(st.find("chain_foreign=") != std::string::npos); CHECK(st.find("maps_nonsys=") != std::string::npos);
  std::string lg = rd(LOGF);
  CHECK(lg.find("CHAIN test eglSwapBuffers orig=") != std::string::npos); CHECK(lg.find("CHAIN test eglCreateWindowSurface orig=null") != std::string::npos);
  CHECK(lg.find("MAPS test non_system_exec=") != std::string::npos);

  /* st_set recovers from an unwritable .tmp (only meaningful as a normal user) */
  if (geteuid() != 0) {
    unlink(STATF ".tmp"); FILE *t = fopen(STATF ".tmp", "w"); if (t) fclose(t); chmod(STATF ".tmp", 0444);
    st_set("probe", "1"); CHECK(rd(STATF).find("probe=1") != std::string::npos);
  }
  printf(fails?"TESTS FAILED %d\n":"ALL TESTS PASSED\n",fails); return fails!=0;
}
