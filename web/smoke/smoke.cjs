// Smoke test for the browser build in headless Chromium (Playwright).
//
//   node web/smoke/smoke.cjs <url> <out-dir> [--phone]
//
// Checks: the page starts with no page errors and shows How to play on a
// first visit, then the first night's card; the canvas draws the marsh (not
// one flat colour); walking moves the keeper (W on a desktop, the touch stick
// on the phone profile) and a ghost joins; the Flare button (or Space) flares
// the lantern; the night ends when every ghost is home (the ghostLights.solve
// page hook) and its card's Next night button starts night 2; the autopilot
// hook brings night 2's ghosts home by walking; on the phone no two controls
// overlap and all are on screen; the Sound button turns the sound off and a
// reload keeps it off. Writes PNGs to <out-dir>.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { chromium, devices } = require('playwright');

const [url, outDir] = process.argv.slice(2);
const phone = process.argv.includes('--phone');
if (!url || !outDir) {
  console.error('usage: smoke.cjs <url> <out-dir> [--phone]');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced: what Chromium writes).
function decodePng(buf) {
  let pos = 8, width = 0, height = 0, channels = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      channels = { 2: 3, 6: 4 }[data[9]];
      if (data[8] !== 8 || !channels || data[12] !== 0) throw new Error('unsupported PNG');
    } else if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      let p;
      if (f === 0) p = 0;
      else if (f === 1) p = a;
      else if (f === 2) p = b;
      else if (f === 3) p = (a + b) >> 1;
      else {
        const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
        p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[y * stride + x] = (v + p) & 255;
    }
  }
  return { width, height, channels, data: out };
}

// Distinct colours (quantised to 4 bits per channel) in a PNG.
function colourCount(png) {
  const img = decodePng(png);
  const seen = new Set();
  for (let i = 0; i < img.data.length; i += img.channels)
    seen.add(((img.data[i] >> 4) << 8) | ((img.data[i + 1] >> 4) << 4) | (img.data[i + 2] >> 4));
  return seen.size;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const base = phone ? devices['iPhone 13'] : { viewport: { width: 1280, height: 800 } };
  // "headless" in the UA keeps arcade from counting a smoke run as a play.
  const context = await browser.newContext({ ...base, userAgent: (base.userAgent || 'Mozilla/5.0') + ' ghost-lights-smoke headless' });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  const fail = async (why) => {
    console.error('FAIL: ' + why);
    await page.screenshot({ path: path.join(outDir, 'failure.png'), timeout: 60000 }).catch(() => {});
    await browser.close();
    process.exit(1);
  };
  const open = async (reload) => {
    if (reload) await page.reload({ waitUntil: 'commit', timeout: 120000 });
    else await page.goto(url, { waitUntil: 'commit' });
    try {
      await page.waitForFunction(() => window.ghostLights && window.ghostStatus, null, { timeout: 90000 });
    } catch (e) {
      await fail('the game never became ready' + (reload ? ' after a reload' : '') + ': ' + errors.join(' | '));
    }
  };
  const status = async () => JSON.parse(await page.evaluate(() => window.ghostLights.status()));
  const until = async (pred, ms = 20000) => {
    const end = Date.now() + ms;
    let st = await status();
    while (!pred(st) && Date.now() < end) {
      await page.waitForTimeout(200);
      st = await status();
    }
    return st;
  };
  const shot = (name) => page.screenshot({ path: path.join(outDir, name), timeout: 180000 });
  // Clicks through Playwright, or by a dispatched click when the page is too
  // slow (software GL) for its actionability checks.
  const press = async (sel) => {
    await page.locator(sel).click({ timeout: 10000 }).catch(() => page.locator(sel).dispatchEvent('click'));
  };
  const visible = (sel) => page.locator(sel).isVisible();

  // On a phone, every visible control is on screen and none overlaps another.
  const checkLayout = async (when) => {
    if (!phone) return;
    const boxes = await page.evaluate(() => {
      const out = [];
      document.querySelectorAll('#top button, #flareBtn, .card.on button').forEach((b) => {
        const r = b.getBoundingClientRect();
        if (r.width > 0 && r.height > 0 && getComputedStyle(b).display !== 'none' && b.offsetParent)
          out.push({ id: b.id || b.textContent, x: r.left, y: r.top, w: r.width, h: r.height });
      });
      return { out, vw: innerWidth, vh: innerHeight };
    });
    for (const a of boxes.out) {
      if (a.x < 0 || a.y < 0 || a.x + a.w > boxes.vw + 0.5 || a.y + a.h > boxes.vh + 0.5) await fail(`${when}: ${a.id} is off screen`);
      if (a.w < 43.5 || a.h < 43.5) await fail(`${when}: ${a.id} is smaller than a finger (${a.w} x ${a.h})`);
      for (const b of boxes.out) {
        if (a === b) continue;
        if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h) await fail(`${when}: ${a.id} overlaps ${b.id}`);
      }
    }
    console.log(`ok: ${boxes.out.length} controls on screen, none overlapping (${when})`);
  };

  await open(false);
  if (!(await visible('#help'))) await fail('How to play was not shown on a first visit');
  await checkLayout('how to play');
  await press('#helpClose');
  if (!(await visible('#intro'))) await fail('the first night\'s card did not show');
  const introText = await page.locator('#introName').innerText();
  if (introText !== 'The Lantern') await fail('the first night is called ' + introText);
  await checkLayout('the first night\'s card');
  await press('#beginBtn');
  let s = await status();
  console.log(`ready: night ${s.level + 1} "${s.name}" with ${s.ghosts} ghosts, keeper at (${s.x}, ${s.z})`);
  if (s.level !== 0 || s.ghosts !== 3 || s.home !== 0) await fail('night 1 should start with 3 ghosts out');
  await page.waitForTimeout(1500);
  const png = await shot('ghost-lights.png');
  const colours = colourCount(png);
  console.log(`screenshot: ${colours} distinct colours`);
  if (colours < 16) await fail('the canvas looks blank (' + colours + ' colours)');
  await checkLayout('playing');

  // Walk north, towards the first ghosts.
  const start = { x: s.x, z: s.z };
  if (phone) {
    const box = await page.locator('#touch').boundingBox();
    const cdp = await context.newCDPSession(page);
    const sx = box.x + box.width * 0.4, sy = box.y + box.height * 0.7;
    const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
    await touch('touchStart', [{ x: sx, y: sy, id: 1 }]);
    for (let i = 1; i <= 5; i++) await touch('touchMove', [{ x: sx, y: sy - 9 * i, id: 1 }]);
    s = await until((st) => st.z < start.z - 3, 30000);
    await touch('touchEnd', []);
  } else {
    await page.keyboard.down('KeyW');
    s = await until((st) => st.z < start.z - 3, 30000);
    await page.keyboard.up('KeyW');
  }
  console.log(`after walking: at (${s.x}, ${s.z}), ${s.following} following, ${s.ev.joined} joined`);
  if (s.z > start.z - 3) await fail('walking did not move the keeper north');
  s = await until((st) => st.ev.joined > 0, 30000);
  if (s.ev.joined < 1) await fail('no ghost joined the lantern');
  if (s.ev.scattered !== 0) await fail('a walk frightened a ghost');
  await shot('ghost-lights-walk.png');

  // The flare.
  if (phone) await page.locator('#flareBtn').dispatchEvent('pointerdown');
  else await page.keyboard.press('Space');
  s = await until((st) => st.ev.flares > 0);
  if (s.ev.flares !== 1) await fail('the flare did not go off');
  console.log('ok: the lantern flared');

  // Every ghost home ends the night; the card's button starts the next one.
  await page.evaluate(() => window.ghostLights.solve());
  s = await until((st) => st.done);
  if (!s.done) await fail('the night did not end with every ghost home');
  if (!(await page.waitForFunction(() => document.getElementById('done').classList.contains('on'), null, { timeout: 10000 }).catch(() => false)))
    await fail('the end-of-night card did not show');
  await checkLayout('the end-of-night card');
  await shot('ghost-lights-done.png');
  await press('#nextBtn');
  s = await until((st) => st.level === 1);
  if (s.level !== 1) await fail('Next night did not start night 2');
  if (!(await visible('#intro'))) await fail('night 2\'s card did not show');
  await press('#beginBtn');
  console.log(`ok: night 2 "${s.name}" with ${s.ghosts} ghosts`);

  // The autopilot walks night 2's ghosts home: the real rules, in the page.
  await page.evaluate(() => window.ghostLights.autopilot(1));
  s = await until((st) => st.done, 240000);
  await page.evaluate(() => window.ghostLights.autopilot(0));
  console.log(`after the autopilot: ${s.home} of ${s.ghosts} home in ${s.time.toFixed(1)} s, ${s.scatters} frights`);
  if (!s.done) await fail('the autopilot did not bring night 2\'s ghosts home');

  // Nights 3 and 5 draw (fog banks, wisps, the churchyard): screenshots for the README.
  for (const [n, name] of [[2, 'fog'], [4, 'churchyard']]) {
    await page.evaluate((i) => window.ghostLights.level(i), n);
    s = await until((st) => st.level === n);
    if (s.level !== n) await fail('the level hook did not open night ' + (n + 1));
    await page.waitForFunction(() => document.getElementById('intro').classList.contains('on'), null, { timeout: 10000 }).catch(() => {});
    await press('#beginBtn');
    await page.evaluate(() => window.ghostLights.autopilot(1));
    await page.waitForTimeout(6000);
    await page.evaluate(() => window.ghostLights.autopilot(0));
    await shot(`ghost-lights-${name}.png`);
    console.log(`ok: night ${n + 1} "${s.name}" draws (${s.fog} fog banks, ${s.wisps} wisps)`);
  }

  // Sound off, kept over a reload.
  await press('#soundBtn');
  if ((await page.locator('#soundBtn').getAttribute('aria-pressed')) !== 'false') await fail('the Sound button did not turn the sound off');
  await open(true);
  if ((await page.locator('#soundBtn').getAttribute('aria-pressed')) !== 'false') await fail('a reload turned the sound back on');
  if (await visible('#help')) await fail('How to play showed again after it was closed');
  console.log('ok: sound off is remembered; How to play is shown once');

  if (errors.length) await fail('page errors: ' + errors.join(' | '));
  console.log('PASS' + (phone ? ' (phone)' : ' (desktop)'));
  await browser.close();
})();
