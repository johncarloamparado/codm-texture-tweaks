#!/usr/bin/env python3
"""v0.57 WebUI checks (headless Chromium, demo mode). Run: python3 tests/ui_test.py   (needs: pip install playwright; playwright install chromium)
Exit code 0 = all checks passed. Skipped (exit 0, message) when playwright is not available."""
import os, re, sys, json
from urllib.parse import quote
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
    check("see-through search works (total = candidates with mips)", ("of %d candidates" % pg.evaluate("window.__ct.mvis()")) in pg.inner_text("#tbCount"), pg.inner_text("#tbCount"))
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
    pg.evaluate("document.querySelector('#twOv [data-tw=all]').click()"); pg.wait_for_timeout(200)     # v0.58.3: nothing ticked + no filter = nothing picked, so tick all the listed ones first
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
        q = new(); open_adv(q, "?demo=%d" % n); v = q.evaluate("window.__ct.mvis()"); q.close(); return v   # v0.58.1: only textures with mips are listed
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
    v = pg.evaluate("window.__ct.mvis()")
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
        else if (cmd.indexOf("sort -u > $T") >= 0) out = uniq.length + "\\n";
        else if ((m = /sed -n '(\\d+),(\\d+)p' [$]T/.exec(cmd)) && cmd.indexOf("ct_cand") >= 0) {
          var a = +m[1], b = +m[2]; window.__chunks.push(b - a + 1);
          if (%(failchunk)d && a > 400) { out = ""; err = 1; }                 /* this chunk can never be read (also not on the retry) */
          else out = uniq.slice(a - 1, b).join("\\n") + "\\n";
        }
        else if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nst=ver=v0.58|stage=started|\\n";
        else if (cmd.indexOf("WROTE") >= 0) { out = "WROTE\\n"; window.__last = cmd; }
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
        if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nbias=0\\nscale=\\nfps=\\nfz=1\\nth=1\\ncore=1\\nnear=1\\nblk=%(blk)s\\nminsz=64\\ndelay=0\\nstyle=%(style)s\\nsedge=1\\nflat=%(flat)s\\ntesttex=%(tt)s\\nttsize=0\\nttsm=0\\nalpha=1\\nmode=4\\nhookdelay=0\\nhooksafe=0\\nforcegl=1\\nver=v0.57.5\\nst=ver=v0.57.4|stage=started|\\n";
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

    # 16. v0.57.3: OFF all / switches in the test window PAUSE the test (it is not lost); no automatic "Unnamed" entries; Delete buttons; Size = width OR height
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    open_tb(pg)
    pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
    tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
    R0 = len(rules(pg))
    check("16a test is running with %d rules" % R0, R0 > 5 and "Check: everything off" in pg.inner_text("#gf"), pg.inner_text("#gf")[:80])
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
    check("16b OFF all: nothing is hidden any more", len(rules(pg)) == 0, len(rules(pg)))
    check("16c OFF all: the test is NOT lost (state kept, paused)", pg.evaluate("(function(){var s=JSON.parse(localStorage.getItem('ct_gst2')||'null');return !!s&&s.paused===true&&s.U.length>0})()"))
    check("16d window says the test is paused and points to ON all (the two old buttons are gone, v0.58.2)", "Test paused" in pg.inner_text("#twSub") and "ON all" in pg.inner_text("#twSub") and pg.evaluate("!document.getElementById('twPauseRow') && !document.querySelector('#twOv [data-tw=resume]') && !document.querySelector('#twOv [data-tw=end]')"))
    check("16e finder card says Test paused", "Test paused" in pg.inner_text("#gf") and pg.evaluate("!!document.getElementById('gfResume')") and not pg.evaluate("!!document.getElementById('gfYes')"))
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    pg.reload(); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    check("16f paused test survives a reload (grass.txt differs from the test list on purpose)", "Test paused" in pg.inner_text("#gf"), pg.inner_text("#gf")[:100])
    tap(pg, "#gfResume"); pg.wait_for_timeout(300)
    check("16g Back to the test: the test rules are back and the answers show again", len(rules(pg)) == R0 and pg.evaluate("!!document.getElementById('gfYes')") and "Check: everything off" in pg.inner_text("#gf"), (len(rules(pg)), R0))
    check("16h state is not paused any more", pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2')).paused") in (False, None))
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    k0 = pg.evaluate("document.querySelector('#twOv .tw-row').dataset.k")
    tap(pg, '#twOv [data-twsw="%s"]' % k0); pg.wait_for_timeout(300)
    check("16i one switch pauses too (that texture is back, the rest of the test stays hidden)", len(rules(pg)) == R0 - 1 and "Test paused" in pg.inner_text("#gf"), (len(rules(pg)), R0))
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    tap(pg, "#gfEnd"); pg.wait_for_timeout(300)
    check("16j End the test: switches stay, the finder test is gone", len(rules(pg)) == R0 - 1 and pg.evaluate("localStorage.getItem('ct_gst2')") is None and not pg.evaluate("!!document.getElementById('gfResume')"))
    check("16k no JS errors (pause)", not errs, errs)
    # 16k2 (v0.57.4): ON all, or putting the switches back by hand, makes the test run again
    pg.close()
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    open_tb(pg)
    pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
    tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
    R1 = len(rules(pg))
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    check("16k2 paused after OFF all and closing the window", "Test paused" in pg.inner_text("#gf"))
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    tap(pg, "#twOv [data-tw=on]"); pg.wait_for_timeout(300)
    check("16k3 ON all: same as the test list, so the test runs again", len(rules(pg)) == R1 and "Test paused" not in pg.inner_text("#twSub") and pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2')).paused") in (False, None), (len(rules(pg)), R1))
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    check("16k4 finder card shows the running test again", "Test paused" not in pg.inner_text("#gf") and pg.evaluate("!!document.getElementById('gfYes')"))
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    k1 = pg.evaluate("document.querySelector('#twOv .tw-row').dataset.k")
    tap(pg, '#twOv [data-twsw="%s"]' % k1); pg.wait_for_timeout(300)
    check("16k5 one switch off: paused", len(rules(pg)) == R1 - 1 and "Test paused" in pg.inner_text("#twSub"))
    tap(pg, '#twOv [data-twsw="%s"]' % k1); pg.wait_for_timeout(300)
    check("16k6 that switch back on: the test runs again", len(rules(pg)) == R1 and "Test paused" not in pg.inner_text("#twSub"), (len(rules(pg)), R1))
    check("16k7 no JS errors (resume by switches)", not errs, errs)
    pg.close()

    # 16l. a running test (many rules in grass.txt) must not fill My textures with "Unnamed N" when the page is refreshed
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    ks = ["37497|512|512|%016x" % (0x2000 + i) for i in range(30)]
    st = json.dumps({"v": 2, "keep": [], "U": ks, "a": 0, "b": 30, "ta": 0, "tb": 30, "phase": "check", "hist": [], "name": "", "grp": "plants"})
    errs = open_adv(pg, "?demo=5&grass=" + ";".join(k.replace("|", " ") for k in ks), "localStorage.setItem('ct_gst2', %s);" % json.dumps(st))
    check("16l test rules do not become Unnamed entries", pg.evaluate("window.__ct.lib().length") == 0 and "Unnamed" not in pg.inner_text("#gl"), pg.evaluate("window.__ct.lib().length"))
    pg.close()
    # a rule hidden by hand (not in the test) is not added either; it is listed as hidden, not saved
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    errs = open_adv(pg, "?demo=5&grass=37497%20512%20512%20aaaabbbbccccdddd")
    check("16m a loose rule stays out of My textures", pg.evaluate("window.__ct.lib().length") == 0)
    check("16n ...and is listed as hidden, not saved", "1 not saved" in pg.inner_text("#tbTag") if pg.evaluate("!!document.getElementById('tbTag')") else True)
    pg.close()

    # 16o. Delete buttons in My textures
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    tl = ";".join(["37497 512 512 %016x other|Unnamed %d" % (0x3000 + i, i + 1) for i in range(5)] + ["37497 512 512 %016x plants|Green grass" % 0x4000])
    gr = "37497 512 512 %016x" % 0x3000          # the first unnamed one is ON
    errs = open_adv(pg, "?demo=5&texlib=" + tl.replace(" ", "%20").replace("|", "|") + "&grass=" + gr.replace(" ", "%20"))
    check("16o list has 6 entries before", pg.evaluate("window.__ct.lib().length") == 6, pg.evaluate("window.__ct.lib().length"))
    check("16p Forget is now called Delete", "Forget" not in pg.inner_text("#gl") and pg.evaluate("document.querySelectorAll('#gl [data-forget]').length") == 6 and pg.evaluate("document.querySelector('#gl [data-forget]').textContent") == "Delete")
    check("16q bulk buttons show the numbers (4 unnamed OFF, 5 OFF)", "(4)" in pg.evaluate("document.querySelector('[data-libdel=unnamed]').textContent") and "(5)" in pg.evaluate("document.querySelector('[data-libdel=off]').textContent"), pg.evaluate("document.querySelector('#gl .lib-bulk').innerText"))
    tap(pg, "[data-libdel=unnamed]")
    check("16r first tap only asks again", pg.evaluate("window.__ct.lib().length") == 6 and "Tap again" in pg.evaluate("document.querySelector('[data-libdel=unnamed]').textContent"))
    tap(pg, "[data-libdel=unnamed]"); pg.wait_for_timeout(300)
    check("16s second tap deletes the 4 unnamed that are OFF; the ON one and the named one stay", pg.evaluate("window.__ct.lib().length") == 2 and len(rules(pg)) == 1, (pg.evaluate("window.__ct.lib().length"), rules(pg)))
    check("16t the file texlib.txt follows", len([l for l in pg.evaluate("window.__ct.fakeTexlib()").split("\n") if l.strip()]) == 2, pg.evaluate("window.__ct.fakeTexlib()"))
    tap(pg, "[data-libdel=off]"); tap(pg, "[data-libdel=off]"); pg.wait_for_timeout(300)
    check("16u Delete all OFF removes the named OFF one too; the ON one stays", pg.evaluate("window.__ct.lib().length") == 1 and len(rules(pg)) == 1)
    tap(pg, "#gl [data-forget]"); pg.wait_for_timeout(300)
    check("16v Delete on a row removes it and shows the texture again", pg.evaluate("window.__ct.lib().length") == 0 and len(rules(pg)) == 0)
    check("16w no JS errors (delete)", not errs, errs)
    pg.close()

    # 16x. Size filter: the number can be the width OR the height (64 = 64xN and Nx64)
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    errs = open_adv(pg, "?demo=600")
    vl = [c for c in pg.evaluate("window.__ct.visList()") if c["lv"] >= 2]     # v0.58.1: With mips only
    for z in (64, 128):
        exp = sum(1 for c in vl if c["w"] == z or c["h"] == z)
        old_way = sum(1 for c in vl if max(c["w"], c["h"]) == z)
        pg.select_option("#tbSide", str(z)); pg.wait_for_timeout(400)
        got = int(re.search(r"Filter: (\d+) of", pg.inner_text("#tbCount")).group(1))
        opt = pg.evaluate("s=>[...document.querySelectorAll('#tbSide option')].find(o=>o.value==s).textContent", str(z))
        check("16x size %d: filter shows every texture with %d as width or height (%d, longest-side rule would give %d)" % (z, z, exp, old_way), got == exp and exp > old_way, (got, exp, old_way))
        check("16y size %d: the option count is the same (%s)" % (z, opt), ("(%d)" % exp) in opt, opt)
        rows = pg.evaluate("[...document.querySelectorAll('#tbList .tb-row b')].map(e=>e.textContent)")
        ok = all(re.search(r"·\s*(\d+)×(\d+)", r) and (int(re.search(r"·\s*(\d+)×(\d+)", r).group(1)) == z or int(re.search(r"·\s*(\d+)×(\d+)", r).group(2)) == z) for r in rows)
        check("16z size %d: every row shown is %dxN or Nx%d" % (z, z, z), ok and len(rows) > 0, rows[:3])
    pg.select_option("#tbSide", "0"); pg.wait_for_timeout(300)
    # the same in the test window
    pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
    tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
    tk = [c for c in vl if c["fmt"] == 37497]
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    pg.select_option("#twSide", "64"); pg.wait_for_timeout(300)
    exp = sum(1 for c in tk if c["w"] == 64 or c["h"] == 64)
    cnt = pg.inner_text("#twCnt")
    check("16A test window: size 64 shows %d of %d (width or height)" % (exp, len(tk)), ("Showing %d of %d" % (exp, len(tk))) in cnt, cnt)
    check("16B no JS errors (size)", not errs, errs)
    pg.close()

    # 17. v0.57.5: the settings folder is sticky; a newer log on another path never swaps it; a backup follows saves; emptied folder is restored
    FOLD = """
    (function(){ window.__sent = [];
      var cands = %(cands)s, heal = %(heal)s;
      window.ksu = { exec: function(cmd, opts, cb){
        var out = ""; window.__sent.push(cmd);
        if (cmd.indexOf("for d in /data/data/$P/files") >= 0) out = cands;
        else if (cmd.indexOf("RESTORED=") >= 0) out = heal;
        else if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nbias=0\\nflat=0\\nver=v0.58\\nst=ver=v0.57.5|stage=started|\\n";
        else if (cmd.indexOf("WROTE") >= 0) out = "WROTE\\n";
        else if (cmd.indexOf("echo OK") >= 0) out = "OK\\n";
        setTimeout(function(){ window[cb](0, out, ""); }, 0);
      } };
    })();
    """
    A = "C|/data/data/com.garena.game.codm/files|100|100|8|1\\n"
    M = "C|/data_mirror/data_ce/null/0/com.garena.game.codm/files|900|900|8|1\\n"
    def fold(cands, heal="HEALED\\n"):
        q = new(); q.add_init_script(FOLD % {"cands": json.dumps(cands.replace("\\n", "\n")), "heal": json.dumps(heal.replace("\\n", "\n"))})
        q.goto(PAGE); q.wait_for_timeout(2300); return q
    q = fold(A + M)
    check("17a same folder on two paths, the mirror has the newer log: the usual /data/data path stays", q.inner_text("#aDir").startswith("/data/data/"), q.inner_text("#aDir"))
    check("17b the choice is written to a pin file outside the game folder", q.evaluate("window.__sent.some(c => c.indexOf('codm_tex_cfg/pin_u0') >= 0 && c.indexOf('/data/data/') >= 0)"))
    q.close()
    q = fold(A + M + "K|pin_u0|/data_mirror/data_ce/null/0/com.garena.game.codm/files\\n")
    check("17c a pinned folder stays in use even if another path would sort first", q.inner_text("#aDir").startswith("/data_mirror/"), q.inner_text("#aDir"))
    q.close()
    q = fold("C|/data/data/com.garena.game.codm/files|0|0|0|0\\n" + M + "K|pin_u0|/data/data/com.garena.game.codm/files\\n")
    check("17d the pinned folder is left only when it is empty and another path holds the settings", q.inner_text("#aDir").startswith("/data_mirror/"), q.inner_text("#aDir"))
    q.close()
    q = fold(A, heal="RESTORED=7\\nHEALED\\n")
    msg = q.inner_text("#toast")
    check("17e emptied folder: files are put back from the backup and the page says so", "7 files were put back" in msg, msg)
    check("17f the restore only runs once per folder", q.evaluate("window.__sent.filter(c => c.indexOf('RESTORED=') >= 0).length") == 1)
    q.evaluate("document.querySelector('#lookSeg').children[1].click()"); q.wait_for_timeout(2800)
    check("17g after a save the backup is synced a moment later, and the last command of the page is that sync", q.evaluate("window.__sent.filter(c => c.indexOf('SYNCED') >= 0).length") >= 1 and q.evaluate("window.__sent[window.__sent.length - 1].indexOf('SYNCED') >= 0"), q.evaluate("window.__sent.length"))
    q.close()

    # 18. v0.58: the page's own settings (export name, tab, filters, finder test ...) are backed up outside the game folder and come back after a wipe
    import base64
    UIM = """
    (function(){ window.__sent = [];
      sessionStorage.setItem('__loads', String((parseInt(sessionStorage.getItem('__loads') || '0', 10)) + 1));
      %(pre)s
      var uidata = %(uidata)s;
      window.ksu = { exec: function(cmd, opts, cb){
        var out = ""; window.__sent.push(cmd);
        if (cmd.indexOf("for d in /data/data/$P/files") >= 0) out = "C|/data/data/com.garena.game.codm/files|100|100|8|1\\n";
        else if (cmd.indexOf("RESTORED=") >= 0) out = "HEALED\\n";
        else if (cmd.indexOf("cat ") === 0 && cmd.indexOf("ui.b64") >= 0) out = uidata;
        else if (cmd.indexOf("UIWRITEOK") >= 0) out = "UIWRITEOK\\n";
        else if (cmd.indexOf("UIOK") >= 0) out = "UIOK\\n";
        else if (cmd.indexOf("for f in bias scale") >= 0) out = "zyg=rezygisk,\\nbias=0\\nflat=0\\nver=v0.67\\nst=ver=v0.67|stage=started|\\n";
        else if (cmd.indexOf("echo OK") >= 0) out = "OK\\n";
        setTimeout(function(){ window[cb](0, out, ""); }, 0);
      } };
    })();
    """
    def b64(d): return base64.b64encode(json.dumps(d).encode()).decode()
    def uipage(uidata="", pre=""):
        q = new(); q.add_init_script(UIM % {"uidata": json.dumps(uidata), "pre": pre}); q.goto(PAGE); q.wait_for_timeout(3200); return q
    def packed(q):
        parts = q.evaluate("(function(){ var a = window.__sent, st = -1, i; for (i = 0; i < a.length; i++) if (a[i].indexOf('ui.tmp') >= 0 && a[i].indexOf(': > ') >= 0) st = i; return st < 0 ? [] : a.slice(st + 1).filter(c => c.indexOf('printf %s') === 0 && c.indexOf('ui.tmp') >= 0); })()")
        raw = "".join(c.split("'")[1] for c in parts)
        return json.loads(base64.b64decode(raw).decode()) if raw else None
    q = uipage(b64({"ct_name": "Juan", "ct_tab": "tools", "ct_pin": "{}", "ct_savemode": "open"}))
    check("18a wiped browser data: saved export name and other page settings come back from the backup", q.evaluate("localStorage.getItem('ct_name')") == "Juan" and q.evaluate("localStorage.getItem('ct_savemode')") == "open", q.evaluate("localStorage.getItem('ct_name')"))
    check("18b the box shows the restored name and the page says it was put back", q.input_value("#rptName") == "Juan" and "page settings were put back" in q.inner_text("#toast"), q.inner_text("#toast"))
    check("18c it reloads once only (no reload loop) and does not read the backup again", q.evaluate("sessionStorage.getItem('__loads')") == "2" and q.evaluate("window.__sent.filter(c => c.indexOf('ui.b64') >= 0 && c.indexOf('cat ') === 0).length") == 0)
    check("18d routing keys (ct_pin) are not put back from the backup", q.evaluate("localStorage.getItem('ct_pin')") != "{}")
    q.close()
    q = uipage(b64({"ct_name": "Juan", "ct_tab": "tools"}), pre="if (!localStorage.getItem('ct_zz')) { localStorage.setItem('ct_zz', '1'); localStorage.setItem('ct_name', 'Keep'); }")
    check("18e keys that already exist are never overwritten; only the missing ones are put back", q.evaluate("localStorage.getItem('ct_name')") == "Keep" and q.evaluate("localStorage.getItem('ct_tab')") == "tools")
    q.close()
    q = uipage("")
    check("18f no backup yet: nothing is restored and the page does not reload", q.evaluate("sessionStorage.getItem('__loads')") == "1")
    d = packed(q)
    check("18g the first launch writes the backup once", d is not None, d)
    q.evaluate("localStorage.setItem('ct_name', 'Maria'); localStorage.setItem('ct_big', 'x'.repeat(160000)); localStorage.setItem('ct_pin', '{\"0\":\"/x\"}')"); q.wait_for_timeout(2800)
    d = packed(q)
    check("18h a change in the page is written to the backup a moment later", d is not None and d.get("ct_name") == "Maria", d and list(d.keys()))
    check("18i a value over 150 KB and the routing keys are left out", d is not None and "ct_big" not in d and "ct_pin" not in d and "ct_ui_stamp" not in d)
    n0 = q.evaluate("window.__sent.filter(c => c.indexOf('ui.tmp') >= 0 && c.indexOf(': > ') >= 0).length")
    q.evaluate("localStorage.setItem('ct_name', 'Maria')"); q.wait_for_timeout(2800)
    check("18j nothing changed = nothing is written again", q.evaluate("window.__sent.filter(c => c.indexOf('ui.tmp') >= 0 && c.indexOf(': > ') >= 0).length") == n0)
    q.evaluate("(function(){ var e = document.getElementById('rptName'); e.value = 'Ana bel!'; e.dispatchEvent(new Event('change', {bubbles: true})); })()"); q.wait_for_timeout(300)
    check("18k the export name is saved as soon as it is typed (cleaned), not only when exporting", q.evaluate("localStorage.getItem('ct_name')") == "Anabel", q.evaluate("localStorage.getItem('ct_name')"))
    check("18l no shell command is bigger than 60 KB", q.evaluate("Math.max.apply(null, window.__sent.map(c => c.length))") < 60000)
    q.close()

    # 19. v0.58.1 Part 1: Mips filter has only "With mips" (Texture browser and test window); old saved values are normalized; Session summary header is compact
    for old in ("any", "no", "yes", "zzz"):
        pg = new(); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.add_init_script("localStorage.setItem('ct_bf', JSON.stringify({fmt:0, side:0, mips:'%s', see:'any', show:'all', sort:'best'}));" % old)
        pg.goto(PAGE + "?demo=600"); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); open_tb(pg)
        opts = pg.evaluate("[...document.querySelectorAll('#tbMips option')].map(o=>o.value)")
        check("19a old saved Mips '%s': the browser offers only With mips" % old, opts == ["yes"], opts)
        got = int(re.search(r"Filter: (\d+) of", pg.inner_text("#tbCount")).group(1))
        exp = pg.evaluate("window.__ct.mvis()")
        check("19b old saved Mips '%s': the list holds exactly the textures with mips (%d)" % (old, exp), got == exp and exp > 0 and exp < pg.evaluate("window.__ct.vis()"), (got, exp))
        saved = pg.evaluate("JSON.parse(localStorage.getItem('ct_bf')).mips")
        check("19c old saved Mips '%s': the saved value is rewritten to yes" % old, saved == "yes", saved)
        check("19d no JS error with old Mips '%s'" % old, not errs, errs)
        pg.close()
    pg = new(); errs = open_adv(pg, "?demo=117")
    tap(pg, "#gfStart") if pg.evaluate("!!document.getElementById('gfStart')") else None
    pg.evaluate("document.getElementById('twOpen') && document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    topts = pg.evaluate("[...document.querySelectorAll('#twMips option')].map(o=>[o.value,o.textContent])")
    check("19e test window: Mips has only With mips (no Any, no No mips)", topts == [["yes", "With mips"]], topts)
    nm = pg.evaluate("[...document.querySelectorAll('#twOv .tw-row')].filter(r => /no mips/.test(r.innerText)).length")
    check("19f test window lists no 'no mips' textures", nm == 0, nm)
    txt = pg.inner_text("#twOv")
    check("19g the words 'Any mips' and 'No mips' are gone from the test window", "Any mips" not in txt and "No mips" not in txt)
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(200)
    check("19h no JS errors (test window Mips)", not errs, errs)
    pg.close()
    for w in (320, 360, 390, 411):
        pg = b.new_page(viewport={"width": w, "height": 800}); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.goto(PAGE); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=stats]"); pg.wait_for_timeout(300)
        g = pg.evaluate("""(function(){ var r = document.getElementById('sref').getBoundingClientRect(), h = document.querySelector('#v-stats summary.ch > h2').getBoundingClientRect(),
          sv = document.querySelector('#v-stats summary.ch > svg').getBoundingClientRect(), sm = document.querySelector('#v-stats summary.ch').getBoundingClientRect();
          return { gap: r.left - h.right, rl: r.left, rr: r.right, cl: sv.left, cr: sv.right, hr: h.right, sl: sm.left, sr: sm.right, sw: document.documentElement.scrollWidth, iw: window.innerWidth }; })()""")
        check("19i %dpx Session summary: refresh button sits right after the title (gap %.0f px)" % (w, g["gap"]), 0 <= g["gap"] <= 14, g)
        check("19j %dpx: button and chevron do not overlap, chevron is at the far end" % w, g["rr"] <= g["cl"] + 0.5 and g["cr"] <= g["sr"] + 0.5 and g["sr"] - g["cr"] < 6, g)
        check("19k %dpx: no horizontal overflow" % w, g["sw"] <= g["iw"], g)
        pg.evaluate("document.querySelector('#v-stats details').open = false")
        pg.click("#sref"); pg.wait_for_timeout(300)
        check("19l %dpx: refresh works without toggling the card; toast shown" % w, pg.evaluate("document.querySelector('#v-stats details').open") is False and "Stats refreshed" in pg.inner_text("#toast"))
        pg.click("#v-stats summary.ch > svg"); pg.wait_for_timeout(200)
        check("19m %dpx: the chevron still opens the card" % w, pg.evaluate("document.querySelector('#v-stats details').open") is True)
        check("19n %dpx: no JS errors" % w, not errs, errs)
        pg.close()

    # 20. v0.58.2 Part 2: "Back to the test" and "End the test" are gone from the test window; every way back / out still works
    for w in (320, 360, 390, 411):
        pg = b.new_page(viewport={"width": w, "height": 800}); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
        open_tb(pg)
        pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
        tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
        R = len(rules(pg))
        pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
        txt = pg.inner_text("#twOv")
        check("20a %dpx: no 'Back to the test' / 'End the test' text or buttons in the test window" % w, "Back to the test" not in txt and "End the test" not in txt and pg.evaluate("!document.getElementById('twPauseRow') && !document.querySelector('#twOv [data-tw=resume]') && !document.querySelector('#twOv [data-tw=end]')"), txt[:200])
        check("20b %dpx: ON all and OFF all are there" % w, pg.evaluate("!!document.querySelector('#twOv [data-tw=on]') && !!document.querySelector('#twOv [data-tw=off]')"))
        tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
        check("20c %dpx: OFF all pauses (state kept), nothing hidden" % w, len(rules(pg)) == 0 and pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2')).paused") is True)
        check("20d %dpx: while paused the window explains how to get back and no text names a missing button" % w, "ON all" in pg.inner_text("#twSub") and "Back to the test" not in pg.inner_text("#twOv"))
        tap(pg, "#twOv [data-tw=on]"); pg.wait_for_timeout(300)
        check("20e %dpx: ON all puts the test back (same rules, not paused)" % w, len(rules(pg)) == R and pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2')).paused") in (False, None), (len(rules(pg)), R))
        tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
        pg.reload(); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
        check("20f %dpx: a paused test survives a reload and the finder card offers Back to the test / End / Cancel" % w, "Test paused" in pg.inner_text("#gf") and pg.evaluate("!!document.getElementById('gfResume') && !!document.getElementById('gfEnd') && !!document.getElementById('gfOff')"))
        pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
        check("20g %dpx: the reopened window still says paused and ON all brings the test back" % w, "Test paused" in pg.inner_text("#twSub"))
        tap(pg, "#twOv [data-tw=on]"); pg.wait_for_timeout(300)
        check("20h %dpx: after the reload ON all resumes too" % w, len(rules(pg)) == R and "Test paused" not in pg.inner_text("#twSub"), (len(rules(pg)), R))
        tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
        tap(pg, "#gfOff"); pg.wait_for_timeout(300)
        check("20i %dpx: Cancel the whole search from the card still ends and restores" % w, pg.evaluate("localStorage.getItem('ct_gst2')") is None and len(rules(pg)) == 0 and not pg.evaluate("!!document.getElementById('gfResume')"), len(rules(pg)))
        check("20j %dpx: no horizontal overflow, no JS errors" % w, pg.evaluate("document.documentElement.scrollWidth <= window.innerWidth") and not errs, errs)
        pg.close()
    # 20k. an ended test (End the test in the card) leaves the window usable, with no pause text
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    open_tb(pg); pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
    tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
    R = len(rules(pg))
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    k0 = pg.evaluate("document.querySelector('#twOv .tw-row').dataset.k")
    tap(pg, '#twOv [data-twsw="%s"]' % k0); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    tap(pg, "#gfEnd"); pg.wait_for_timeout(300)
    check("20k End the test (card): switches stay, test gone", len(rules(pg)) == R - 1 and pg.evaluate("localStorage.getItem('ct_gst2')") is None)
    check("20l no JS errors (end from card)", not errs, errs)
    pg.close()

    # 21. v0.58.3 Part 3: Side test also works on the ticked textures while the test is PAUSED (one function, twSideInfo, decides)
    def start_side(pg, q="?demo=300"):
        pg.goto(PAGE + q); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
        open_tb(pg); pg.select_option("#tbFmt", "37497"); pg.wait_for_timeout(400)
        tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS")
        pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    def gstj(pg): return pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2'))")
    def tick(pg, n):
        ks = pg.evaluate("[...document.querySelectorAll('#twOv .tw-row')].map(e=>e.dataset.k)")[:n]
        for k in ks: tap(pg, '#twOv [data-twck="%s"]' % k)
        return ks
    for w in (320, 360, 390, 411):
        pg = b.new_page(viewport={"width": w, "height": 800}); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        start_side(pg)
        g0 = gstj(pg); R0 = len(rules(pg))
        check("21a %dpx: nothing picked = Side test disabled, with a reason" % w, pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled") and "Nothing is picked" in pg.inner_text("#twSideNote") and "pick some first" in pg.inner_text("#twOv [data-tw=side]"), pg.inner_text("#twSideNote"))
        ks = tick(pg, 3)
        sb = pg.inner_text("#twOv [data-tw=side]")
        check("21b %dpx: 3 ticked = enabled, label shows the count, note says Ready" % w, (not pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled")) and "only the 3 ticked" in sb and "Ready" in pg.inner_text("#twSideNote"), (sb, pg.inner_text("#twSideNote")))
        tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(300)
        check("21c %dpx: OFF all pauses the test; the ticks stay and the Side test button stays enabled" % w, gstj(pg).get("paused") is True and len(rules(pg)) == 0 and not pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled") and "only the 3 ticked" in pg.inner_text("#twOv [data-tw=side]") and "Your own switches are replaced" in pg.inner_text("#twSideNote"), pg.inner_text("#twSideNote"))
        tap(pg, "#twOv [data-tw=side]"); pg.wait_for_timeout(400)
        g1 = gstj(pg); r = rules(pg)
        check("21d %dpx: side test from the paused state = a normal gst.sub of exactly the ticked keys, not paused" % w, not g1.get("paused") and g1.get("sub") and sorted(g1["sub"]["keys"]) == sorted(ks), g1.get("sub"))
        check("21e %dpx: hist / a / b / ta / tb / phase / name / grp / keep / U are untouched" % w, all(g1[k] == g0[k] for k in ("hist", "a", "b", "ta", "tb", "phase", "name", "grp", "keep", "U")))
        check("21f %dpx: grass.txt = keep + the 3 ticked, exact rules" % w, len(r) == len(g0["keep"]) + 3 and all(EXACT.match(x) for x in r), len(r))
        check("21g %dpx: the card shows the running side test and the window is closed" % w, "Side test · 3" in pg.inner_text("#gf") and not pg.evaluate("!!document.getElementById('twOv')"))
        tap(pg, "#gfSubNo"); pg.wait_for_timeout(400)
        g2 = gstj(pg)
        check("21h %dpx: 'still there' = main list again, same rules as before the side test" % w, not g2.get("sub") and len(rules(pg)) == R0 and g2["ta"] == g0["ta"] and g2["tb"] == g0["tb"], (len(rules(pg)), R0))
        check("21i %dpx: no horizontal overflow, no JS errors" % w, pg.evaluate("document.documentElement.scrollWidth <= window.innerWidth") and not errs, errs)
        pg.close()
    # 21j+ one width: more rules of the semantics
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    start_side(pg)
    tap(pg, "#twOv [data-tw=all]"); pg.wait_for_timeout(200)
    n_listed = int(re.search(r"ticked (\d+)", pg.inner_text("#twCnt")).group(1))
    tot = len(gstj(pg)["U"]) if False else pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2')).tb - JSON.parse(localStorage.getItem('ct_gst2')).ta")
    if n_listed >= tot:
        check("21j everything ticked = refused, disabled with a reason", pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled") and "needs fewer than all" in pg.inner_text("#twSideNote"), pg.inner_text("#twSideNote"))
        pg.evaluate("document.querySelector('#twOv [data-tw=side]').click()"); pg.wait_for_timeout(200)
        check("21j2 a click on the disabled button changes nothing", not gstj(pg).get("sub"))
    else:
        check("21j everything listed ticked is still fewer than the test list: allowed (textures without mips are not listed)", not pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled"))
    tap(pg, "#twOv [data-tw=none]"); pg.wait_for_timeout(150)
    pg.fill("#twQ", "512"); pg.wait_for_timeout(300)
    shown = int(re.search(r"Showing (\d+) of", pg.inner_text("#twCnt")).group(1))
    lab = pg.inner_text("#twOv [data-tw=side]")
    if 0 < shown < tot: check("21k search only: the matches are used (%d shown)" % shown, ("only the %d shown" % shown) in lab, lab)
    ks = tick(pg, 2)
    check("21l ticks win over the search", "only the 2 ticked" in pg.inner_text("#twOv [data-tw=side]"), pg.inner_text("#twOv [data-tw=side]"))
    tap(pg, "#twOv [data-tw=none]"); pg.fill("#twQ", "zzzznomatch"); pg.wait_for_timeout(300)
    check("21m search with no match = nothing picked, disabled", pg.evaluate("document.querySelector('#twOv [data-tw=side]').disabled") and "Nothing is picked" in pg.inner_text("#twSideNote"), pg.inner_text("#twSideNote"))
    pg.fill("#twQ", ""); pg.wait_for_timeout(200)
    ks = tick(pg, 4)
    tap(pg, "#twOv [data-tw=side]"); pg.wait_for_timeout(400)
    # narrowing a running side test: open the window again, tick 2 of the 4, side test again
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    ks2 = tick(pg, 2)
    tap(pg, "#twOv [data-tw=side]"); pg.wait_for_timeout(400)
    g = gstj(pg)
    check("21n narrowing a side test replaces gst.sub with the smaller subset", g.get("sub") and sorted(g["sub"]["keys"]) == sorted(ks2), g.get("sub"))
    # reload with a running side test (give the demo the same grass.txt)
    gr = pg.evaluate("window.__ct.fakeGrass()")
    pg.close()
    pg = new(); errs2 = []
    pg.on("pageerror", lambda e: errs2.append(str(e)))
    pg.add_init_script("localStorage.setItem('ct_gst2', %s)" % json.dumps(json.dumps(g)))
    pg.goto(PAGE + "?demo=300&grass=" + gr.replace("\n", ";").replace(" ", "%20")); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    check("21o reload with a running side test: the card still shows it", "Side test · 2" in pg.inner_text("#gf"), (pg.inner_text("#gf")[:300], gr[:80], pg.evaluate("localStorage.getItem('ct_gst2')")))
    check("21p no JS errors (semantics)", not errs and not errs2, (errs, errs2))
    pg.close()

    # 22. v0.60 Part 4: "Return to last test" (one checkpoint, key ct_gcp)
    def cp_open(pg, q="?demo=300", init=None, ui=None):
        if init: pg.add_init_script(init)
        pg.goto(PAGE + q); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    def cp_start(pg, fmt="37497"):
        open_tb(pg); pg.select_option("#tbFmt", fmt); pg.wait_for_timeout(400)
        tap(pg, "#tbbSelF"); tap(pg, "#tbbTestS"); pg.wait_for_timeout(200)
    def gj(pg): return pg.evaluate("JSON.parse(localStorage.getItem('ct_gst2'))")
    def cpj(pg): return pg.evaluate("JSON.parse(localStorage.getItem('ct_gcp'))")
    def has(pg, sel): return pg.evaluate("s=>!!document.querySelector(s)", sel)
    def n_ret(pg): return pg.evaluate("[...document.querySelectorAll('#gf button')].filter(b=>/Return to last test/.test(b.innerText)).length")
    SAME = ("U", "keep", "a", "b", "ta", "tb", "phase", "hist", "name", "grp")
    def to_found(pg):
        tap(pg, "#gfYes")
        for _ in range(40):
            if not has(pg, "#gfYes"): break
            tap(pg, "#gfYes")
    def to_pend(pg):
        tap(pg, "#gfYes")
        for _ in range(60):
            if has(pg, "#gfKeepSet") or not has(pg, "#gfNo"): break
            tap(pg, "#gfNo")

    # 22a-22e: per width
    for w in (320, 360, 390, 411):
        pg = b.new_page(viewport={"width": w, "height": 800}); errs = []
        pg.on("pageerror", lambda e: errs.append(str(e)))
        cp_open(pg); cp_start(pg)
        check("22a %dpx: no checkpoint yet: no Return control, no ct_gcp" % w, n_ret(pg) == 0 and not has(pg, "#gfReturn") and pg.evaluate("localStorage.getItem('ct_gcp')") is None)
        R0 = sorted(rules(pg)); g0 = gj(pg)
        tap(pg, "#gfYes"); tap(pg, "#gfYes")             # two answers: check -> bisect -> narrower
        g1 = gj(pg); R1 = sorted(rules(pg))
        check("22b %dpx: the test moved on (history 2)" % w, len(g1["hist"]) == 2 and g1["phase"] == "bisect", g1["hist"])
        check("22c %dpx: while a test runs there is no Return control" % w, n_ret(pg) == 0)
        tap(pg, "#gfOff"); pg.wait_for_timeout(300)                                   # Cancel the whole search
        c = cpj(pg)
        check("22e %dpx: Cancel takes the checkpoint (why cancel); the switches are back to before" % w, c and c["why"] == "cancel" and gj(pg) is None and len(rules(pg)) == 0 and all(c["gst"][k] == g1[k] for k in SAME), c and c["why"])
        check("22f %dpx: ONE control, in the finder card, with one line of what it returns to" % w, n_ret(pg) == 1 and pg.evaluate("document.querySelectorAll('#gfReturn').length") == 1 and ("%d textures, step 3" % (g1["tb"] - g1["ta"])) in pg.inner_text("#gfReturn"), pg.inner_text("#gfReturn"))
        pg.evaluate("document.getElementById('gfReturn').scrollIntoView()")
        check("22g %dpx: the words 'Return to last test' are only on this one control (not in the page text elsewhere, no Home toggle)" % w, pg.evaluate("document.body.innerText.split('Return to last test').length - 1") == 1 and not pg.evaluate("/Remove grass/i.test(document.getElementById('v-home') ? document.getElementById('v-home').innerText : '')"))
        m = pg.evaluate("(function(){ var b = document.getElementById('gfReturn').getBoundingClientRect(); return { l: b.left, r: b.right, sw: document.documentElement.scrollWidth, iw: window.innerWidth, cw: document.getElementById('gfReturn').scrollWidth, ow: document.getElementById('gfReturn').clientWidth, h: b.height }; })()")
        check("22h %dpx: no horizontal overflow, the control is inside the screen and not clipped" % w, m["sw"] <= m["iw"] and m["l"] >= 0 and m["r"] <= m["iw"] and m["cw"] <= m["ow"] + 1 and m["h"] >= 40, m)
        tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
        g2 = gj(pg)
        check("22i %dpx: Return restores list / slice / phase / history / name / group / keep" % w, g2 and all(g2[k] == g1[k] for k in SAME) and not g2.get("paused") and not g2.get("sub"))
        check("22j %dpx: the test rules are written again (same as before Cancel), exact rules" % w, sorted(rules(pg)) == R1 and all(EXACT.match(x) for x in rules(pg)))
        check("22k %dpx: after a Return the checkpoint is gone and the control with it" % w, pg.evaluate("localStorage.getItem('ct_gcp')") is None and n_ret(pg) == 0 and has(pg, "#gfYes"))
        check("22l %dpx: no JS errors" % w, not errs, errs)
        pg.close()

    # 22d: Return while ANOTHER test is running is refused (reason, nothing written, the checkpoint stays)
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); tap(pg, "#gfYes"); tap(pg, "#gfOff"); pg.wait_for_timeout(300)
    c0 = pg.evaluate("localStorage.getItem('ct_gcp')")
    cp_start(pg, "37497")                      # a new test starts (the control is hidden while it runs)
    g1 = gj(pg); R1 = sorted(rules(pg))
    check("22d1 while the new test runs the Return control is hidden", n_ret(pg) == 0)
    pg.evaluate("window.__ct.ret()"); pg.wait_for_timeout(200)
    check("22d2 Return while a test is running: refused with a reason, rules / test / checkpoint unchanged", sorted(rules(pg)) == R1 and gj(pg) == g1 and pg.evaluate("localStorage.getItem('ct_gcp')") == c0 and "running test" in pg.inner_text("#toast"), pg.inner_text("#toast"))
    tap(pg, "#gfYes"); tap(pg, "#gfOff"); pg.wait_for_timeout(300)
    check("22d3 the newer test replaces the checkpoint (only the latest is kept)", pg.evaluate("localStorage.getItem('ct_gcp')") != c0 and n_ret(pg) == 1)
    check("22d4 no JS errors", not errs, errs)
    pg.close()

    # 22m: End the test (while paused) keeps the switches; Return puts the test back, with my own switches
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); tap(pg, "#gfYes")
    g1 = gj(pg)
    pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(300)
    k0 = pg.evaluate("document.querySelector('#twOv .tw-row').dataset.k")
    tap(pg, "#twOv [data-tw=off]"); pg.wait_for_timeout(250)
    tap(pg, '#twOv [data-twsw="%s"]' % k0); pg.wait_for_timeout(250)           # one of my own switches: ON for k0
    pg.evaluate("document.querySelector('#twOv [data-tw=close]').click()"); pg.wait_for_timeout(300)
    sw1 = sorted(rules(pg))
    check("22m paused with one own switch", gj(pg)["paused"] is True and sw1 == [k0.replace("|", " ")] , (sw1, k0))
    tap(pg, "#gfEnd"); pg.wait_for_timeout(300)
    c = cpj(pg)
    check("22n End while paused: the checkpoint holds the paused test and my own switches (sw)", c and c["why"] == "end" and c["gst"]["paused"] is True and c["sw"] == [k0] and gj(pg) is None and sorted(rules(pg)) == sw1, c and (c["why"], c["sw"]))
    check("22o the control says the test is paused and the own switches come back", "paused" in pg.inner_text("#gfReturn") and "step 2" in pg.inner_text("#gfReturn"), pg.inner_text("#gfReturn"))
    # hide another texture by hand (Texture browser): Return would turn it off -> two taps
    other = pg.evaluate("window.__ct.visList().filter(x=>x.fmt!=37497&&x.lv>=2).map(x=>x.fmt)[0]")
    open_tb(pg)
    if other:
        pg.select_option("#tbFmt", str(other)); pg.wait_for_timeout(300); tap(pg, "#tbbSelF"); tap(pg, "#tbbHide"); tap(pg, "#tbbHide"); pg.wait_for_timeout(300)
    before = sorted(rules(pg))
    tap(pg, "#gfReturn"); pg.wait_for_timeout(300)
    if other:
        check("22p Return that would turn my other switches off asks first (tap again), nothing written yet", "Tap again" in pg.inner_text("#gfReturn") and sorted(rules(pg)) == before and cpj(pg) is not None, pg.inner_text("#gfReturn")[:80])
        tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
    g3 = gj(pg)
    check("22q Return restores the paused test and exactly my own switches", g3 and g3["paused"] is True and all(g3[k] == g1[k] for k in SAME) and sorted(rules(pg)) == sw1, (sorted(rules(pg)), sw1))
    check("22r the card shows the paused test with Back to the test / End / Cancel", "Test paused" in pg.inner_text("#gf") and has(pg, "#gfResume") and has(pg, "#gfEnd") and n_ret(pg) == 0)
    tap(pg, "#gfResume"); pg.wait_for_timeout(300)
    check("22s Back to the test works after a Return (switches = test rules, not paused)", gj(pg).get("paused") in (False, None) and len(rules(pg)) == len(g1["keep"]) + g1["tb"] - g1["ta"])
    check("22t no JS errors (end / paused / return)", not errs, errs)
    pg.close()

    # 22u: a texture found: the checkpoint is the LAST STEP shown (before the answer), and deleting the saved texture clears it
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); to_found(pg)
    c = cpj(pg)
    check("22u found: checkpoint taken (why found) and points to the saved texture", c and c["why"] == "found" and c["found"] and gj(pg) is None, c and c["why"])
    g = c["gst"]
    check("22v the checkpoint is the last step that was shown: 1 texture off, history without the last answer", g["tb"] - g["ta"] == 1 and g["phase"] == "bisect" and g["U"][g["ta"]] == c["found"], (g["tb"] - g["ta"], g["phase"]))
    check("22w the card offers Return (step %d)" % (len(g["hist"]) + 1), n_ret(pg) == 1 and ("step %d" % (len(g["hist"]) + 1)) in pg.inner_text("#gfReturn"), pg.inner_text("#gfReturn"))
    pg.evaluate("document.querySelectorAll('#gl details').forEach(d=>d.open=true)")
    fk = pg.evaluate("document.querySelector('#gl [data-forget]').getAttribute('data-forget')")
    check("22x the found texture is in My textures", fk == c["found"], fk)
    tap(pg, "#gl [data-forget]"); pg.wait_for_timeout(400)
    check("22y Delete of the saved texture clears the checkpoint (no stale Return)", pg.evaluate("localStorage.getItem('ct_gcp')") is None and n_ret(pg) == 0)
    pg.close()
    pg = new(); errs3 = []
    pg.on("pageerror", lambda e: errs3.append(str(e)))
    cp_open(pg); cp_start(pg); to_found(pg)
    c = cpj(pg); R0 = len(rules(pg))
    tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
    g = gj(pg)
    check("22z found -> Return: the last step is back (1 texture off, same history), exact rules", g and g["tb"] - g["ta"] == 1 and g["hist"] == c["gst"]["hist"] and len(rules(pg)) == len(g["keep"]) + 1 and "Test" in pg.inner_text("#gf"), g)
    check("22z1 the found texture stays in My textures", len(pg.evaluate("window.__ct.lib()")) == 1)
    check("22z2 no JS errors (found)", not errs and not errs3, (errs, errs3))
    pg.close()

    # 22aa: nothing changed (fail at Test 1) -> Return restores the Test 1 (all textures off)
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg)
    g0 = gj(pg); N0 = len(rules(pg))
    tap(pg, "#gfNo"); pg.wait_for_timeout(300)
    c = cpj(pg)
    check("22aa fail: checkpoint why fail, the search stopped note is shown, the switches are back", c and c["why"] == "fail" and "Search stopped" in pg.inner_text("#gf") and len(rules(pg)) == 0 and gj(pg) is None, c and c["why"])
    check("22ab fail: the checkpoint is Test 1 (phase check, all textures, history empty)", c["gst"]["phase"] == "check" and c["gst"]["hist"] == [] and all(c["gst"][k] == g0[k] for k in SAME))
    tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
    check("22ac Return after a failed search: Test 1 is running again with all textures off; the old 'Search stopped' note is gone", len(rules(pg)) == N0 and gj(pg)["phase"] == "check" and "Search stopped" not in pg.inner_text("#gf"))
    check("22ad no JS errors (fail)", not errs, errs)
    pg.close()

    # 22ae: a pending set (needs several together): Discard takes the last test step; Keep all clears nothing but records a found checkpoint
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); to_pend(pg)
    check("22ae the demo reached the pending set card", has(pg, "#gfKeepSet") and has(pg, "#gfDropSet"))
    if has(pg, "#gfDropSet"):
        check("22af while a set is pending there is no Return control", n_ret(pg) == 0)
        pend = pg.evaluate("JSON.parse(localStorage.getItem('ct_gset'))")
        tap(pg, "#gfDropSet"); pg.wait_for_timeout(300)
        c = cpj(pg)
        check("22ag Discard takes the checkpoint: the last single-texture check (phase verify)", c and c["why"] == "drop" and c["gst"]["phase"] == "verify" and c["gst"]["tb"] - c["gst"]["ta"] == 1 and c["gst"]["U"] == pend["U"], c and c["gst"]["phase"])
        tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
        g = gj(pg)
        check("22ah Return after Discard: a normal test in its last check (1 texture off)", g and g["phase"] == "verify" and len(rules(pg)) == len(g["keep"]) + 1 and has(pg, "#gfNo"))
    check("22ai no JS errors (pending)", not errs, errs)
    pg.close()

    # 22aj: reload keeps the checkpoint; Return works after the reload (new page, same browser data)
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); tap(pg, "#gfYes"); g1 = gj(pg)
    tap(pg, "#gfOff"); pg.wait_for_timeout(300)
    pg.reload(); pg.wait_for_timeout(900)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
    check("22aj after a reload the Return control is still there", n_ret(pg) == 1 and cpj(pg)["why"] == "cancel")
    tap(pg, "#gfReturn"); pg.wait_for_timeout(400)
    g2 = gj(pg)
    check("22ak Return after the reload restores the test", g2 and all(g2[k] == g1[k] for k in SAME) and len(rules(pg)) == len(g1["keep"]) + g1["tb"] - g1["ta"])
    check("22al no JS errors (reload)", not errs, errs)
    pg.close()

    # 22am: invalid / stale checkpoints are dropped silently, never a crash, never a wrong Return
    good = None
    pg = new(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    cp_open(pg); cp_start(pg); tap(pg, "#gfOff"); pg.wait_for_timeout(300); good = pg.evaluate("localStorage.getItem('ct_gcp')"); pg.close()
    gv = json.loads(good)
    def broken(fn):
        d = json.loads(good); fn(d); return json.dumps(d)
    K = gv["gst"]["U"][0]
    cases = {
        "not json": "{oops",
        "wrong version": broken(lambda d: d.__setitem__("v", 9)),
        "unknown why": broken(lambda d: d.__setitem__("why", "zzz")),
        "bad key": broken(lambda d: d["gst"]["U"].__setitem__(0, "37497 512 512 abc")),
        "slice empty": broken(lambda d: d["gst"].__setitem__("tb", d["gst"]["ta"])),
        "slice outside": broken(lambda d: d["gst"].__setitem__("tb", len(d["gst"]["U"]) + 5)),
        "bad phase": broken(lambda d: d["gst"].__setitem__("phase", "x")),
        "sub key not in list": broken(lambda d: d["gst"].__setitem__("sub", {"keys": ["37497|1|1|abcd"]})),
        "hist not a list": broken(lambda d: d["gst"].__setitem__("hist", "x")),
        "over 4096 rules": broken(lambda d: d["gst"].__setitem__("keep", ["1|%d|1|%x" % (i, i + 1) for i in range(4100)])),
        "sw not keys": broken(lambda d: d.__setitem__("sw", ["x"])),
        "empty object": "{}", "a string": '"abc"', "null": "null",
    }
    bad_ok = 0
    for nm, val in cases.items():
        pg = new(); er = []
        pg.on("pageerror", lambda e: er.append(str(e)))
        pg.add_init_script("if (!sessionStorage.getItem('z')) { sessionStorage.setItem('z', '1'); localStorage.setItem('ct_gcp', %s); }" % json.dumps(val))
        pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(700)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(250)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(250)
        ok = n_ret(pg) == 0 and pg.evaluate("localStorage.getItem('ct_gcp')") is None and not er and len(rules(pg)) == 0
        check("22am invalid checkpoint dropped silently (%s)" % nm, ok, (n_ret(pg), pg.evaluate("localStorage.getItem('ct_gcp')"), er))
        pg.close()
    pg = new(); er = []
    pg.on("pageerror", lambda e: er.append(str(e)))
    pg.add_init_script("if (!sessionStorage.getItem('z')) { sessionStorage.setItem('z', '1'); localStorage.setItem('ct_gcp', %s); }" % json.dumps(good))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(700)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(250)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(250)
    check("22an the same data, valid, is offered (so the drops above are the validation, not the setup)", n_ret(pg) == 1 and not er)
    pg.close()
    # exactly 4096 rules is valid and Return writes exactly 4096; 4097 is dropped (above)
    big = json.loads(good); g = big["gst"]
    ks = ["37497|%d|1|%016x" % (i % 1000 + 1, i + 1) for i in range(4096)]
    g["U"] = ks[:2048]; g["keep"] = ks[2048:]; g["a"] = 0; g["b"] = 2048; g["ta"] = 0; g["tb"] = 2048; g["hist"] = []; g["phase"] = "check"; g["sub"] = None
    pg = new(); er = []
    pg.on("pageerror", lambda e: er.append(str(e)))
    pg.add_init_script("if (!sessionStorage.getItem('z')) { sessionStorage.setItem('z', '1'); localStorage.setItem('ct_gcp', %s); }" % json.dumps(json.dumps(big)))
    pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(700)
    pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(250)
    pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(250)
    tap(pg, "#gfReturn"); pg.wait_for_timeout(500)
    check("22ao exactly 4096 exact rules: Return writes all of them", len(rules(pg)) == 4096 and gj(pg) is not None and not er, (len(rules(pg)), er))
    pg.close()

    # 22ap: Short mode and sizes: the control and its line stay visible
    for w in (320, 411):
        pg = b.new_page(viewport={"width": w, "height": 800}); er = []
        pg.on("pageerror", lambda e: er.append(str(e)))
        pg.add_init_script("localStorage.setItem('ct_ui_guide', 'short')")
        cp_open(pg); cp_start(pg); tap(pg, "#gfYes"); tap(pg, "#gfOff"); pg.wait_for_timeout(300)
        m = pg.evaluate("(function(){ var b = document.getElementById('gfReturn'), s = b.querySelector('small'); return { short: document.documentElement.classList.contains('short'), vis: b.offsetHeight > 0, svis: getComputedStyle(s).display !== 'none' && s.offsetHeight > 0, sw: document.documentElement.scrollWidth, iw: window.innerWidth, t: s.innerText }; })()")
        check("22ap %dpx Short mode: the control and its line are visible, no overflow" % w, m["short"] and m["vis"] and m["svis"] and m["sw"] <= m["iw"] and "step" in m["t"], m)
        tap(pg, "#gfReturn"); pg.wait_for_timeout(300)
        check("22aq %dpx Short mode: Return works" % w, gj(pg) is not None and not er, er)
        pg.close()

    # 22ar: the checkpoint is in the page-settings backup (a ct_ key) and never in the routing keys
    q = uipage("")
    q.evaluate("localStorage.setItem('ct_gcp', %s)" % json.dumps(good)); q.wait_for_timeout(2800)
    d = packed(q)
    check("22ar the page-settings backup contains ct_gcp", d is not None and "ct_gcp" in d and json.loads(d["ct_gcp"])["why"] == "cancel", d and list(d.keys()))
    q.close()


    # 23. v0.60 Part 5: a set saved with "Save as ONE set" is ONE container in My textures (one switch, one name / group, Delete for the whole set)
    def ck(k): return k.replace("|", " ")
    def tlines(*rows): return ";".join(rows).replace(" ", "%20")
    pgk = new(); pgk.goto(PAGE + "?demo=300"); pgk.wait_for_timeout(700)
    CK = [c["key"] for c in pgk.evaluate("window.__ct.visList()") if c["lv"] >= 2][:12]
    pgk.close()
    S3 = CK[0:3]; T2 = CK[3:5]; ONE = CK[5]
    def lib_page(w, rows, grass=(), init=None, h=900):
        pg = b.new_page(viewport={"width": w, "height": h}); er = []
        pg.on("pageerror", lambda e: er.append(str(e)))
        if init: pg.add_init_script(init)
        pg.goto(PAGE + "?demo=300&texlib=" + tlines(*rows) + "&grass=" + ";".join(ck(k) for k in grass).replace(" ", "%20")); pg.wait_for_timeout(900)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('#gl details.lib-set').forEach(d=>d.open=false)"); pg.wait_for_timeout(150)
        return pg, er
    ROWS = [ck(k) + " plants~sset3|Green grass" for k in S3] + [ck(ONE) + " effects|Smoke"] + [ck(k) + " other~sset2|Bushes" for k in T2]
    def nsets(pg): return pg.evaluate("document.querySelectorAll('#gl details.lib-set').length")
    def lib_sets(pg): return pg.evaluate("(function(){var m={};window.__ct.lib().forEach(function(e){var k=e.set||'-';(m[k]=m[k]||[]).push([e.name,e.grp]);});return m;})()")
    def tfile(pg): return [l for l in pg.evaluate("window.__ct.fakeTexlib()").split("\n") if l.strip()]
    def toast_txt(pg): return pg.evaluate("document.getElementById('toast').innerText")
    def tb_pick(pg, keys):
        """tick these textures in the Texture browser (found with the id search, the selection stays while the filter changes)"""
        for k in keys:
            pg.fill("#tbQ", "id:" + k.split("|")[3]); pg.wait_for_timeout(350)
            tap(pg, '#tbList .tw-ck[data-tbck="%s"]' % k)
        pg.fill("#tbQ", ""); pg.wait_for_timeout(300)

    # 23a-23g: per width
    for w in (320, 360, 390, 411):
        pg, er = lib_page(w, ROWS, S3)
        check("23a %dpx: 6 saved textures show as 2 set containers + 1 single row (not 6 rows)" % w, nsets(pg) == 2 and pg.evaluate("document.querySelectorAll('#gl .item.lib').length") == 3, (nsets(pg), pg.evaluate("document.querySelectorAll('#gl .item.lib').length")))
        m = pg.evaluate("(function(){var d=document.querySelector('#gl details.lib-set[data-set=sset3]');var s=d.querySelector('summary');return {sw:d.querySelectorAll('[data-toggle]').length, role:d.querySelector('[role=switch]').getAttribute('aria-checked'), t:s.innerText, sh:s.getBoundingClientRect().height, bh:d.querySelector('.lib-sw').getBoundingClientRect().height, bw:d.querySelector('.lib-sw').getBoundingClientRect().width};})()")
        check("23b %dpx: the summary shows the name, '3 textures', the state and has exactly ONE switch (>= 40 px)" % w, m["sw"] == 1 and m["role"] == "true" and "Green grass" in m["t"] and "3 textures" in m["t"] and "hidden" in m["t"] and m["sh"] >= 40 and m["bh"] >= 40 and m["bw"] >= 40, m)
        pg.click("#gl details.lib-set[data-set=sset3] > summary .lib-sum"); pg.wait_for_timeout(250)
        m = pg.evaluate("(function(){var d=document.querySelector('#gl details.lib-set[data-set=sset3]');return {open:d.open, li:d.querySelectorAll('.lib-mem li').length, msw:d.querySelectorAll('.lib-mem button,.lib-mem input,.lib-mem select,.lib-mem [role=switch]').length, txt:d.querySelector('.lib-mem').innerText, sw:document.documentElement.scrollWidth, iw:window.innerWidth, over:[...document.querySelectorAll('#gl *')].filter(function(e){var r=e.getBoundingClientRect();return r.width>0&&(r.right>window.innerWidth+1||r.left<-1);}).length, nm:d.querySelector('.gf-name').getBoundingClientRect().width, del:d.querySelector('[data-forget]').getBoundingClientRect().height};})()")
        check("23c %dpx: opening it lists the 3 members read only (format, size, short id), no per-member switch / input" % w, m["open"] and m["li"] == 3 and m["msw"] == 0 and "512" in m["txt"] and "id " in m["txt"], m)
        check("23d %dpx: no horizontal overflow, name field and Delete usable (>= 40 px)" % w, m["sw"] <= m["iw"] and m["over"] == 0 and m["nm"] >= 120 and m["del"] >= 40, m)
        pg.click("#gl details.lib-set[data-set=sset3] .lib-sw"); pg.wait_for_timeout(300)
        r = rules(pg)
        check("23e %dpx: the one switch (a real tap) turns ALL 3 members off together and does not close the container" % w, not any(ck(k) in r for k in S3) and pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3]').open") and "normal" in pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] summary').innerText"), (r, pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] summary').innerText")))
        pg.click("#gl details.lib-set[data-set=sset3] .lib-sw"); pg.wait_for_timeout(300)
        r = rules(pg)
        check("23f %dpx: tapping it again hides all 3 again; the other textures are untouched" % w, all(ck(k) in r for k in S3) and len(r) == 3, r)
        check("23g %dpx: no JS errors" % w, not er, er)
        pg.close()

    # 23h: Short mode (the count and state stay visible)
    for w in (320, 411):
        pg, er = lib_page(w, ROWS, S3, "localStorage.setItem('ct_ui_guide', 'short')")
        m = pg.evaluate("(function(){var s=document.querySelector('#gl details.lib-set[data-set=sset3] summary'), st=s.querySelector('.lib-st');return {short:document.documentElement.classList.contains('short'), vis:st.offsetHeight>0&&getComputedStyle(st).display!=='none', t:st.innerText, sw:document.documentElement.scrollWidth, iw:window.innerWidth};})()")
        check("23h %dpx Short mode: '3 textures · hidden' is still visible, no overflow" % w, m["short"] and m["vis"] and "3 textures" in m["t"] and m["sw"] <= m["iw"], m)
        pg.close()

    pg, er = lib_page(360, ROWS, S3)
    # 23i rename + group of the whole set write every member line
    tap(pg, "#gl details.lib-set[data-set=sset3] summary")
    pg.fill("#gl details.lib-set[data-set=sset3] [data-name]", "Brown plants"); pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] [data-name]').dispatchEvent(new Event('change',{bubbles:true}))"); pg.wait_for_timeout(300)
    ls = lib_sets(pg)
    check("23i rename writes all 3 members (lib and texlib.txt), still one container", all(x[0] == "Brown plants" for x in ls["sset3"]) and len(ls["sset3"]) == 3 and sum(1 for l in tfile(pg) if "Brown plants" in l) == 3 and nsets(pg) == 2, (ls, tfile(pg)))
    pg.evaluate("(function(){var s=document.querySelector('#gl details.lib-set[data-set=sset3] [data-grpsel]');s.value='effects';s.dispatchEvent(new Event('change',{bubbles:true}));})()"); pg.wait_for_timeout(300)
    ls = lib_sets(pg)
    check("23j moving the group moves all 3 members (lib and texlib.txt), it shows once", all(x[1] == "effects" for x in ls["sset3"]) and sum(1 for l in tfile(pg) if "effects~sset3|Brown plants" in l) == 3 and nsets(pg) == 2 and pg.evaluate("document.querySelectorAll('#gl details.lib-set[data-set=sset3]').length") == 1, (ls, tfile(pg)))
    check("23k the file format is unchanged (fmt w h hash group~setid|name)", all(re.match(r"^\d+ \d+ \d+ [0-9a-f]{16} (plants|effects|other)(~s[a-z0-9]+)?\|[A-Za-z0-9 _.\-]+$", l) for l in tfile(pg)), tfile(pg))
    # 23l partly hidden
    pg.evaluate("window.__ct.lib()")  # no-op
    pg.close()
    pg, er = lib_page(360, ROWS, S3[:2])
    t = pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] summary').innerText")
    check("23l 2 of 3 hidden: state says partly hidden, the switch is off", "partly hidden" in t and pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] .lib-sw').getAttribute('aria-checked')") == "false", t)
    tap(pg, "#gl details.lib-set[data-set=sset3] summary")
    check("23m opened: each member says hidden / normal", pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] .lib-mem').innerText").count("hidden") == 2 and pg.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3] .lib-mem').innerText").count("normal") == 1)
    tap(pg, "#gl details.lib-set[data-set=sset3] .lib-sw"); pg.wait_for_timeout(250)
    check("23n one tap on a partly hidden set hides ALL 3", len([x for x in rules(pg) if x in [ck(k) for k in S3]]) == 3, rules(pg))
    # 23o Delete set
    tap(pg, "#gl details.lib-set[data-set=sset3] [data-forget]"); pg.wait_for_timeout(300)
    check("23o Delete removes all 3 members, the rules and the texlib.txt lines (the other set and the single stay)", "sset3" not in lib_sets(pg) and len(lib_sets(pg).get("sset2", [])) == 2 and len(tfile(pg)) == 3 and not any(ck(k) in rules(pg) for k in S3), (lib_sets(pg), tfile(pg)))
    check("23p no JS errors (rename / group / partly / delete)", not er, er)
    pg.close()

    # 23q bulk delete: a set goes only when ALL its members are OFF
    pg, er = lib_page(360, ROWS, [S3[0]])          # set3 is partly hidden (1 of 3), set2 and the single are OFF
    b1 = pg.evaluate("document.querySelector('[data-libdel=off]').textContent")
    check("23q the bulk button counts units: 2 (set2 + the single); the partly hidden set is not counted", "(2)" in b1, b1)
    tap(pg, "[data-libdel=off]"); tap(pg, "[data-libdel=off]"); pg.wait_for_timeout(300)
    ls = lib_sets(pg)
    check("23r after 'Delete all that are OFF' the partly hidden set still has ALL 3 members, set2 and the single are gone", len(ls.get("sset3", [])) == 3 and "sset2" not in ls and len(ls) == 1, ls)
    check("23s texlib.txt matches (3 lines)", len(tfile(pg)) == 3, tfile(pg))
    pg.close()
    pg, er = lib_page(360, ROWS, [])               # nothing hidden: both sets and the single go as whole units
    tap(pg, "[data-libdel=off]"); tap(pg, "[data-libdel=off]"); pg.wait_for_timeout(300)
    check("23t a set that is OFF in all members is deleted as a whole (no member is left alone)", len(lib_sets(pg)) == 0 and len(tfile(pg)) == 0, (lib_sets(pg), tfile(pg)))
    pg.close()
    pg, er = lib_page(360, ROWS, S3)               # set3 fully ON stays
    tap(pg, "[data-libdel=off]"); tap(pg, "[data-libdel=off]"); pg.wait_for_timeout(300)
    check("23u a set that is ON is never touched by the bulk delete", len(lib_sets(pg).get("sset3", [])) == 3 and nsets(pg) == 1, lib_sets(pg))
    check("23v no JS errors (bulk)", not er, er)
    pg.close()

    # 23w a set can never show twice or split when the file is odd (different group / name on its lines, members apart, one lonely member)
    odd = [ck(S3[0]) + " plants~sset3|Green grass", ck(ONE) + " effects|Smoke", ck(S3[1]) + " effects~sset3|Other name", ck(S3[2]) + " other~sset3|Third", ck(T2[0]) + " other~slone|Lonely"]
    pg, er = lib_page(360, odd, S3)
    ls = lib_sets(pg)
    check("23w lines of one set with different names / groups are read as ONE set (name and group of the first line)", ls.get("sset3") == [["Green grass", "plants"]] * 3, ls)
    check("23x it shows ONCE (one container, in Plants)", nsets(pg) == 1 and pg.evaluate("document.querySelectorAll('#gl details.gf-grp[data-grp=plants] details.lib-set[data-set=sset3]').length") == 1, nsets(pg))
    check("23y a set id with one member left is a normal single texture ('1 textures together' never shows)", "slone" not in ls and "textures together" not in pg.inner_text("#gl") and "1 textures" not in pg.inner_text("#gl") and pg.evaluate("document.querySelectorAll('#gl [data-name]').length") == 3, ls)
    check("23z no JS errors (odd file)", not er, er)
    pg.close()

    # 23aa the copy in this browser (ct_lib2) repairs lines that lost 'group|name': the set comes back as ONE set
    bk = json.dumps([{"key": k, "name": "Green grass", "grp": "plants", "set": "sset3"} for k in S3])
    dam = [ck(k) for k in S3] + [ck(ONE) + " effects|Smoke"]
    pg, er = lib_page(360, dam, S3, "localStorage.setItem('ct_lib2', %s)" % json.dumps(bk))
    ls = lib_sets(pg)
    check("23aa damaged lines + ct_lib2 copy: the 3 are ONE set again (one container, name and group back)", ls.get("sset3") == [["Green grass", "plants"]] * 3 and nsets(pg) == 1, ls)
    check("23ab ...and the copy is rewritten as it is now", len(json.loads(pg.evaluate("localStorage.getItem('ct_lib2')"))) == 4)
    pg.close()
    bk2 = json.dumps([{"key": S3[0], "name": "Green grass", "grp": "plants", "set": "sset3"}, {"key": S3[1], "name": "Green grass", "grp": "plants", "set": "sset3"}])
    pg, er = lib_page(360, [ck(S3[0]) + " plants~sset3|Green grass", ck(S3[1]), ck(S3[2]) + " plants|Third"], S3, "localStorage.setItem('ct_lib2', %s)" % json.dumps(bk2))
    ls = lib_sets(pg)
    check("23ac a repaired member joins its set; a texture that was alone stays alone", len(ls.get("sset3", [])) == 2 and len(ls.get("-", [])) == 1 and nsets(pg) == 1, ls)
    pg.close()

    # 23ad older texlib.txt formats load exactly as before
    old_rows = [ck(CK[0]) + " Grass 1", ck(CK[1]) + " Smoke old", ck(CK[2]) + " plants|Named", ck(CK[3]) + " effects~sab|Pair", ck(CK[4]) + " effects~sab|Pair"]
    pg, er = lib_page(360, old_rows, [])
    ls = pg.evaluate("window.__ct.lib().map(function(e){return [e.name,e.grp,e.set];})")
    check("23ad v0.48 lines (no group) / v0.49 lines / set lines load as before", ls == [["Grass 1", "plants", ""], ["Smoke old", "other", ""], ["Named", "plants", ""], ["Pair", "effects", "sab"], ["Pair", "effects", "sab"]], ls)
    check("23ae the old lines make 3 rows and 1 container", nsets(pg) == 1 and pg.evaluate("document.querySelectorAll('#gl .item.lib').length") == 4, pg.evaluate("document.querySelectorAll('#gl .item.lib').length"))
    pg.close()

    # 23af Save as ONE set from the Texture browser -> one container -> survives a reload
    pg = b.new_page(viewport={"width": 360, "height": 900}); er = []
    pg.on("pageerror", lambda e: er.append(str(e)))
    open_adv_q = open_adv(pg, "?demo=300"); er = open_adv_q
    tb_pick(pg, S3)
    pg.evaluate("document.getElementById('tbSaveD').open = true"); pg.fill("#tbSelName", "Test set"); pg.evaluate("document.getElementById('tbSelName').dispatchEvent(new Event('change',{bubbles:true}))")
    tap(pg, "#tbbSaveS"); pg.wait_for_timeout(300)
    ls = lib_sets(pg); sid = [s for s in ls if s != "-"]
    check("23af Save as ONE set: one set id for all 3, same name and group", len(sid) == 1 and len(ls[sid[0]]) == 3 and ls[sid[0]][0][0] == "Test set" and len(set(map(tuple, ls[sid[0]]))) == 1, ls)
    pg.evaluate("document.getElementById('tbOv') && document.querySelector('#tbOv .sh-x') && document.querySelector('#tbOv .sh-x').click()"); pg.wait_for_timeout(300)
    check("23ag My textures shows ONE container with 3 textures and one switch", nsets(pg) == 1 and "3 textures" in pg.inner_text("#gl") and pg.evaluate("document.querySelectorAll('#gl details.lib-set [data-toggle]').length") == 1)
    tl = tfile(pg)
    check("23ah texlib.txt has 3 lines with the same ~id", len(tl) == 3 and len(set(re.search(r"~(s[a-z0-9]+)\|", l).group(1) for l in tl)) == 1, tl)
    # (a) one by one with textures that are already saved: they stay as they are, nothing is duplicated
    pg2, er2 = lib_page(360, tl, [])
    check("23ai after a reload (texlib.txt read back) it is still ONE container", nsets(pg2) == 1 and pg2.evaluate("document.querySelectorAll('#gl .item.lib').length") == 1, nsets(pg2))
    pg2.close()
    check("23aj no JS errors (browser save)", not er, er)
    pg.close()

    # 23ak a key that is already saved ALONE joins the new set (named in the note); a key that is in ANOTHER set blocks the save
    pg = b.new_page(viewport={"width": 360, "height": 900}); er = open_adv(pg, "?demo=300&texlib=" + tlines(ck(S3[0]) + " effects|Smoke", ck(T2[0]) + " other~sset2|Bushes", ck(T2[1]) + " other~sset2|Bushes"))
    tb_pick(pg, S3)
    pg.evaluate("document.getElementById('tbSaveD').open = true"); tap(pg, "#tbbSaveS"); pg.wait_for_timeout(300)
    ls = lib_sets(pg); sid = [s for s in ls if s not in ("-", "sset2")]
    check("23ak a texture saved alone before joins the new set: 3 members, no duplicate entry for it", len(sid) == 1 and len(ls[sid[0]]) == 3 and "-" not in ls and len(pg.evaluate("window.__ct.lib()")) == 5, ls)
    check("23al the note says so", "saved alone before" in toast_txt(pg), toast_txt(pg))
    tap(pg, "#tbbClr")
    tb_pick(pg, (S3[0], T2[0], CK[7]))
    n0 = len(tfile(pg)); lj = pg.evaluate("JSON.stringify(window.__ct.lib())")
    tap(pg, "#tbbSaveS"); pg.wait_for_timeout(300)
    check("23am a key that is a member of ANOTHER set: the save is refused with the reason, nothing changes (lib and texlib.txt)", "already in the set" in pg.inner_text("#tbMsg") and pg.evaluate("JSON.stringify(window.__ct.lib())") == lj and len(tfile(pg)) == n0, (pg.inner_text("#tbMsg"), len(tfile(pg))))
    tap(pg, "#tbbSaveE"); pg.wait_for_timeout(300)
    check("23an 'one by one' with saved ones: they are not changed, moved or duplicated; only the new one is added (6 entries) and the note says the others stay", len(pg.evaluate("window.__ct.lib()")) == 6 and "stay" in toast_txt(pg) and pg.evaluate("window.__ct.lib().filter(function(e){return e.key===%s;}).length" % json.dumps(S3[0])) == 1, (len(pg.evaluate("window.__ct.lib()")), toast_txt(pg)))
    ls = lib_sets(pg)
    check("23ao the sets are intact after 'one by one' (no member moved out)", len(ls[sid[0]]) == 3 and len(ls["sset2"]) == 2, ls)
    check("23ap no JS errors (already saved)", not er, er)
    pg.close()

    # 23aq Keep all (the finder's 'needs N together') goes through the same rules
    def pend_page(rows, grass, U):
        st = json.dumps({"v": 2, "U": U, "keep": [], "name": "Pend set", "grp": "plants", "cp": {"a": 0, "b": len(U), "ta": 0, "tb": len(U), "phase": "check", "hist": []}})
        pg = b.new_page(viewport={"width": 360, "height": 900}); er = open_adv(pg, "?demo=300&texlib=" + tlines(*rows) + "&grass=" + ";".join(ck(k) for k in grass).replace(" ", "%20"), "localStorage.setItem('ct_gset', %s)" % json.dumps(st))
        return pg, er
    pg, er = pend_page([], S3, S3)
    check("23aq the pending set is there (Keep all)", has(pg, "#gfKeepSet"))
    tap(pg, "#gfKeepSet"); pg.wait_for_timeout(300)
    ls = lib_sets(pg); sid = [s for s in ls if s != "-"]
    check("23ar Keep all: ONE set of 3 in My textures, one container", len(sid) == 1 and len(ls[sid[0]]) == 3 and nsets(pg) == 1 and ls[sid[0]][0] == ["Pend set", "plants"], ls)
    check("23as the checkpoint points at the set id (Return is cleared when the set is deleted)", (cpj(pg) or {}).get("found") == sid[0], cpj(pg))
    tap(pg, "#gl details.lib-set [data-forget]"); pg.wait_for_timeout(300)
    check("23at Delete of that set clears the checkpoint", cpj(pg) is None and len(pg.evaluate("window.__ct.lib()")) == 0)
    check("23au no JS errors (Keep all)", not er, er)
    pg.close()
    pg, er = pend_page([ck(S3[0]) + " other~sset2|Bushes", ck(CK[8]) + " other~sset2|Bushes"], S3, S3)
    lj = pg.evaluate("JSON.stringify(window.__ct.lib())")
    tap(pg, "#gfKeepSet"); pg.wait_for_timeout(300)
    check("23av Keep all when one texture is in ANOTHER set: refused with the reason, the pending set and the library stay as they are", has(pg, "#gfKeepSet") and "already in the set" in toast_txt(pg) and pg.evaluate("JSON.stringify(window.__ct.lib())") == lj and pg.evaluate("localStorage.getItem('ct_gset')") is not None, toast_txt(pg))
    pg.close()
    pg, er = pend_page([ck(S3[0]) + " effects|Smoke"], S3, S3)
    tap(pg, "#gfKeepSet"); pg.wait_for_timeout(300)
    ls = lib_sets(pg)
    check("23aw Keep all when one is saved alone: it joins the set, 3 members, no duplicate", len([s for s in ls if s != "-"]) == 1 and len(pg.evaluate("window.__ct.lib()")) == 3 and "-" not in ls, ls)
    pg.close()

    # 23ax a set member found again by the finder: it stays in its set (nothing duplicated, nothing moved, the set is not split)
    st = json.dumps({"v": 2, "keep": [], "mode": "search", "U": [S3[1]], "a": 0, "b": 1, "ta": 0, "tb": 1, "phase": "check", "hist": [], "name": "Again", "grp": "other", "sub": None, "paused": False})
    pg = b.new_page(viewport={"width": 360, "height": 900}); er = open_adv(pg, "?demo=300&texlib=" + tlines(*ROWS) + "&grass=" + ck(S3[1]).replace(" ", "%20"), "localStorage.setItem('ct_gst2', %s)" % json.dumps(st))
    lj = pg.evaluate("JSON.stringify(window.__ct.lib())")
    tap(pg, "#gfYes"); pg.wait_for_timeout(300)
    check("23ax found again: the library is exactly as before (still one set of 3), one container", pg.evaluate("JSON.stringify(window.__ct.lib())") == lj and nsets(pg) == 2 and not has(pg, "#gfYes"), (nsets(pg), has(pg, "#gfYes")))
    check("23ay no JS errors (found again)", not er, er)
    pg.close()

    # 24. v0.60.1 Part 6A: compact My textures (single row, set container); 6B adds the test window and the Texture browser
    ROWS24 = ROWS + [ck(k) + " plants|Extra %d" % i for i, k in enumerate(CK[6:12])]
    def geo24(pg):
        return pg.evaluate("""(function(){
          function h(e){return e?e.getBoundingClientRect().height:0}
          var s=document.querySelector('#gl .item.lib:not(.lib-set)'), d=document.querySelector('#gl details.lib-set');
          var cells=s.querySelectorAll('.sw,.gf-name,select,[data-forget]'), d2=d.querySelectorAll('summary .lib-sw');
          var mn=999; [].slice.call(cells).concat([].slice.call(d2)).forEach(function(e){mn=Math.min(mn,h(e))});
          var vis=0, y=0; [].slice.call(document.querySelectorAll('#gl .item.lib')).forEach(function(e){});
          return {single:h(s), set:h(d), minTarget:mn, gap:parseFloat(getComputedStyle(s.parentNode).rowGap)||0,
                  over:document.documentElement.scrollWidth-innerWidth, minFont:Math.min.apply(null,[].slice.call(document.querySelectorAll('#gl .item.lib .t,#gl .item.lib .d,#gl .lib-st,#gl .gf-name')).map(function(e){return parseFloat(getComputedStyle(e).fontSize)}))}})()""")
    for w in (320, 360, 390, 411):
        pg, er = lib_page(w, ROWS24, S3)
        g = geo24(pg)
        MAXS = 126 if w <= 320 else 114   # at 320 px the format / size line wraps to 3 lines
        check("24a %dpx: a single My textures row is <= %d px (was 142 at 360), a closed set container <= 58 px (was 67)" % (w, MAXS), g["single"] <= MAXS and g["set"] <= 58, g)
        check("24b %dpx: more rows fit one 800 px screen than before (>= %.1f single rows, was 5.3)" % (w, 6.0 if w <= 320 else 6.5), 800 / (g["single"] + g["gap"]) >= (6.0 if w <= 320 else 6.5), g)
        check("24c %dpx: switch, name field, group and Delete (and the set switch) are >= 40 px high, fonts >= 12 px" % w, g["minTarget"] >= 40 and g["minFont"] >= 12, g)
        check("24d %dpx: no horizontal overflow; 24e no JS errors" % w, g["over"] <= 0 and not er, (g, er))
        pg.close()
    for cls in ("cc", "cc md", ""):
        pg, er = lib_page(360, ROWS24, S3)
        pg.evaluate("c=>{document.documentElement.className=document.documentElement.className.replace(/\\bcc\\b|\\bmd\\b/g,'').trim()+' '+c}", cls); pg.wait_for_timeout(250)
        g = geo24(pg)
        check("24f size '%s': targets >= 40 px, no overflow, row <= 114 px" % (cls or "none"), g["minTarget"] >= 40 and g["over"] <= 0 and g["single"] <= 114, g)
        pg.close()
    pg, er = lib_page(320, ROWS24, S3, "localStorage.setItem('ct_ui_guide', 'short')")
    g = geo24(pg)
    check("24g Short mode (320 px): still compact, '3 textures' and the state visible, no overflow", g["single"] <= 126 and g["over"] <= 0 and pg.evaluate("document.querySelector('#gl details.lib-set .lib-st').offsetHeight>0"), g)
    pg.close()
    pg, er = lib_page(360, ROWS24, S3, None, 600)
    pg.evaluate("window.scrollTo(0, document.querySelector('#gl .item.lib:not(.lib-set)').getBoundingClientRect().top + scrollY - 80)"); pg.wait_for_timeout(250)
    y0 = pg.evaluate("scrollY"); r0 = pg.evaluate("document.querySelector('#gl .item.lib:not(.lib-set) .sw').getBoundingClientRect().top")
    pg.click("#gl .item.lib:not(.lib-set) .sw"); pg.wait_for_timeout(300)
    check("24h tapping a single-row switch does not move the page or the row (scroll anchoring kept)", abs(pg.evaluate("scrollY") - y0) <= 1 and abs(pg.evaluate("document.querySelector('#gl .item.lib:not(.lib-set) .sw').getBoundingClientRect().top") - r0) <= 1, (y0, pg.evaluate("scrollY")))
    pg.click("#gl details.lib-set .lib-sw"); pg.wait_for_timeout(300)
    check("24i the set switch still turns all members off/on and keeps the container closed; no JS errors", pg.evaluate("document.querySelector('#gl details.lib-set').open") is False and not er, er)
    pg.close()

    # 24j-24r. v0.60.2 Part 6B: compact Texture browser rows (.tb-row) and test window rows (.tw-row). Real px: these windows are not inside .view (no zoom).
    HITJS = """(function(sel,what){
      var r=document.querySelector(sel), t=r.querySelector(what), q=t.getBoundingClientRect(), cx=q.left+q.width/2, cy=q.top+q.height/2, d=19, ok=true;
      [[d,0],[-d,0],[0,d],[0,-d]].forEach(function(o){var e=document.elementFromPoint(cx+o[0],cy+o[1]); if(!(e===t||t.contains(e))) ok=false});
      return ok})"""
    GEO6B = """(function(sel){
      var rows=[].slice.call(document.querySelectorAll(sel)), r=rows[0]; if(!r) return null;
      var hs=rows.slice(0,8).map(function(e){return e.getBoundingClientRect().height});
      var fs=[].slice.call(r.querySelectorAll('b,small,small span,em')).filter(function(e){return e.offsetHeight>0}).map(function(e){return parseFloat(getComputedStyle(e).fontSize)});
      var st=r.querySelector('.tb-st'); if(st&&st.offsetHeight>0) fs.push(parseFloat(getComputedStyle(st).fontSize));
      var gap=parseFloat(getComputedStyle(r.parentNode).rowGap)||0, sh=r.closest('.sh').getBoundingClientRect().height;
      return {max:Math.max.apply(null,hs), gap:gap, minFont:Math.min.apply(null,fs), over:document.documentElement.scrollWidth-innerWidth, sh:sh}})"""
    def open6b(w, win, init=None, h=800):
        pg = b.new_page(viewport={"width": w, "height": h}); er = []
        pg.on("pageerror", lambda e: er.append(str(e)))
        if init: pg.add_init_script(init)
        pg.goto(PAGE + "?demo=300"); pg.wait_for_timeout(800)
        pg.click("#dock [data-v=adv]"); pg.wait_for_timeout(300)
        pg.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); pg.wait_for_timeout(200)
        pg.evaluate("document.getElementById('tbOpen').click()"); pg.wait_for_timeout(400)
        if win == "tw":
            pg.evaluate("document.getElementById('tbbSelF').click()"); pg.wait_for_timeout(200)
            pg.evaluate("document.getElementById('tbbTestS').click()"); pg.wait_for_timeout(600)
            pg.evaluate("document.getElementById('twOpen').click()"); pg.wait_for_timeout(500)
        return pg, er
    ROW = {"tb": "#tbList .tb-row", "tw": "#twOv .tw-row"}
    CKS = {"tb": ".tw-ck", "tw": ".tw-ck"}; SWS = {"tb": ".tb-sw", "tw": ".tb-sw"}
    for w in (320, 360, 390, 411):
        for win in ("tb", "tw"):
            pg, er = open6b(w, win)
            g = pg.evaluate(GEO6B + "('%s')" % ROW[win])
            lim = 80 if win == "tb" else (76 if w <= 320 else 60)   # 320 px, before: browser 132, test window 71 (the test window grew 1 px there because its fonts went from 10-11 px to 12 px)
            check("24j %dpx %s: every row <= %d px (before at 360: browser 96, test window 57-71; at 320: 132 / 71)" % (w, win, lim), g["max"] <= lim, g)
            check("24k %dpx %s: fonts >= 12 px (name, format / size, mips, id, hint, status), no horizontal overflow" % (w, win), g["minFont"] >= 12 and g["over"] <= 0, g)
            pg.evaluate("document.getElementById('toast') && (document.getElementById('toast').style.display='none')"); pg.evaluate("s=>document.querySelector(s).scrollIntoView({block:'center'})", ROW[win]); pg.wait_for_timeout(200)
            ok = pg.evaluate(HITJS + "('%s','%s')" % (ROW[win], CKS[win])) and pg.evaluate(HITJS + "('%s','%s')" % (ROW[win], SWS[win]))
            check("24l %dpx %s: checkbox and switch hit area >= 38 px around the centre (a 19 px reach in each direction from the centre)" % (w, win), ok)
            check("24m %dpx %s: no JS errors" % (w, win), not er, er)
            pg.close()
    # more rows visible than before (360 px)
    pg, er = open6b(360, "tb"); g = pg.evaluate(GEO6B + "('#tbList .tb-row')")
    check("24n browser: more rows per window than before (96 px rows: 7.3; now >= 9)", g["sh"] / (g["max"] + g["gap"]) >= 9, g)
    pg.close()
    pg, er = open6b(360, "tw"); g = pg.evaluate(GEO6B + "('#twOv .tw-row')")
    rows_before = 278 / (64 + 5)
    check("24o test window: the list area shows more rows than before (4.0 with 64 px average rows)", 278 / (g["max"] + g["gap"]) > rows_before, g)
    pg.close()
    # three sizes + Short mode (class change on <html>)
    for win in ("tb", "tw"):
        for cls in ("cc", "cc md", ""):
            pg, er = open6b(360, win)
            pg.evaluate("c=>{document.documentElement.className=document.documentElement.className.replace(/\\bcc\\b|\\bmd\\b/g,'').trim()+' '+c}", cls); pg.wait_for_timeout(250)
            g = pg.evaluate(GEO6B + "('%s')" % ROW[win])
            check("24p %s size '%s': rows <= %d px, fonts >= 12, no overflow" % (win, cls or "none", 80 if win == "tb" else 60), g["max"] <= (80 if win == "tb" else 60) and g["minFont"] >= 12 and g["over"] <= 0, g)
            pg.close()
        pg, er = open6b(320, win, "localStorage.setItem('ct_ui_guide', 'short')")
        g = pg.evaluate(GEO6B + "('%s')" % ROW[win])
        check("24q %s Short mode (320 px): compact, nothing cut off, no overflow, no JS errors" % win, g["max"] <= (80 if win == "tb" else 76) and g["over"] <= 0 and not er, (g, er))
        pg.close()
    # scroll + selection do not jump when a checkbox / switch is tapped
    SCR = """(function(sel){var e=document.querySelector(sel);while(e&&!(e.scrollHeight>e.clientHeight+2&&/(auto|scroll)/.test(getComputedStyle(e).overflowY)))e=e.parentElement;return e})"""
    for win in ("tb", "tw"):
        # Audit (2026-10-28): the test window's list is only ~151 px tall at 360 x 800 with the filters open (Part 8 filter pack), so no row can sit
        # 20 px inside both edges after the scroll below and the probe crashed with "reading 'dataset'". A 1100 px tall page gives the list its old room;
        # the assertions below are unchanged. The browser keeps the old 800 px page.
        pg, er = open6b(360, win, None, 1100 if win == "tw" else 800)
        pg.evaluate("(function(){var e=%s('%s');window.__sc=e;document.querySelectorAll('%s')[10].scrollIntoView({block:'start'});e.scrollTop+=40})()" % (SCR, ROW[win], ROW[win])); pg.wait_for_timeout(250)
        s0 = pg.evaluate("window.__sc.scrollTop"); k = pg.evaluate("(function(){var r=[].slice.call(document.querySelectorAll('%s')).filter(function(e){var q=e.getBoundingClientRect();var c=window.__sc.getBoundingClientRect();return q.top>c.top+20&&q.bottom<c.bottom-20})[1];return r.dataset.key||r.dataset.k})()" % ROW[win])
        rsel = '%s[data-%s="%s"]' % (ROW[win], "key" if win == "tb" else "k", k)
        RELY = "(function(){var r=document.querySelector('%s'),c=window.__sc.getBoundingClientRect();return r?r.getBoundingClientRect().top-c.top:-9999})()" % rsel   # the row relative to its scroll area (the text above the list may change height by itself)
        y0 = pg.evaluate(RELY)
        pg.click(rsel + " .tw-ck"); pg.wait_for_timeout(300)
        y1 = pg.evaluate(RELY)
        check("24r %s: ticking a checkbox keeps the row where it is on the screen (the scroll anchoring may move scrollTop by itself when the text above changes, same as before 6B); the tick stays" % win, abs(y1 - y0) <= 1 and pg.evaluate("document.querySelector('%s .tw-ck').getAttribute('aria-checked')" % rsel) == "true", (s0, y0, y1))
        pg.click(rsel + " .tb-sw"); pg.wait_for_timeout(350)
        y2 = pg.evaluate(RELY)
        check("24s %s: tapping the switch does not move the list; the tick (selection) is kept; no JS errors" % win, abs(y2 - y0) <= 1 and pg.evaluate("document.querySelector('%s .tw-ck').getAttribute('aria-checked')" % rsel) == "true" and not er, (y0, y2, er))
        pg.close()

    # 25. v0.61 Part 7 Stage C: Restore original action, persistence, limits and checkpoint safety
    SRC = open(os.path.join(HERE, "..", "webroot", "index.html"), encoding="utf-8").read()
    MOD = open(os.path.join(HERE, "..", "module.prop"), encoding="utf-8").read()
    CPP = open(os.path.join(HERE, "..", "main.cpp"), encoding="utf-8").read()
    HAN = open(os.path.join(HERE, "..", "HANDBOOK.md"), encoding="utf-8").read()
    check("25a current v0.67 / versionCode=89 are consistent, backup includes orig.txt, and action field is documented",
          '#define VERSION "v0.67"' in CPP and "version=v0.67" in MOD and "versionCode=89" in MOD and
          re.search(r'var CFGLIST = "[^\"]*\borig\b[^\"]*";', SRC) and
          'localStorage.setItem("ct_gact", gForm.act)' in SRC and 'if (!/^ct_/.test(k) || UI_SKIP.test(k)) continue;' in SRC and "Action field" in HAN)

    def p25_page(w, texlib="", grass="", orig="", init=None, h=800):
        q = b.new_page(viewport={"width": w, "height": h}); er = []
        q.on("pageerror", lambda e: er.append(str(e)))
        if init: q.add_init_script(init)
        url = PAGE + "?demo=300&texlib=" + quote(texlib.replace("\n", ";"), safe="") + "&grass=" + quote(grass.replace("\n", ";"), safe="") + "&orig=" + quote(orig.replace("\n", ";"), safe="")
        q.goto(url); q.wait_for_timeout(700)
        q.click("#dock [data-v=adv]"); q.wait_for_timeout(250)
        q.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); q.wait_for_timeout(250)
        return q, er

    # v0.48 / v0.49 entries and old set lines default to Hide without rewriting the source file.
    old_rows = [ck(CK[0]) + " Old smoke", ck(CK[1]) + " plants~slegacy|Legacy set", ck(CK[2]) + " plants~slegacy|Legacy set"]
    old_text = "\n".join(old_rows)
    q, er = p25_page(360, old_text)
    old_lib = q.evaluate("window.__ct.lib().map(e=>e.act)")
    check("25b legacy v0.48/v0.49/set lines default to Hide and are not rewritten", old_lib == ["hide"] * 3 and q.evaluate("window.__ct.fakeTexlib()") == old_text, (old_lib, q.evaluate("window.__ct.fakeTexlib()")))
    q.close()

    # Restore action tokens load; a mixed set takes the action of its first line and normalizes once.
    rst_rows = [ck(CK[0]) + " plants~~r|Original", ck(CK[1]) + " plants~srestore1~r|Restored set", ck(CK[2]) + " plants~srestore1~r|Restored set"]
    q, er = p25_page(360, "\n".join(rst_rows))
    acts = q.evaluate("window.__ct.lib().map(e=>e.act)")
    check("25c ~~r single and ~s...~r set lines load as Restore original", acts == ["restore", "restore", "restore"], acts)
    q.close()
    mixed = [ck(CK[0]) + " plants~smixed1|Mixed", ck(CK[1]) + " plants~smixed1~r|Mixed"]
    q, er = p25_page(360, "\n".join(mixed))
    mix = q.evaluate("window.__ct.lib().map(e=>e.act)")
    rewritten = q.evaluate("window.__ct.fakeTexlib()")
    check("25d mixed set normalizes to first line's Hide action", mix == ["hide", "hide"], (mix, rewritten))
    q.close()

    # Change the whole set's action, verify the ON state moves to orig.txt, then test switch / rename / group / delete.
    q, er = lib_page(360, ROWS, S3)
    q.evaluate("document.querySelector('#gl details.lib-set[data-set=sset3]').open=true"); q.wait_for_timeout(100)
    q.select_option('#gl [data-actsel="sset3"]', "restore"); q.wait_for_timeout(300)
    og = q.evaluate("window.__ct.fakeOrig().split('\\n').filter(Boolean).length")
    gr = q.evaluate("window.__ct.fakeGrass().split('\\n').filter(Boolean).length")
    check("25e changing a set to Restore moves its ON keys to orig.txt only", og == 3 and gr == 0 and all(e["act"] == "restore" for e in q.evaluate("window.__ct.lib().filter(e=>e.set==='sset3')")), (og, gr))
    q.click('#gl details.lib-set[data-set="sset3"] .lib-sw'); q.wait_for_timeout(250)
    check("25f one set switch toggles every Restore member OFF", q.evaluate("window.__ct.fakeOrig().trim()") == "")
    q.click('#gl details.lib-set[data-set="sset3"] .lib-sw'); q.wait_for_timeout(250)
    check("25g one set switch toggles every Restore member ON", len(q.evaluate("window.__ct.fakeOrig().split('\\n').filter(Boolean)")) == 3)
    q.fill('#gl [data-name="sset3"]', "New plants"); q.locator('#gl [data-name="sset3"]').dispatch_event("change"); q.wait_for_timeout(250)
    q.select_option('#gl [data-grpsel="sset3"]', "effects"); q.wait_for_timeout(250)
    meta = q.evaluate("window.__ct.lib().filter(e=>e.set==='sset3').map(e=>[e.name,e.grp])")
    check("25h set rename and group update all members together", len(meta) == 3 and all(x == ["New plants", "effects"] for x in meta), meta)
    q.click('#gl details.lib-set[data-set="sset3"] [data-forget="sset3"]'); q.wait_for_timeout(300)
    check("25i deleting a Restore set removes all its keys from orig.txt", len(q.evaluate("window.__ct.lib()")) == 3 and q.evaluate("window.__ct.fakeOrig().trim()") == "", (q.evaluate("window.__ct.lib()"), q.evaluate("window.__ct.fakeOrig()")))
    check("25j set operations have no JS errors", not er, er)
    q.close()

    # Cancel must restore the pre-edit maps for a Restore entry without turning it into Hide.
    restore_row = ck(CK[0]) + " plants~~r|Original"
    q, er = p25_page(360, restore_row, "", "")
    q.click('#gl .item.lib:not(.lib-set) [data-toggle]'); q.wait_for_timeout(250)
    before_cancel = q.evaluate("({g:window.__ct.fakeGrass(),o:window.__ct.fakeOrig(),a:window.__ct.lib()[0].act})")
    q.evaluate("document.getElementById('cancelChg').click()"); q.wait_for_timeout(350)
    after_cancel = q.evaluate("({g:window.__ct.fakeGrass(),o:window.__ct.fakeOrig(),a:window.__ct.lib()[0].act})")
    check("25k Cancel restores baseline maps and keeps Hide/Restore action distinct", before_cancel["a"] == "restore" and before_cancel["o"].strip() != "" and after_cancel["o"].strip() == "" and after_cancel["g"].strip() == "" and after_cancel["a"] == "restore", (before_cancel, after_cancel))
    q.close()

    # The 4097th orig rule is refused before any write; the original 4096 remain intact.
    q, er = p25_page(360)
    big_orig = ["37497|%d|1|%016x" % (i + 1, i + 1) for i in range(4096)]
    ok4096 = q.evaluate("ks=>window.__ct.setOrigRules(ks,'limit test')", big_orig); q.wait_for_timeout(100)
    count4096 = len(q.evaluate("window.__ct.fakeOrig().split('\\n').filter(Boolean)"))
    too_many = big_orig + ["37497|60000|1|000000000000ffff"]
    ok4097 = q.evaluate("ks=>window.__ct.setOrigRules(ks,'limit test')", too_many); q.wait_for_timeout(100)
    count_after = len(q.evaluate("window.__ct.fakeOrig().split('\\n').filter(Boolean)"))
    check("25l orig.txt accepts 4096 rules and refuses the 4097th before writing", ok4096 is True and count4096 == 4096 and ok4097 is False and count_after == 4096 and "Nothing was changed" in q.inner_text("#toast"), (ok4096, count4096, ok4097, count_after, q.inner_text("#toast")))
    check("25m 4096-rule cap test has no JS errors", not er, er)
    q.close()

    # A checkpoint can Return when a separate Restore entry is active, but a checkpoint whose own universe became Restore is stale.
    def checkpoint_for(k):
        return {"v": 1, "t": 1, "why": "cancel", "found": "", "gst": {"v": 2, "U": [k], "keep": [], "a": 0, "b": 1, "ta": 0, "tb": 1, "phase": "check", "hist": [], "name": "", "grp": "other", "sub": None, "paused": False}, "sw": None}
    other_restore = CK[10]
    init_good = "localStorage.setItem('ct_gcp', " + json.dumps(json.dumps(checkpoint_for(CK[0]))) + ");"
    q, er = p25_page(360, ck(other_restore) + " plants~~r|Restore other", "", ck(other_restore), init_good)
    check("25n Return checkpoint survives a separate active Restore entry", q.evaluate("window.__ct.cp() !== null") and has(q, "#gfReturn"), (q.evaluate("window.__ct.cp()"), has(q, "#gfReturn")))
    tap(q, "#gfReturn"); q.wait_for_timeout(250)
    check("25o Return restores the test while preserving orig.txt", ck(CK[0]) in q.evaluate("window.__ct.fakeGrass()") and ck(other_restore) in q.evaluate("window.__ct.fakeOrig()") and q.evaluate("localStorage.getItem('ct_gcp')") is None, (q.evaluate("window.__ct.fakeGrass()"), q.evaluate("window.__ct.fakeOrig()")))
    q.close()
    init_stale = "localStorage.setItem('ct_gcp', " + json.dumps(json.dumps(checkpoint_for(CK[0]))) + ");"
    q, er = p25_page(360, ck(CK[0]) + " plants~~r|Now Restore", "", ck(CK[0]), init_stale)
    check("25p a checkpoint containing a key that became Restore is dropped silently", q.evaluate("window.__ct.cp() === null") and q.evaluate("localStorage.getItem('ct_gcp')") is None and not has(q, "#gfReturn") and not er, (q.evaluate("window.__ct.cp()"), q.evaluate("localStorage.getItem('ct_gcp')"), er))
    q.close()

    # Check action choice survives the page reload when the saved library file is carried forward.
    q, er = p25_page(360, ck(CK[0]) + " plants|One")
    q.select_option('#gl [data-actsel="' + CK[0] + '"]', "restore"); q.wait_for_timeout(250)
    persisted_texlib = q.evaluate("window.__ct.fakeTexlib()")
    q.goto(PAGE + "?demo=300&texlib=" + quote(persisted_texlib.replace("\n", ";"), safe="")); q.wait_for_timeout(700)
    check("25q Hide vs Restore stays distinct after reload when the saved library is reloaded", q.evaluate("window.__ct.lib().length") == 1 and q.evaluate("window.__ct.lib()[0].act") == "restore" and "~~r|" in q.evaluate("window.__ct.fakeTexlib()"), q.evaluate("window.__ct.fakeTexlib()"))
    q.close()

    # Action preference uses ct_gact and is included in the ct_* page-settings backup pack; orig.txt is in the settings backup list.
    q, er = p25_page(360)
    q.evaluate("var e=document.getElementById('gfAct'); if(e){e.value='restore';e.dispatchEvent(new Event('change',{bubbles:true}));}"); q.wait_for_timeout(150)
    action_pref = q.evaluate("localStorage.getItem('ct_gact')")
    check("25r choosing Restore original in the finder chooser persists ct_gact", action_pref == "restore", action_pref)
    check("25s no JS errors for action preference", not er, er)
    q.close()

    # Single Restore row remains compact at phone widths; all action screens have no JS errors.
    for w in (320, 360, 390, 411):
        q, er = p25_page(w, restore_row)
        row_h = q.evaluate("document.querySelector('#gl .item.lib:not(.lib-set)').getBoundingClientRect().height")
        check("25t %dpx: single Restore row is 108 px high" % w, abs(row_h - 108) <= 1, row_h)
        check("25u %dpx: Restore UI has no JS errors or horizontal overflow" % w, not er and q.evaluate("document.documentElement.scrollWidth <= innerWidth"), (er, q.evaluate("[document.documentElement.scrollWidth,innerWidth]")))
        q.close()

    # 26. v0.62 Part 8: collapsible test-window filters and same-session UI-state persistence
    SRC = open(os.path.join(HERE, "..", "webroot", "index.html"), encoding="utf-8").read()
    MOD = open(os.path.join(HERE, "..", "module.prop"), encoding="utf-8").read()
    CPP = open(os.path.join(HERE, "..", "main.cpp"), encoding="utf-8").read()
    HAN = open(os.path.join(HERE, "..", "HANDBOOK.md"), encoding="utf-8").read()
    check("26a v0.67 / versionCode 89 are consistent and filter collapse is documented",
          '#define VERSION "v0.67"' in CPP and "version=v0.67" in MOD and "versionCode=89" in MOD and
          'id="twFilterPack"' in SRC and 'data-tw="filters"' in SRC and "Test-window collapsible filters" in HAN)

    def p26_test(w):
        q = b.new_page(viewport={"width": w, "height": 800}); er = []
        q.on("pageerror", lambda e: er.append(str(e)))
        q.goto(PAGE + "?demo=300"); q.wait_for_timeout(600)
        q.click("#dock [data-v=adv]"); q.wait_for_timeout(200)
        q.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); q.wait_for_timeout(150)
        # Open Texture browser and start a test from the available candidate rows.
        q.evaluate("document.getElementById('tbOpen').click()"); q.wait_for_timeout(250)
        if q.locator("#tbList .tb-row").count() == 0:
            q.close(); return None, er
        q.evaluate("document.querySelector('#tbList .tb-row .tw-ck, #tbList .tb-row [role=checkbox]').click()")
        q.wait_for_timeout(100)
        if q.locator("#tbbTestS").count(): q.evaluate("document.getElementById('tbbTestS').click()")
        q.wait_for_timeout(300)
        if q.locator("#twOpen").count(): q.evaluate("document.getElementById('twOpen').click()")
        q.wait_for_timeout(250)
        if not q.locator("#twOv").count(): q.close(); return None, er
        return q, er

    for w in (320, 360, 390, 411):
        q, er = p26_test(w)
        if q is None:
            check("26b %dpx: test window can be opened from the demo test flow" % w, False, "could not establish a test-window fixture")
            continue
        btn = q.locator('#twOv [data-tw="filters"]')
        pack = q.locator("#twFilterPack")
        first_key = q.locator("#twList .tw-row").first.get_attribute("data-k")
        fmt_value, width_value = first_key.split("|")[0], first_key.split("|")[1]
        q.fill("#twQ", width_value)
        q.select_option("#twFmt", fmt_value)
        q.wait_for_timeout(100)
        if q.locator("#twList .tw-row").count() == 0:
            q.fill("#twQ", "")
            q.select_option("#twFmt", "0")
            q.wait_for_timeout(100)
        q.locator("#twList .tw-row .tw-ck").first.click(); q.wait_for_timeout(80)
        filt = q.evaluate("({q:document.querySelector('#twQ').value,fmt:document.querySelector('#twFmt').value,see:document.querySelector('#twSee').value,sel:[...document.querySelectorAll('#twList .tw-ck[aria-checked=true]')].map(e=>e.getAttribute('data-twck'))})")
        list_before = q.locator("#twList").bounding_box()
        q.click('#twOv [data-tw="filters"]'); q.wait_for_timeout(120)
        list_after = q.locator("#twList").bounding_box()
        hidden_ok = q.locator("#twFilterPack").evaluate("e=>e.hidden") and btn.get_attribute("aria-expanded") == "false" and btn.inner_text() == "Show filters"
        gain = (list_after["height"] - list_before["height"]) if list_before and list_after else -1
        check("26b %dpx: collapsing filters hides the pack accessibly and expands list viewport" % w, hidden_ok and gain >= 30, (hidden_ok, gain, list_before, list_after))
        q.click('#twOv [data-tw="filters"]'); q.wait_for_timeout(100)
        shown_ok = not pack.evaluate("e=>e.hidden") and btn.get_attribute("aria-expanded") == "true"
        q.click('#twOv [data-tw="filters"]'); q.wait_for_timeout(100)
        q.click('#twOv [data-tw="close"]'); q.wait_for_timeout(250)
        q.evaluate("document.getElementById('twOpen').click()"); q.wait_for_timeout(250)
        restored = q.evaluate("({q:document.querySelector('#twQ').value,fmt:document.querySelector('#twFmt').value,see:document.querySelector('#twSee').value,collapsed:document.querySelector('#twFilterPack').hidden,expanded:document.querySelector('#twOv [data-tw=filters]').getAttribute('aria-expanded'),sel:[...document.querySelectorAll('#twList .tw-ck[aria-checked=true]')].map(e=>e.getAttribute('data-twck'))})")
        check("26c %dpx: filters, ticks, and collapsed state survive close/reopen in the same test" % w,
              shown_ok and restored["q"] == filt["q"] and restored["fmt"] == filt["fmt"] and restored["see"] == filt["see"] and restored["sel"] == filt["sel"] and restored["collapsed"] and restored["expanded"] == "false", (shown_ok, filt, restored))
        sizes = q.evaluate("""(()=>{let all=[...document.querySelectorAll('#twOv button,#twOv input,#twOv select')];let els=all.filter(e=>!e.matches('.tw-ck,.tb-sw')&&e.getClientRects().length>0);let text=[...document.querySelectorAll('#twOv *')].filter(e=>e.children.length===0&&e.textContent.trim()&&e.getBoundingClientRect().width>0&&e.getBoundingClientRect().height>0);let px=e=>parseFloat(getComputedStyle(e).fontSize);let hit=[...document.querySelectorAll('#twOv .tw-ck')].filter(e=>e.getClientRects().length>0).map(e=>{let r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2-20,h:r.height+24}});return {minText:Math.min(...text.map(px)),minControl:Math.min(...els.map(e=>Math.min(e.getBoundingClientRect().width,e.getBoundingClientRect().height))),checkboxHitMin:Math.min(...hit.map(x=>x.h)),overflow:document.documentElement.scrollWidth>innerWidth,footerVisible:document.querySelector('#twOv .sh-foot').getBoundingClientRect().bottom<=innerHeight,withMips:[...document.querySelector('#twMips').options].map(o=>o.textContent),buttons:els.map(e=>[e.id||e.getAttribute('data-tw'),e.getBoundingClientRect().width,e.getBoundingClientRect().height]).slice(0,8)}})()""")
        check("26d %dpx: no horizontal overflow, readable text and one With mips choice" % w,
              not sizes["overflow"] and sizes["footerVisible"] and sizes["minText"] >= 12 and sizes["minControl"] >= 40 and sizes["checkboxHitMin"] >= 40 and sizes["withMips"] == ["With mips"], sizes)
        check("26e %dpx: no JavaScript errors" % w, not er, er)
        q.close()

    # 27. v0.64 Part 8: finder guidance (active-filter summary, sort explanation, zero results, LIST INCOMPLETE, selection feedback). Display only.
    SRC27 = open(os.path.join(HERE, "..", "webroot", "index.html"), encoding="utf-8").read()
    MOD27 = open(os.path.join(HERE, "..", "module.prop"), encoding="utf-8").read()
    CPP27 = open(os.path.join(HERE, "..", "main.cpp"), encoding="utf-8").read()
    HAN27 = open(os.path.join(HERE, "..", "HANDBOOK.md"), encoding="utf-8").read()
    check("27a v0.67 / versionCode 89 are consistent, the summary and sort explanation exist and are documented",
          '#define VERSION "v0.67"' in CPP27 and "version=v0.67" in MOD27 and "versionCode=89" in MOD27 and
          'id="tbSum"' in SRC27 and 'id="tbWhy"' in SRC27 and "Finder guidance" in HAN27)

    def p27_open(w, h=800):
        q = b.new_page(viewport={"width": w, "height": h}); er = []
        q.on("pageerror", lambda e: er.append(str(e)))
        q.goto(PAGE + "?demo=300"); q.wait_for_timeout(700)
        q.click("#dock [data-v=adv]"); q.wait_for_timeout(250)
        q.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); q.wait_for_timeout(150)
        q.evaluate("document.getElementById('tbOpen').click()"); q.wait_for_timeout(400)
        return q, er
    KEYS27 = "[...document.querySelectorAll('#tbList .tb-row')].map(e=>e.dataset.key)"
    for w in (320, 360, 390, 411):
        q, er = p27_open(w)
        T = q.evaluate("window.__ct.mvis()")
        sm = q.inner_text("#tbSum")
        check("27b %dpx: no filter active -> summary says only mips are offered, no other filter, all %d candidates listed" % (w, T),
              "Only textures with mips are offered." in sm and "No other filter is active." in sm and ("All %d candidates are listed." % T) in sm and "INCOMPLETE" not in sm, sm)
        geo = q.evaluate("""(function(){var e=document.getElementById('tbSum'),r=e.getBoundingClientRect(),c=document.getElementById('tbSheetBody')||document.body;
          return {fs:parseFloat(getComputedStyle(e).fontSize),right:r.right,vw:document.documentElement.clientWidth,sw:document.documentElement.scrollWidth,vis:r.height>0}})()""")
        check("27c %dpx: summary is visible, >= 12 px and does not overflow the screen" % w, geo["vis"] and geo["fs"] >= 12 and geo["right"] <= geo["vw"] + 1 and geo["sw"] <= geo["vw"] + 1, geo)
        # active filters appear in plain words and the numbers agree with the count line
        q.fill("#tbQ", "z>50"); q.select_option("#tbSee", "some"); q.wait_for_timeout(350)
        fmts = q.evaluate("[...document.querySelectorAll('#tbFmt option')].map(o=>o.value).filter(v=>v!='0')")
        if fmts: q.select_option("#tbFmt", fmts[0]); q.wait_for_timeout(300)
        sm = q.inner_text("#tbSum"); cn = q.inner_text("#tbCount")
        m = re.search(r"Filter: (\d+) of (\d+) candidates", cn)
        F = int(m.group(1)) if m else -1
        check("27d %dpx: summary names the active filters and its numbers match the count line" % w,
              'search "z>50"' in sm and "10% or more see-through" in sm and (not fmts or "format " in sm) and
              ((("No texture matches these filters (%d candidates in total)." % T) in sm) if F == 0 else (("%d of %d candidates match." % (F, T)) in sm or ("All %d candidates are listed." % T) in sm)), (sm, cn))
        # nothing matches: unmistakable
        q.fill("#tbQ", "zzzzqqqq"); q.wait_for_timeout(350)
        sm = q.inner_text("#tbSum"); lst = q.inner_text("#tbList")
        check("27e %dpx: zero results are stated in the summary and in the list, with the candidate total" % w,
              ("No texture matches these filters (%d candidates in total)." % T) in sm and "Nothing matches" in lst and ("%d candidates are in the list" % T) in lst, (sm, lst))
        check("27f %dpx: no JS errors" % w, not er, er)
        q.close()
    # sort explanation: closed by default, changes with the Sort choice, says nothing is hidden for you
    q, er = p27_open(360)
    check("27g the sort explanation is a closed <details> in the page source (the test page opens every <details> on purpose) and there is one entry per sort", '<details class="tb-why" id="tbWhy">' in SRC27 and q.evaluate("[...document.querySelectorAll('#tbSort option')].length") == 6)
    seen = {}
    for sv in ("best", "size", "fmt", "mips", "see", "hash"):
        q.select_option("#tbSort", sv); q.wait_for_timeout(300)
        seen[sv] = q.evaluate("document.getElementById('tbWhyTxt').textContent")
    check("27h every sort has its own explanation, ending with 'nothing is hidden or selected for you'",
          len(set(seen.values())) == 6 and all(t.endswith("nothing is hidden or selected for you.") for t in seen.values()) and "mips come first" in seen["best"] and "fingerprint" in seen["hash"], seen)
    # ranking stability + never auto-hide / auto-select
    q.select_option("#tbSort", "best"); q.wait_for_timeout(300)
    k1 = q.evaluate(KEYS27)
    q.select_option("#tbSort", "size"); q.wait_for_timeout(300); q.select_option("#tbSort", "best"); q.wait_for_timeout(300)
    k2 = q.evaluate(KEYS27)
    q.select_option("#tbSort", "hash"); q.wait_for_timeout(300)
    kh = q.evaluate("[...document.querySelectorAll('#tbList .tb-row')].map(e=>e.querySelector('.tb-fp').textContent.replace(/^id /,'').replace(/ /g,''))")
    check("27i ranking is stable (same order after switching away and back), id sort is sorted, no rule was added and nothing is selected by sorting",
          k1 == k2 and len(k1) > 5 and kh == sorted(kh) and q.evaluate("window.__ct.rules().length") == 0 and "Nothing selected" in q.inner_text("#tbSelTxt"), (k1[:3], k2[:3], kh[:3]))
    # selected vs outside the filter, selection survives filter changes
    q.select_option("#tbSort", "best"); q.wait_for_timeout(250)
    q.evaluate("[...document.querySelectorAll('#tbList .tw-ck')].slice(0,3).forEach(e=>e.click())"); q.wait_for_timeout(200)
    t0 = q.inner_text("#tbSelTxt")
    q.fill("#tbQ", "zzzzqqqq"); q.wait_for_timeout(350)
    t1 = q.inner_text("#tbSelTxt")
    q.fill("#tbQ", ""); q.wait_for_timeout(350)
    t2 = q.inner_text("#tbSelTxt")
    check("27j 3 ticked textures: 'all in the current filter' -> '3 outside it' while nothing matches -> back again after clearing the search",
          t0.startswith("3 selected (all of them") and t1.startswith("3 selected") and "3 outside it" in t1 and t2.startswith("3 selected (all of them"), (t0, t1, t2))
    check("27k no JS errors (sort, ranking, selection)", not er, er)
    q.close()
    # LIST INCOMPLETE: red label first, in the summary and in the count line
    pg2, errs2 = mock_page(1200, failchunk=1)
    sm = pg2.inner_text("#tbSum"); warn = pg2.evaluate("(function(){var e=document.querySelector('#tbSum .tb-warn');return e?{t:e.textContent,c:getComputedStyle(e).color,w:getComputedStyle(e).fontWeight}:null})()")
    check("27l an unreadable chunk shows a bold red LIST INCOMPLETE label at the start of the summary (and the count line keeps its warning)",
          sm.startswith("LIST INCOMPLETE") and "Refresh list" in sm and warn and warn["c"].startswith("rgb(248, 113, 113)") and int(warn["w"]) >= 600 and "INCOMPLETE" in pg2.inner_text("#tbCount"), (sm, warn))
    check("27m no JS errors (incomplete list)", not errs2, errs2)
    pg2.close()

    # 28. v0.65 Part 12: Restore original finder mode. Hide remains the default; restore has its own candidate stream/rules.
    SRC28 = open(os.path.join(HERE, "..", "webroot", "index.html"), encoding="utf-8").read()
    MOD28 = open(os.path.join(HERE, "..", "module.prop"), encoding="utf-8").read()
    CPP28 = open(os.path.join(HERE, "..", "main.cpp"), encoding="utf-8").read()
    HAN28 = open(os.path.join(HERE, "..", "HANDBOOK.md"), encoding="utf-8").read()
    check("28a v0.65 exposes a separate RESTORECAND stream, Restore mode and 4096-rule native cap documentation",
          '#define VERSION "v0.67"' in CPP28 and "version=v0.67" in MOD28 and "versionCode=89" in MOD28 and
          'loadCandText("RESTORECAND")' in SRC28 and 'id="tbTestMode"' in SRC28 and
          'Restore finder (Part 12; v0.65 / versionCode 86)' in HAN28 and 'g_rseen_n < 4096' in CPP28)

    def p28_open(w=360, h=800, extra="", init=None):
        q = b.new_page(viewport={"width": w, "height": h}); er = []
        q.on("pageerror", lambda e: er.append(str(e)))
        if init: q.add_init_script(init)
        open_adv(q, "?demo=0" + extra)
        return q, er

    q, er = p28_open()
    check("28b no saved preference defaults to Hide, while Restore mode offers only mipmapped RESTORECAND candidates",
          q.input_value("#tbTestMode") == "hide" and q.evaluate("window.__ct.vis()") == 3, (q.input_value("#tbTestMode"), q.evaluate("window.__ct.vis()")))
    q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(250)
    vl28 = q.evaluate("window.__ct.visList()")
    check("28c Restore candidate source is distinct: four demo candidates, each mipmapped, including ETC2 RGB and a guarded size",
          len(vl28) == 4 and all(x["lv"] >= 2 for x in vl28) and any(x["fmt"] == 37492 for x in vl28) and any(x["fmt"] == 37497 and x["w"] == 512 and x["h"] == 1024 for x in vl28), vl28)
    tap(q, "#tbbTestF"); tap(q, "#tbbTestF")
    q.wait_for_timeout(250)
    orig4 = q.evaluate("window.__ct.orig().length")
    grass0 = q.evaluate("window.__ct.rules().length")
    yes28 = q.locator("#gfYes").inner_text() if q.locator("#gfYes").count() else ""
    no28 = q.locator("#gfNo").inner_text() if q.locator("#gfNo").count() else ""
    check("28d Test 1 writes four rules to orig.txt only and asks 'problem fixed?' using Restore-specific wording",
          orig4 == 4 and grass0 == 0 and "problem is fixed" in yes28.lower() and "problem is still there" in no28.lower() and not re.search(r"\b(gone|switch off|switched off)\b", yes28 + " " + no28, re.I), (orig4, grass0, yes28, no28))
    tap(q, "#gfYes")
    check("28e Restore 'Yes, problem fixed' narrows to half; Back returns to the previous four-candidate test", q.evaluate("window.__ct.orig().length") == 2)
    tap(q, "#gfBack")
    check("28f Back restores all four original-render rules and never adds Hide rules", q.evaluate("window.__ct.orig().length") == 4 and q.evaluate("window.__ct.rules().length") == 0)
    tap(q, "#twOpen"); q.wait_for_timeout(250)
    action_text = q.locator("#twOv").inner_text() if q.locator("#twOv").count() else ""
    check("28g test window identifies Restore original as the saved action", "Saved action: Restore original." in action_text)
    q.click("#twList .tw-ck"); q.wait_for_timeout(150)
    q.click('#twOv [data-tw="side"]'); q.wait_for_timeout(250)
    check("28h Side test restores only the ticked candidate; 'No' returns to the main test list", q.evaluate("window.__ct.orig().length") == 1 and q.evaluate("window.__ct.rules().length") == 0 and q.locator("#gfSubNo").count() == 1)
    tap(q, "#gfSubNo")
    check("28i Side-test No restores the full main Restore test without changing grass.txt", q.evaluate("window.__ct.orig().length") == 4 and q.evaluate("window.__ct.rules().length") == 0)
    tap(q, "#gfOff")
    check("28j Cancel leaves pre-test Restore rules and creates a Return to last test checkpoint", q.evaluate("window.__ct.orig().length") == 0 and q.locator("#gfReturn").count() == 1)
    tap(q, "#gfReturn")
    check("28k Return to last test restores Restore mode and its four exact rules", q.evaluate("window.__ct.orig().length") == 4 and q.evaluate("window.__ct.rules().length") == 0 and q.locator("#gfYes").count() == 1)
    check("28l Restore finder has no JavaScript errors", not er, er)
    q.close()

    # Completing a one-texture path saves that entry with the Restore action.
    q, er = p28_open()
    q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(150); tap(q, "#tbbTestF"); tap(q, "#tbbTestF")
    tap(q, "#gfYes"); tap(q, "#gfYes"); tap(q, "#gfYes"); q.wait_for_timeout(250)
    lib28 = q.evaluate("window.__ct.lib().map(e=>({key:e.key,act:e.act}))")
    check("28m completing the Restore path saves the found texture in My textures as Restore original, and orig.txt contains it", len(lib28) == 1 and lib28[0]["act"] == "restore" and q.evaluate("window.__ct.orig().length") == 1 and q.evaluate("window.__ct.rules().length") == 0, lib28)
    check("28n found Restore action is written using the legacy-compatible ~r texlib marker", "~~r|" in q.evaluate("window.__ct.fakeTexlib()"))
    check("28o found-asset flow has no JavaScript errors", not er, er)
    q.close()

    # If no single texture fixes the look but restoring the complete candidate set does, Keep all saves a Restore-action set.
    q, er = p28_open()
    q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(150); tap(q, "#tbbTestF"); tap(q, "#tbbTestF")
    tap(q, "#gfYes"); tap(q, "#gfNo"); tap(q, "#gfNo"); tap(q, "#gfNo"); q.wait_for_timeout(250)
    pending = q.locator("#gfKeepSet").count() == 1
    check("28p multi-texture result is held as a pending Restore set, not dropped or converted to Hide", pending and q.evaluate("window.__ct.orig().length") == 4 and q.evaluate("window.__ct.rules().length") == 0)
    tap(q, "#gfKeepSet"); q.wait_for_timeout(250)
    libset = q.evaluate("window.__ct.lib().map(e=>({act:e.act,set:e.set||'',key:e.key}))")
    check("28q Keep all saves one four-member My textures set with action Restore original", len(libset) == 4 and all(e["act"] == "restore" for e in libset) and len(set(e["set"] for e in libset)) == 1 and libset[0]["set"] and q.evaluate("window.__ct.orig().length") == 4 and q.evaluate("window.__ct.rules().length") == 0, libset)
    check("28r Keep-all Restore set flow has no JavaScript errors", not er, er)
    q.close()

    # A candidate already listed in grass.txt must not be copied into orig.txt. Select that exact row, not the ranking-first row.
    conflict = "37492 512 512 aa01000000000001"
    q, er = p28_open(extra="&grass=" + quote(conflict, safe=""))
    q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(200)
    q.evaluate("k=>{let r=[...document.querySelectorAll('#tbList .tb-row')].find(e=>e.dataset.key===k); if(r) r.querySelector('.tw-ck').click();}", "37492|512|512|aa01000000000001"); q.wait_for_timeout(120); tap(q, "#tbbTestS")
    check("28s Restore mode excludes a candidate already in grass.txt instead of writing overlapping rules", q.evaluate("window.__ct.orig().length") == 0 and q.evaluate("window.__ct.rules().length") == 1 and q.locator("#gfYes").count() == 0)
    check("28t conflicting-rule attempt has no JavaScript errors", not er, er)
    q.close()

    # A v0.64 saved test has no mode field; it must reload as Hide, never as Restore.
    legacy_key = "37497|512|512|0a1b2c3d4e5f6071"
    legacy = {"v": 2, "keep": [], "U": [legacy_key], "a": 0, "b": 1, "ta": 0, "tb": 1, "phase": "check", "hist": [], "name": "legacy", "grp": "other", "paused": False}
    init = "localStorage.setItem('ct_gst2', " + json.dumps(json.dumps(legacy)) + ");"
    q, er = p28_open(extra="&grass=" + quote("37497 512 512 0a1b2c3d4e5f6071", safe=""), init=init)
    got_legacy = q.evaluate("JSON.parse(localStorage.getItem('ct_gst2') || 'null')")
    check("28u a saved pre-Part-12 test without mode migrates to Hide/search", bool(got_legacy and got_legacy.get("mode") == "search") and q.input_value("#tbTestMode") == "hide" and q.locator("#gfYes").count() == 1, got_legacy)
    check("28v legacy Hide test has no JavaScript errors", not er, er)
    q.close()

    # Pause/reload: persisted test mode is Restore, and its paused state can be recovered with orig.txt as the source.
    q, er = p28_open()
    q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(150); tap(q, "#tbbTestF"); tap(q, "#tbbTestF"); q.wait_for_timeout(200)
    q.evaluate("document.getElementById('twOpen').click()"); q.wait_for_timeout(200)
    q.click("#twList .tb-sw"); q.wait_for_timeout(250)
    st28 = q.evaluate("JSON.parse(localStorage.getItem('ct_gst2') || 'null')")
    saved_orig = q.evaluate("window.__ct.fakeOrig()")
    check("28aa toggling a candidate pauses the test and persists mode=restore plus the current Restore original rules", bool(st28 and st28.get("mode") == "restore" and st28.get("paused") is True) and len(saved_orig.splitlines()) == 3, st28)
    reload_url = PAGE + "?demo=0&orig=" + quote(saved_orig.replace("\n", ";"), safe="")
    q.goto(reload_url); q.wait_for_timeout(700); q.click("#dock [data-v=adv]"); q.wait_for_timeout(250); q.evaluate("document.querySelectorAll('details').forEach(d=>d.open=true)"); q.wait_for_timeout(200)
    check("28ab after reload, Restore-mode test is recovered as paused, not misread as Hide", q.locator("#gfResume").count() == 1 and "Restore test paused" in q.inner_text("#rsCard") and "Restore test paused" not in q.inner_text("#grassCard") and q.evaluate("JSON.parse(localStorage.getItem('ct_gst2') || 'null').mode") == "restore")
    check("28ac pause/reload has no JavaScript errors", not er, er)
    q.close()

    # Exact rule boundary: 4095 existing Restore rules + one selected candidate is allowed; 4096 + one is refused before writing.
    def orig_keys28(n):
        return ["37492|100|100|" + format(0x1000000000000000 + i, "016x") for i in range(n)]
    for existing, expected in ((4095, 4096), (4096, 4096)):
        q, er = p28_open()
        q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(120)
        ks = orig_keys28(existing)
        q.evaluate("ks=>window.__ct.setOrigRules(ks,'limit test')", ks); q.wait_for_timeout(250)
        q.evaluate("document.getElementById('tbTestMode').dispatchEvent(new Event('change',{bubbles:true}))"); q.wait_for_timeout(200)
        q.click("#tbList .tw-ck"); q.wait_for_timeout(120); tap(q, "#tbbTestS"); q.wait_for_timeout(250)
        count28 = q.evaluate("window.__ct.orig().length")
        started28 = q.locator("#gfYes").count() == 1
        check("28w%d Restore rule cap with %d existing: %d rules, %s" % (existing, existing, count28, "test starts" if existing == 4095 else "4097th rule refused"),
              count28 == expected and (started28 if existing == 4095 else not started28) and (existing != 4096 or "4097 restored textures" in q.inner_text("#toast")), (count28, started28, q.inner_text("#toast")))
        check("28x%d no JavaScript errors at the Restore rule boundary" % existing, not er, er)
        q.close()

    for w in (320, 360, 390, 411):
        q, er = p28_open(w)
        q.select_option("#tbTestMode", "restore"); q.wait_for_timeout(150)
        box = q.evaluate("(function(){var e=document.getElementById('tbTestMode'),r=e.getBoundingClientRect(); return {right:r.right, left:r.left, vw:innerWidth, sw:document.documentElement.scrollWidth, h:r.height, visible:r.width>0&&r.height>0};})()")
        check("28y %dpx Restore-mode selector is visible without horizontal overflow" % w, box["visible"] and box["left"] >= -1 and box["right"] <= box["vw"] + 1 and box["sw"] <= box["vw"] + 1, box)
        check("28z %dpx Restore mode has no JavaScript errors" % w, not er, er)
        q.close()

    # 29. v0.66: Texture hider and Texture restorer share one finder that sits in the card of its mode
    def p29_open(w, extra=""):
        q = b.new_page(viewport={"width": w, "height": 800}); er = []
        q.on("pageerror", lambda e: er.append(str(e)))
        q.goto(PAGE + "?demo=300" + extra); q.wait_for_timeout(700)
        q.click("#dock [data-v=adv]"); q.wait_for_timeout(250)
        return q, er
    def card_open(q, cid):
        # a real tap on the card header, with the other of the two cards and this one closed first, like a person would
        for c in ("grassCard" if cid == "rsCard" else "rsCard", cid):
            q.evaluate("(function(id){var d=document.getElementById(id).querySelector('details'); if(d && d.open) d.open=false;})", c)
        q.wait_for_timeout(100)
        q.click("#%s summary" % cid); q.wait_for_timeout(300)
    for w in (320, 360, 390, 411):
        q, er = p29_open(w)
        in_list = q.evaluate("(function(){var h=[].slice.call(document.querySelectorAll('summary h2')).map(function(e){return e.textContent.trim().replace(/^Textures/,'')});return h.indexOf('Texture hider')>=0 && h.indexOf('Texture restorer')===h.indexOf('Texture hider')+1 && h.indexOf('Texture browser')===h.indexOf('Texture restorer')+1})()")
        check("29a %dpx Texture restorer sits between Texture hider and Texture browser" % w, in_list)
        card_open(q, "rsCard")
        where = q.evaluate("(function(){var g=document.getElementById('gf');return {inR:!!g.closest('#rsCard'),inH:!!g.closest('#grassCard'),mode:localStorage.getItem('ct_test_mode'),stubH:!document.getElementById('gfStubH').hidden,start:(document.getElementById('gfStart')||{}).textContent||''}})()")
        check("29b %dpx opening the Texture restorer puts the finder there in Restore mode, the hider shows a note" % w, where["inR"] and not where["inH"] and where["mode"] == "restore" and where["stubH"], where)
        check("29c %dpx the Restore start button says restore search and there is no Test mode select" % w, "restore search" in where["start"] and q.locator("#gfMode").count() == 0, where["start"])
        card_open(q, "grassCard")
        back = q.evaluate("(function(){var g=document.getElementById('gf');return {inR:!!g.closest('#rsCard'),inH:!!g.closest('#grassCard'),mode:localStorage.getItem('ct_test_mode'),start:(document.getElementById('gfStart')||{}).textContent||''}})()")
        check("29d %dpx opening the Texture hider moves the finder back in Hide mode" % w, back["inH"] and not back["inR"] and back["mode"] == "hide" and "hide search" in back["start"], back)
        over = q.evaluate("document.documentElement.scrollWidth<=innerWidth+1")
        check("29e %dpx no horizontal overflow and no JavaScript errors" % w, over and not er, er)
        q.close()
    # 29f: a running test in one card cannot be started or confused from the other card
    q, er = p29_open(360)
    card_open(q, "rsCard"); q.evaluate("document.getElementById('gfStart').click()"); q.wait_for_timeout(300)
    busy = q.evaluate("(function(){var s=document.getElementById('gfStubH');return {hidden:s.hidden,txt:s.textContent,inR:!!document.getElementById('gf').closest('#rsCard'),yes:!!document.getElementById('gfYes')}})()")
    check("29f a Restore test in progress: the Texture hider only shows a note that points to the restorer", busy["inR"] and busy["yes"] and not busy["hidden"] and "Restore test in progress" in busy["txt"], busy)
    card_open(q, "grassCard")
    still = q.evaluate("(function(){return {inR:!!document.getElementById('gf').closest('#rsCard'),mode:localStorage.getItem('ct_test_mode')}})()")
    check("29g opening the Texture hider does not take the finder away from a running Restore test", still["inR"] and still["mode"] == "restore", still)
    check("29h hider/restorer switching has no JavaScript errors", not er, er)
    q.close()
    # 29i: the two finders never offer the same texture: a hidden one and a restored one are offered to neither finder (no overlap, no error)
    def p29_counts(extra):
        q, er = p29_open(360, extra)
        card_open(q, "grassCard"); nh = int(re.search(r"(\d+) texture", q.inner_text("#gfStart")).group(1))
        kh = q.evaluate("window.__ct.visList().filter(function(c){return c.lv>=2}).slice(0,2).map(function(c){return c.key})")
        card_open(q, "rsCard"); nr = int(re.search(r"(\d+) texture", q.inner_text("#gfStart")).group(1))
        kr = q.evaluate("window.__ct.visList().filter(function(c){return c.lv>=2}).slice(0,2).map(function(c){return c.key})")
        q.close(); return nh, nr, kh, kr, er
    nh0, nr0, kh, kr, er0 = p29_counts("")
    if len(kh) == 2 and len(kr) == 2:
        sp = lambda ks: ";".join(k.replace("|", " ") for k in ks)
        nh1, nr1, _a, _b, er1 = p29_counts("&grass=" + quote(sp([kh[0], kr[0]]), safe="") + "&orig=" + quote(sp([kh[1], kr[1]]), safe=""))
        check("29i the Hide finder leaves out one hidden and one restored texture (no overlap, no error)", nh1 == nh0 - 2 and not er0 and not er1, (nh0, nh1, er0, er1))
        check("29j the Restore finder leaves out one hidden and one restored texture (no overlap, no error)", nr1 == nr0 - 2, (nr0, nr1))
    else:
        check("29i fixture has two candidates in each finder", False, (kh, kr))

    b.close()

print("\n%d passed, %d failed" % (passed, len(fails)))
sys.exit(1 if fails else 0)
