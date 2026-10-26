// Syntax check of every inline <script> in webroot/index.html (no browser needed). Usage: node tests/js_syntax_check.js [file]
const fs = require("fs"), vm = require("vm"), path = require("path");
const file = process.argv[2] || path.join(__dirname, "..", "webroot", "index.html");
const html = fs.readFileSync(file, "utf8");
const re = /<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g;
let m, n = 0, bad = 0;
while ((m = re.exec(html))) {
  n++;
  try { new vm.Script(m[1], { filename: "inline-script-" + n }); } catch (e) { bad++; console.log("SYNTAX ERROR in script " + n + ": " + e.message); }
}
console.log((bad ? "FAIL" : "OK") + ": " + n + " inline scripts checked");
process.exit(bad ? 1 : 0);
