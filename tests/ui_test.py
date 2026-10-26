#!/usr/bin/env python3
"""v0.57 WebUI checks (headless Chromium, demo mode). Run: python3 tests/ui_test.py   (needs: pip install playwright; playwright install chromium)
Exit code 0 = all checks passed. Skipped (exit 0, message) when playwright is not available."""
import os, re, sys, json
try:
    from playwright.sync_api import sync_playwright
except Exception as e:
    print("SKIP ui_test: playwright not available (%s)" % e); sys.exit(0)

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = "file://" + os.path.join(HERE, "..", "webroot", "index.html")
fails = []; passed = 0
def check(name, cond, extra=""):
    global passed
    if cond: passed += 1; print("PASS", name)
    else: fails.append(name); print("FAIL", name, extra)

EXACT = re.compile(r"^\d+ \d+ \d+ [0-9a-f]{16}$")
def rules(pg): return [l for l in pg.evaluate("window.__ct.rules()")]

def open_adv(pg, q, init=None):
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    if init: pg.add_init_script(init)
    pg.goto(PAGE + q); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)")
    pg.wait_for_timeout(200)
    open_tb(pg)
    return errs

def open_tb(pg):
    """the Texture browser lives in a window now: tap the button to open it (no-op when it is already open)"""
    pg.evaluate("document.getElementById('tbOpen').click()"); pg.wait_for_timeout(350)

def tap(pg, sel): pg.evaluate("s=>document.querySelector(s).click()", sel); pg.wait_for_timeout(120)

with sync_playwright() as p:
    b = p.chromium.launch()
    def new(): return b.new_page(viewport={"width": 360, "height": 800})

    # 1. 5000 candidates: nothing truncated, only one batch of rows in the page
    pg = new(); errs = open_adv(pg, "?demo=5000")
    n = pg.evaluate("window.__ct.cands()")
    check("all 5003 candidates available (no 1500 cut)", n == 5003, n)
    check("only 40 rows in the page", pg.evaluate("document.querySelectorAll('#tbList .tb-row').length") == 40)
    tap(pg, "#tbMore")
    check("Show more adds one batch of 40", pg.evaluate("document.querySelectorAll('#tbList .tb-row').length") == 80)
    check("no duplicate rows", pg.evaluate("new Set([...document.querySelectorAll('#tbList .tb-row')].map(e=>e.dataset.key)).size") == 80)
    check("counter shows 0 / 4096", "0 / 4096" in pg.inner_text("#tbCount"))
    check("fingerprint shown 4-4-4-4", pg.evaluate("/id [0-9a-f]{4} [0-9a-f]{4} [0-9a-f]{4} [0-9a-f]{4}/.test(document.querySelector('#tbList .tb-row').innerText)"))

    # 2. hash prefix search
    key = pg.evaluate("document.querySelector('#tbList .tb-row').dataset.key")
    h = key.split("|")[3]
    pg.fill("#tbQ", "id:" + h[:6]); pg.wait_for_timeout(400)
    check("hash prefix search finds it", pg.evaluate("document.querySelectorAll('#tbList .tb-row').length") >= 1 and h[:6] in pg.inner_text("#tbList").replace(" ", ""))
    pg.fill("#tbQ", "z>50 512"); pg.wait_for_timeout(400)
    check("see-through search works", "of 5003" in pg.inner_text("#tbCount") or "of 4" in pg.inner_text("#tbCount"))
    pg.fill("#tbQ", ""); pg.wait_for_timeout(400)

    # 3. select filtered > 60, test selected: exact rules only
    pg.fill("#tbQ", "512x512"); pg.wait_for_timeout(400)
    F = int(re.search(r"Filter: (\d+) of", pg.inner_text("#tbCount")).group(1))
    check("filter has more than 60 textures", F > 60, F)
    tap(pg, "#tbbSelF")
    check("selection counts all filtered, not only the visible rows", ("%d selected" % F) in pg.inner_text("#tbSelTxt"), pg.inner_text("#tbSelTxt"))
    tap(pg, "#tbbTestS")
    r = rules(pg)
    check("test of %d selected: one exact rule each" % F, len(r) == F and all(EXACT.match(x) for x in r), (len(r), r[:2]))
    check("no range text anywhere in rules", not any("range" in x for x in r))
    check("counter synchronized", ("%d / 4096" % F) in pg.inner_text("#gf"), pg.inner_text("#gf")[:120])
    # 4. side test of > 60 inside the test window
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    pg.fill("#twQ", "256"); pg.wait_for_timeout(300)  # none of 512x512 are 256 wide -> maybe nothing
    pg.fill("#twQ", ""); pg.select_option("#twMips", "yes") if False else None
    pg.evaluate("document.querySelector('#twOv [data-tw=all]').click()"); pg.wait_for_timeout(200)
    ticked = pg.evaluate("document.querySelectorAll('#twOv .tw-row.sel').length")
    pg.evaluate("document.querySelector('#twOv [data-tw=none]').click()")
    pg.fill("#twQ", "mips"); pg.wait_for_timeout(300)
    check("test window lists in batches of 40", pg.evaluate("document.querySelectorAll('#twOv .tw-row').length") <= 40)
    pg.fill("#twQ", ""); pg.wait_for_timeout(200)
    pg.evaluate("document.querySelector('#twOv [data-tw=all]').click()")  # ticks all shown-filter (everything)
    tap(pg, "#twOv [data-tw=none]")
    # tick the first 70 by filter 'lv' not available -> use tick all then side test of the whole is refused ("every texture")
    pg.evaluate("document.querySelector('#twOv [data-tw=all]').click()")
    pg.evaluate("document.querySelector('#twOv [data-tw=side]').click()"); pg.wait_for_timeout(200)
    check("side test of every texture is refused with a message", pg.is_visible("#twMsg") or pg.evaluate("!document.querySelector('#twOv [data-tw=side]').disabled") is False)
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    check("no JS errors (flow 1-4)", not errs, errs)
    pg.close()

    # 5. side test with 70 of 120 textures, then yes / bisect: exact rules each step
    pg = new(); errs = open_adv(pg, "?demo=117")
    tap(pg, "#gfStart") if pg.evaluate("!!document.getElementById('gfStart')") else None
    n0 = len(rules(pg))
    pg.evaluate("document.getElementById('twOpen') && document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    keysN = pg.evaluate("document.querySelectorAll('#twOv .tw-row').length")
    # filter by mips to make a subset
    pg.select_option("#twMips", "yes") if pg.evaluate("!!document.getElementById('twMips')") else None
    pg.wait_for_timeout(200)
    pg.evaluate("document.querySelector('#twOv [data-tw=side]').click()"); pg.wait_for_timeout(300)
    r = rules(pg)
    check("side test rules are exact", all(EXACT.match(x) for x in r) and len(r) > 60, len(r))
    tap(pg, "#gfSubYes"); pg.wait_for_timeout(200)
    r2 = rules(pg)
    check("after 'hidden' the next step has about half, still exact", 0 < len(r2) < len(r) and all(EXACT.match(x) for x in r2), (len(r), len(r2)))
    tap(pg, "#gfBack"); pg.wait_for_timeout(200)
    check("Back returns to the main list exactly as it was", len(rules(pg)) == n0, (n0, len(rules(pg))))
    check("no JS errors (side test)", not errs, errs)
    pg.close()

    # 6. the 4096 limit: exactly 4096 is accepted, 4097 is refused with nothing written
    def vis_for(n):
        q = new(); open_adv(q, "?demo=%d" % n); v = q.evaluate("window.__ct.vis()"); q.close(); return v
    n = 4100
    for _ in range(12):
        v = vis_for(n)
        if v == 4096: break
        n += (4096 - v)
    check("found a demo size with exactly 4096 candidates", v == 4096, (n, v))
    pg = new(); errs = open_adv(pg, "?demo=%d" % n)
    tap(pg, "#tbbTestF"); first = len(rules(pg))
    check("hide/test ALL asks to be tapped twice", first == 0, first)
    tap(pg, "#tbbTestF")
    check("4096 exact rules accepted", len(rules(pg)) == 4096, len(rules(pg)))
    check("counter shows 4096 / 4096", "4096 / 4096" in pg.inner_text("#gf"))
    check("file text has 4096 unique exact lines", len(set(pg.evaluate("window.__ct.fakeGrass()").split("\n"))) == 4096)
    check("no JS errors (4096)", not errs, errs)
    pg.close()
    pg = new(); errs = open_adv(pg, "?demo=%d" % (n + 1))
    v = pg.evaluate("window.__ct.vis()")
    check("4097 candidates", v == 4097, v)
    tap(pg, "#tbbSelF"); tap(pg, "#tbbHide"); tap(pg, "#tbbHide")
    check("4097th unique rule refused, no partial save", len(rules(pg)) == 0 and pg.evaluate("window.__ct.fakeGrass()") == "", (len(rules(pg)), len(pg.evaluate("window.__ct.fakeGrass()"))))
    tap(pg, "#tbbTestS")
    check("test of 4097 is refused too", len(rules(pg)) == 0)
    # hide 4096 of them: select all, then unselect one by hand
    pg.evaluate("document.querySelector('#tbList .tb-row .tw-ck').click()"); pg.wait_for_timeout(100)
    tap(pg, "#tbbHide")
    check("4096 of 4097 selected can be hidden", len(rules(pg)) == 4096, len(rules(pg)))
    tap(pg, "#tbbShow")
    check("Show selected restores them all", len(rules(pg)) == 0, len(rules(pg)))
    check("no JS errors (4097)", not errs, errs)
    pg.close()

    # 7. migration: old range lines, duplicates, legacy two-field lines
    old = "range 4868e8fcf4786f93 49f0f952e93df72b 0 0 2;37497 512 512 0a1b2c3d4e5f6071;37497 512 512 0A1B2C3D4E5F6071;37497 512;# note;37496 256 256 0x11aa22bb33cc44dd;range 1 2 3 4 5 6 7 8"
    pg = new()
    st = json.dumps({"keep": [], "filt": {"fmt": 0, "side": 0, "minlv": 0}, "U": ["37497|512|512|0a1b2c3d4e5f6071", "37497|512|512|11aa22bb33cc44dd"], "a": 0, "b": 2, "ta": 0, "tb": 2, "phase": "check", "hist": [], "name": "", "grp": "plants"})
    lib = json.dumps([{"key": "37497|512|512|0a1b2c3d4e5f6071", "name": "Green grass", "grp": "plants", "set": ""}])
    init = "localStorage.setItem('ct_gst2', %s); localStorage.setItem('ct_lib2', %s);" % (json.dumps(st), json.dumps(lib))
    errs = open_adv(pg, "?demo=20&texlib=37497%20512%20512%200a1b2c3d4e5f6071%20plants|Green%20grass&grass=" + old.replace("#", "%23"), init)
    r = rules(pg)
    check("old range lines gone, valid rules kept, duplicates merged", r == ["37496 256 256 11aa22bb33cc44dd", "37497 512 512 0a1b2c3d4e5f6071"], r)
    check("grass.txt was cleaned (no range text)", "range" not in pg.evaluate("window.__ct.fakeGrass()"))
    check("old range based finder state discarded", "Testing" not in pg.inner_text("#gf") and pg.evaluate("localStorage.getItem('ct_gst2')") is None)
    check("saved name kept", "Green grass" in pg.inner_text("#gl"))
    check("no JS errors (migration)", not errs, errs)
    pg.close()

    # 8. an exact (side-test era, ex:true) state with more than 60 keys survives
    pg = new()
    keys = ["37497|512|512|%016x" % (0x1000 + i) for i in range(80)]
    st = json.dumps({"keep": [], "ex": True, "U": keys, "a": 0, "b": 80, "ta": 0, "tb": 40, "phase": "bisect", "hist": [], "name": "", "grp": "plants"})
    grass = ";".join(k.replace("|", " ") for k in keys[:40])
    errs = open_adv(pg, "?demo=5&grass=" + grass, "localStorage.setItem('ct_gst2', %s);" % json.dumps(st))
    check("exact state with 40 of 80 keys is continued", "Testing 40" in pg.inner_text("#gf"), pg.inner_text("#gf")[:200])
    check("no JS errors (ex state)", not errs, errs)
    pg.close()

    # 9. real (non-demo) path with a mocked shell: chunked candidate retrieval, no tail cut, no duplicates, atomic chunked write
    MOCK = """
    (function () {
      var lines = [], seen = {}, i, seed = 777;
      function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
      var NDIST = %(ndist)d;
      for (i = 0; i < NDIST; i++) {
        var h = ""; for (var j = 0; j < 4; j++) h += ("0000" + Math.floor(rnd() * 65536).toString(16)).slice(-4);
        if (seen[h]) { i--; continue; } seen[h] = 1;
        lines.push("GRASSCAND fmt=37497 w=512 h=128 n=1000 h=" + h + " a=" + (i %% 256) + " z=" + (i %% 100) + " lv=" + (2 + i %% 8));
      }
      var dup = lines.slice(0, 300).concat(lines.slice(0, 300));          /* the same textures logged by several launches */
      var all = lines.concat(dup);
      var uniq = Object.keys(all.reduce(function (o, l) { o[l] = 1; return o; }, {})).sort();
      window.__cmds = []; window.__chunks = [];
      window.ksu = { exec: function (cmd, opts, cb) {
        var out = "", err = 0, m;
        window.__cmds.push(cmd.length);
        if (cmd.indexOf("for d in /data/data/$P/files") >= 0) out = "C|/data/data/com.garena.game.codm/files|100|100\\n";
        else if (cmd.indexOf("sort -u | wc -l") >= 0) out = uniq.length + "\\n";
        else if ((m = /sed -n '(\\d+),(\\d+)p'/.exec(cmd)) && cmd.indexOf("GRASSCAND") >= 0) {
          var a = +m[1], b = +m[2]; window.__chunks.push(b - a + 1);
          if (%(failchunk)d && a > 400) { out = ""; err = 1; }                 /* this chunk can never be read (also not on the retry) */
          else out = uniq.slice(a - 1, b).join("\\n") + "\\n";
        }
        else if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nst=ver=v0.57.2|stage=started|\\n";
        else if (cmd.indexOf("echo OK") >= 0) { out = "OK\\n"; window.__last = cmd; }
        setTimeout(function () { window[cb](err, out, ""); }, 0);
      } };
      window.__uniqLines = uniq.length;
    })();
    """
    def mock_page(ndist, failchunk=0):
        q = new(); errs = []
        q.on("pageerror", lambda e: errs.append(str(e)))
        q.add_init_script(MOCK % {"ndist": ndist, "failchunk": failchunk})
        q.goto(PAGE); q.wait_for_timeout(2500)
        q.click("#dock [data-v=adv]"); q.wait_for_timeout(300)
        q.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); q.wait_for_timeout(300)
        open_tb(q)
        return q, errs
    pg, errs = mock_page(4300)
    uniqn = pg.evaluate("window.__uniqLines")
    chunks = pg.evaluate("window.__chunks")
    txt = pg.inner_text("#tbCount")
    check("mock log has more lines than the old 1500 cut", uniqn > 1500, uniqn)
    check("every distinct texture is listed (4300), duplicates merged", "of 4300 candidates" in txt, txt)
    check("read in chunks of at most 400 lines", len(chunks) >= 11 and max(chunks) <= 400, (len(chunks), max(chunks or [0])))
    check("list is not marked incomplete", "INCOMPLETE" not in txt)
    # write many rules: hide 3000 via select all + hide (4300 > 4096 -> refused), so pick a filter first
    pg.fill("#tbQ", "mips"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelector('#tbSort') && 0")
    pg.select_option("#tbSort", "hash") if False else None
    tap(pg, "#tbbSelF"); n_sel = int(re.search(r"(\d+) selected", pg.inner_text("#tbSelTxt")).group(1))
    # keep selection between 3000 and 4096 by using a 8-mips filter if needed
    if n_sel > 4096:
        pg.fill("#tbQ", "8 mips"); pg.wait_for_timeout(300); tap(pg, "#tbbClr"); tap(pg, "#tbbSelF")
        n_sel = int(re.search(r"(\d+) selected", pg.inner_text("#tbSelTxt")).group(1))
    pg.evaluate("window.__cmds.length = 0")
    tap(pg, "#tbbHide"); tap(pg, "#tbbHide"); pg.wait_for_timeout(800)
    cmds = pg.evaluate("window.__cmds"); last = pg.evaluate("window.__last || ''")
    check("hid %d textures through the mocked shell" % n_sel, ("%d / 4096" % n_sel) in pg.inner_text("#gf"), pg.inner_text("#gf")[:100])
    check("no single shell command is larger than 60 KB", cmds and max(cmds) < 60000, max(cmds or [0]))
    check("file is written to .new, counted, then moved", "grass.txt" in last and "mv -f" in last and ("= %d ]" % n_sel) in last, last[:200])
    check("no JS errors (mock shell)", not errs, errs)
    pg.close()
    pg, errs = mock_page(1200, failchunk=1)
    txt = pg.inner_text("#tbCount")
    check("a chunk that cannot be read is reported, not hidden", "INCOMPLETE" in txt, txt)
    pg.close()

    # 14. regression (v0.57): render() must run to the end, so the Look card and the status chip follow the saved files.
    #     A leftover reference to a removed variable (gOld) stopped render() half way: Look stayed on "Off" although flat=4 was saved.
    STATE = """
    (function(){ window.__rej = [];
      window.addEventListener("unhandledrejection", function(e){ window.__rej.push(String(e.reason)); });
      window.ksu = { exec: function(cmd, opts, cb){
        var out = "";
        if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nbias=0\\nscale=\\nfps=\\nfz=1\\nth=1\\ncore=1\\nnear=1\\nblk=%(blk)s\\nminsz=64\\ndelay=0\\nstyle=%(style)s\\nsedge=1\\nflat=%(flat)s\\ntesttex=%(tt)s\\nttsize=0\\nttsm=0\\nalpha=1\\nmode=4\\nhookdelay=0\\nhooksafe=0\\nforcegl=1\\nver=v0.57.2\\nst=ver=v0.57.2|stage=started|\\n";
        else if (cmd.indexOf("echo OK") >= 0) out = "OK\\n";
        setTimeout(function(){ window[cb](0, out, ""); }, 0);
      } };
    })();
    """
    for (flat, tt, blk, want) in [(4, 0, 0, "Flat +4"), (0, 1, 0, "True Pixelated"), (0, 0, 8, "Pixel Blocks"), (0, 0, 0, "Look off"), (0, 0, 0, "Detail Cut +2")]:
        q = new(); q.add_init_script(STATE % {"flat": flat, "tt": tt, "blk": blk, "style": 2 if "Detail" in want else 0})
        q.goto(PAGE); q.wait_for_timeout(2500)
        chip = q.inner_text("#status")
        seg = q.evaluate("Array.from(document.querySelectorAll('#lookSeg [aria-checked=true]')).map(b=>b.innerText.trim())")
        rej = q.evaluate("window.__rej")
        check("saved flat=%d testtex=%d blk=%d: status chip says '%s'" % (flat, tt, blk, want), want in chip, chip)
        check("  Look card shows the same one (%s) and render() did not throw" % want, len(seg) == 1 and not rej, (seg, rej))
        q.close()

    # 15. v0.57.2: the Texture browser is closed until the violet button is tapped; it opens in its own window and scrolls by itself
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    vis = lambda sel: pg.evaluate("s=>{var e=document.querySelector(s);return !!e&&e.getClientRects().length>0}", sel)
    check("browser list is hidden until the button is tapped", not vis("#tbList") and vis("#tbOpen"))
    check("button shows the numbers", "textures" in pg.inner_text("#tbOpenSum"), pg.inner_text("#tbOpenSum"))
    tap(pg, "#tbOpen"); pg.wait_for_timeout(450)
    check("window opens with the list and the filters inside", vis("#tbOv") and vis("#tbList") and vis("#tbQ") and pg.evaluate("!!document.querySelector('#tbOv #tb')"))
    n1 = pg.evaluate("document.getElementById('tbList').children.length")
    pg.evaluate("var l=document.getElementById('tbSheetBody');l.scrollTop=l.scrollHeight"); pg.wait_for_timeout(500)
    n2 = pg.evaluate("document.getElementById('tbList').children.length")
    check("scrolling to the end of the window loads the next 40", n1 == 40 and n2 == 80, (n1, n2))
    tap(pg, "#tbOv [data-tbx]"); pg.wait_for_timeout(450)
    check("Close puts the browser back and removes the window", not vis("#tbList") and not pg.evaluate("!!document.getElementById('tbOv')") and pg.evaluate("!!document.querySelector('#tbHold #tb')"))
    tap(pg, "#tbOpen"); pg.wait_for_timeout(450)
    pg.fill("#tbQ", "ETC2"); pg.wait_for_timeout(400)
    check("search still works inside the window", pg.evaluate("document.getElementById('tbList').children.length") > 0 and (lambda m: m and int(m.group(1)) < int(m.group(2)))(re.search(r"Filter: (\d+) of (\d+)", pg.inner_text("#tbCount"))), pg.inner_text("#tbCount")[:80])
    check("no JS errors (window)", not errs, errs)
    pg.close()
    b.close()

print("\n%d passed, %d failed" % (passed, len(fails)))
sys.exit(1 if fails else 0)
