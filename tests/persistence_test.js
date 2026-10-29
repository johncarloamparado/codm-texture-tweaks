#!/usr/bin/env node
/* Persistence regression checks: run with node tests/persistence_test.js. No Android shell/device required. */
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');
const { spawnSync } = require('child_process');
const assert = require('assert');

const ROOT = path.resolve(__dirname, '..');
const HTML = fs.readFileSync(path.join(ROOT, 'webroot/index.html'), 'utf8');
const CPP = fs.readFileSync(path.join(ROOT, 'main.cpp'), 'utf8');
const MOD = fs.readFileSync(path.join(ROOT, 'module.prop'), 'utf8');
const HAN = fs.readFileSync(path.join(ROOT, 'HANDBOOK.md'), 'utf8');
let passed = 0;
function check(name, fn) {
  try { fn(); passed++; process.stdout.write('PASS ' + name + '\n'); }
  catch (e) { process.stderr.write('FAIL ' + name + ': ' + (e && e.message || e) + '\n'); process.exitCode = 1; }
}
function between(source, start, end) {
  const a = source.indexOf(start);
  const b = source.indexOf(end, a + start.length);
  assert(a >= 0 && b > a, 'could not locate source section: ' + start);
  return source.slice(a, b);
}
const mkd = () => fs.mkdtempSync(path.join(os.tmpdir(), 'codm-persist-'));
function failMvDir(dir) {
  const bin = path.join(dir, 'bin');
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(bin, 'mv'), '#!/bin/sh\necho simulated-mv-failure >&2\nexit 1\n', { mode: 0o755 });
  return bin;
}
function shell(cmd, envExtra = {}) {
  const r = spawnSync('/bin/sh', ['-c', cmd], {
    encoding: 'utf8',
    env: { ...process.env, ...envExtra },
    maxBuffer: 1024 * 1024
  });
  return { errno: r.status == null ? -1 : r.status, stdout: r.stdout || '', stderr: r.stderr || (r.error ? String(r.error) : '') };
}
function atomicContext(dir, mvFail = false, log = []) {
  let env = {};
  if (mvFail) {
    const bin = failMvDir(dir);
    env.PATH = bin + path.delimiter + process.env.PATH;
  }
  const ctx = vm.createContext({
    D: dir,
    Promise,
    sh: cmd => { log.push(cmd); return Promise.resolve(shell(cmd, env)); },
    queued: fn => Promise.resolve().then(fn),
    backupSoon: () => {}
  });
  vm.runInContext(between(HTML, '  var WCH = 400;', '  function writeGrass()'), ctx, { timeout: 1000 });
  return ctx;
}
function flushContext(dir, opts = {}) {
  const log = [];
  const payload = opts.payload || Buffer.from(JSON.stringify({ ct_name: 'New value' })).toString('base64');
  let env = {};
  if (opts.mvFail) {
    const bin = failMvDir(dir);
    env.PATH = bin + path.delimiter + process.env.PATH;
  }
  const ctx = vm.createContext({
    demo: false, uiReady: true, uiDirty: true, uiBusy: false,
    uiLast: '', uiInfo: '',
    Promise, Math,
    uiPack: () => ({ b: payload, n: 1 }),
    bkDir: () => dir,
    uiShow: () => {}, backupSoon: () => {},
    sh: cmd => {
      log.push(cmd);
      if (opts.writeFail && cmd.indexOf('printf %s') >= 0)
        return Promise.resolve({ errno: 1, stdout: '', stderr: 'simulated chunk write failure' });
      if (opts.silentDrop && cmd.indexOf('printf %s') >= 0)
        return Promise.resolve({ errno: 0, stdout: 'UIWRITEOK\n', stderr: '' });
      return Promise.resolve(shell(cmd, env));
    }
  });
  vm.runInContext(between(HTML, '  function uiFlush() {', '  function uiRestore()'), ctx, { timeout: 1000 });
  return { ctx, log, payload, flush: () => ctx.uiFlush() };
}

(async function main() {
  check('release version is aligned at v0.70 / versionCode 93', () => {
    assert(CPP.includes('#define VERSION "v0.70"'));
    assert(MOD.includes('version=v0.70') && MOD.includes('versionCode=93'));
    assert(HTML.includes('ver: "v0.70"') && HTML.includes("echo 'UI: v0.70'"));
  });
  check('Restore finder has its own candidate stream and only writes orig.txt in Restore mode', () => {
    assert(HTML.includes('loadCandText("GRASSCAND")') && HTML.includes('loadCandText("RESTORECAND")'));
    assert(CPP.includes('RESTORECAND fmt=%d') && CPP.includes('g_rseen_n < 4096'));
    assert(HTML.includes('function applyModeRules(mode, keys, msg) { return mode === "restore" ? setOrigRules(keys, msg) : setRules(keys, msg); }'));
    assert(HTML.includes('return writeOrig().then(function (r) { grassDone(r, msg); return !!r.ok; });'));
    assert(HTML.includes('gSave(); /* v0.65: a Restore test must survive the same reloads as a Hide test */'));
  });
  check('Restore mode is persisted, old tests without a mode still default to Hide, and global scale limit is disclosed', () => {
    assert(HTML.includes('localStorage.setItem("ct_test_mode", gTestMode)'));
    assert(HTML.includes('if (raw.mode !== "restore") raw.mode = "search"'));
    assert(HTML.includes('not global render scale') && HTML.includes('global anisotropy clamp'));
    assert(HTML.includes('Restore test paused, not lost'));
  });
  check('backward-compatible Hide default and explicit Restore token remain documented', () => {
    assert(/act = "hide"/.test(HTML));
    assert(/\(gp\[2\] \|\| ""\)\.trim\(\) === "r" \? "restore" : "hide"/.test(HTML));
    assert(HAN.includes('Action field') && HAN.includes('4096 unique'));
  });
  check('pinned-folder and settings/UI backup allowlists remain intact', () => {
    assert(/pinU\) for \(var j = 0; j < list\.length; j\+\+\) if \(String\(list\[j\]\.u\) === pinU\)/.test(HTML));
    assert(/var CFGLIST = "[^"\\]*\bgrass\b[^"\\]*\borig\b[^"\\]*\btexlib\b/.test(HTML));
    assert(/var UI_SKIP = \/\^\(ct_pin\|ct_pinu\|ct_wr\|ct_ui_stamp\|ct_ui_msg\)\$\//.test(HTML));
    assert(/#define GRASS_MAX_RULES 4096/.test(CPP));
  });

  const tempRoots = [];
  try {
    const d = mkd(); tempRoots.push(d);
    fs.writeFileSync(path.join(d, 'grass.txt'), 'previous rules\n');
    const log = [];
    const ctx = atomicContext(d, true, log);
    const failed = await ctx.writeLinesAtomic('grass.txt', ['new rule one', 'new rule two']);
    check('failed atomic rename is reported as failure and preserves old settings', () => {
      assert.strictEqual(failed.ok, false);
      assert.strictEqual(fs.readFileSync(path.join(d, 'grass.txt'), 'utf8'), 'previous rules\n');
      assert(!fs.existsSync(path.join(d, 'grass.txt.new')));
      assert(log.some(c => c.includes('mv -f') && c.includes('grass.txt')));
    });
    const goodCtx = atomicContext(d, false, []);
    const good = await goodCtx.writeLinesAtomic('grass.txt', ['new rule one', 'new rule two']);
    check('successful atomic write commits complete contents and removes temporary file', () => {
      assert.strictEqual(good.ok, true);
      assert.strictEqual(fs.readFileSync(path.join(d, 'grass.txt'), 'utf8'), 'new rule one\nnew rule two\n');
      assert(!fs.existsSync(path.join(d, 'grass.txt.new')));
    });

    const dOrig = mkd(); tempRoots.push(dOrig);
    fs.writeFileSync(path.join(dOrig, 'orig.txt'), 'previous restore rules\n');
    const origCtx = atomicContext(dOrig, true, []);
    origCtx.demo = false; origCtx.MAXR = 4096; origCtx.cur = {};
    origCtx.origSel = { '37492|256|256|0123456789abcdef': 1 };
    origCtx.nk = x => x;
    origCtx.origLines = () => Object.keys(origCtx.origSel).sort().map(k => k.replace(/\|/g, ' '));
    vm.runInContext(between(HTML, '  function writeOrig() {', '  if (demo) window.__ct'), origCtx, { timeout: 1000 });
    const failOrig = await origCtx.writeOrig();
    check('failed atomic orig.txt write preserves previous restore rules and removes temporary file', () => {
      assert.strictEqual(failOrig.ok, false);
      assert.strictEqual(fs.readFileSync(path.join(dOrig, 'orig.txt'), 'utf8'), 'previous restore rules\n');
      assert(!fs.existsSync(path.join(dOrig, 'orig.txt.new')));
    });

    const d2 = mkd(); tempRoots.push(d2);
    fs.writeFileSync(path.join(d2, 'ui.b64'), 'previous-good-ui-backup');
    const badWrite = flushContext(d2, { writeFail: true });
    await badWrite.flush();
    check('failed UI backup chunk never replaces ui.b64 and remains dirty for retry', () => {
      assert.strictEqual(fs.readFileSync(path.join(d2, 'ui.b64'), 'utf8'), 'previous-good-ui-backup');
      assert.strictEqual(badWrite.ctx.uiBusy, false);
      assert.strictEqual(badWrite.ctx.uiDirty, true);
      assert(!badWrite.log.some(c => c.includes('mv -f') && c.includes('ui.tmp')));
      assert(!fs.existsSync(path.join(d2, 'ui.tmp')));
    });

    const d3 = mkd(); tempRoots.push(d3);
    fs.writeFileSync(path.join(d3, 'ui.b64'), 'previous-good-ui-backup');
    const shortWrite = flushContext(d3, { silentDrop: true });
    await shortWrite.flush();
    check('UI backup verifies exact byte count before atomic replacement', () => {
      assert.strictEqual(fs.readFileSync(path.join(d3, 'ui.b64'), 'utf8'), 'previous-good-ui-backup');
      assert.strictEqual(shortWrite.ctx.uiDirty, true);
      assert.strictEqual(shortWrite.ctx.uiBusy, false);
      assert(!fs.existsSync(path.join(d3, 'ui.tmp')));
      assert(shortWrite.log.some(c => c.includes('wc -c')));
    });

    const d4 = mkd(); tempRoots.push(d4);
    fs.writeFileSync(path.join(d4, 'ui.b64'), 'previous-good-ui-backup');
    const moveFail = flushContext(d4, { mvFail: true });
    await moveFail.flush();
    check('failed UI backup rename preserves last good backup and resets busy state', () => {
      assert.strictEqual(fs.readFileSync(path.join(d4, 'ui.b64'), 'utf8'), 'previous-good-ui-backup');
      assert.strictEqual(moveFail.ctx.uiDirty, true);
      assert.strictEqual(moveFail.ctx.uiBusy, false);
      assert(!fs.existsSync(path.join(d4, 'ui.tmp')));
    });

    const d5 = mkd(); tempRoots.push(d5);
    fs.writeFileSync(path.join(d5, 'ui.b64'), 'previous-good-ui-backup');
    const success = flushContext(d5);
    await success.flush();
    check('successful UI backup atomically commits exact payload and clears dirty state', () => {
      assert.strictEqual(fs.readFileSync(path.join(d5, 'ui.b64'), 'utf8'), success.payload);
      assert.strictEqual(success.ctx.uiLast, success.payload);
      assert.strictEqual(success.ctx.uiDirty, false);
      assert.strictEqual(success.ctx.uiBusy, false);
      assert(!fs.existsSync(path.join(d5, 'ui.tmp')));
    });
  } catch (e) {
    process.stderr.write('FAIL unexpected persistence harness error: ' + (e && e.stack || e) + '\n');
    process.exitCode = 1;
  } finally {
    for (const d of tempRoots) fs.rmSync(d, { recursive: true, force: true });
  }
  process.stdout.write('\n' + passed + ' persistence checks passed' + (process.exitCode ? ' (with failures)' : '') + '\n');
})();
