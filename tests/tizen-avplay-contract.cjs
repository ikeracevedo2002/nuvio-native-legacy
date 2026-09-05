// Exercises the production EM_JS bridge against the AVPlay fake. This covers
// JS calls and state transitions, not the C parser, codecs or TV compositing.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('src/video_tizen.c', 'utf8');
const body = source.match(/EM_JS\(double, nv_av,[\s\S]*?int dstTam\), \{([\s\S]*?)\n\}\);/);
assert(body, 'production bridge not found');
const outputs = new Map();
const context = vm.createContext({
  console, setTimeout, clearTimeout, document: {},
  UTF8ToString: value => value,
  stringToUTF8: (text, address) => outputs.set(address, text),
  HEAPF64: new Float64Array(16),
});
vm.runInContext('window = globalThis', context);
vm.runInContext(fs.readFileSync('tools/fake-avplay.js', 'utf8'), context);
// EM_JS stringification preserves C escapes; emulate the doubled backslashes
// used for JS strings/regexes in this source. The full emcc build checks syntax.
vm.runInContext('bridge = function(cmd, txt, a, b, c, d, dst, dstTam) {' +
  body[1].replace(/\\\\/g, '\\') + '\n}', context);
const call = (op, text = '', a = 0, b = 0, c = 0, d = 0, dst = 0, size = 0) =>
  context.bridge(op, text, a, b, c, d, dst, size);

(async () => {
  let failures = 0;
  function check(name, fn) {
    try { fn(); console.log('PASS:', name); }
    catch (e) { failures++; console.error('FAIL:', name, e.message); }
  }
  assert.equal(call('abrir', 'https://example.invalid/video.mkv'), 1);
  await new Promise(resolve => setTimeout(resolve, 30));
  call('estado', '', 0, 0, 0, 0, 8, 64);
  check('duration uses getDuration in milliseconds', () =>
    assert.equal(context.HEAPF64[2], 7200));
  check('video metadata is read in a valid state after async preparation', () => {
    assert.equal(context.HEAPF64[6], 3840);
    assert.equal(context.HEAPF64[7], 2160);
  });
  call('faixas', '', 0, 0, 0, 0, 100, 4096);
  check('bridge returns all audio and text rows', () => {
    assert.match(outputs.get(100), /A\t1\teng/);
    assert.match(outputs.get(100), /A\t2\tpor/);
    assert.match(outputs.get(100), /T\t3\tpor/);
  });
  check('app pumps video before any screen can return', () => {
    const app = fs.readFileSync('src/app.c', 'utf8');
    assert.ok(/void app_atualizar\(float dt, Uint32 agora\)\s*\{\s*(?:\/\*[\s\S]*?\*\/\s*|\/\/[^\n]*\n\s*)*video_bombear\(\);/.test(app),
      'video_bombear must run before conditional screen handlers');
    assert.equal((app.match(/video_bombear\(\);/g) || []).length, 1);
  });
  call('parar');
  process.exitCode = failures ? 1 : 0;
})();
