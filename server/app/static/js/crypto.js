// Server-rendered sample block (see templates/crypto.html).
const INITIAL_BLOCK = JSON.parse(document.getElementById('initial-block').textContent);

// Pure-JS SHA-256 implementation so the live interactive sandbox works on both HTTPS and plain HTTP
function sha256HexPureJS(asciiStr) {
  const utf8 = new TextEncoder().encode(asciiStr);
  const K = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  ];
  let H = [
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  ];
  const bitLen = utf8.length * 8;
  const padLen = ((utf8.length + 9 + 63) >> 6) << 6;
  const buf = new Uint8Array(padLen);
  buf.set(utf8);
  buf[utf8.length] = 0x80;
  const view = new DataView(buf.buffer);
  view.setUint32(padLen - 8, Math.floor(bitLen / 0x100000000), false);
  view.setUint32(padLen - 4, bitLen >>> 0, false);

  const W = new Uint32Array(64);
  const rotr = (x, n) => (x >>> n) | (x << (32 - n));

  for (let offset = 0; offset < padLen; offset += 64) {
    for (let i = 0; i < 16; i++) {
      W[i] = view.getUint32(offset + i * 4, false);
    }
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(W[i - 15], 7) ^ rotr(W[i - 15], 18) ^ (W[i - 15] >>> 3);
      const s1 = rotr(W[i - 2], 17) ^ rotr(W[i - 2], 19) ^ (W[i - 2] >>> 10);
      W[i] = (W[i - 16] + s0 + W[i - 7] + s1) >>> 0;
    }
    let [a, b, c, d, e, f, g, h] = H;
    for (let i = 0; i < 64; i++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ ((~e) & g);
      const temp1 = (h + S1 + ch + K[i] + W[i]) >>> 0;
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (S0 + maj) >>> 0;
      h = g; g = f; f = e;
      e = (d + temp1) >>> 0;
      d = c; c = b; b = a;
      a = (temp1 + temp2) >>> 0;
    }
    H[0] = (H[0] + a) >>> 0; H[1] = (H[1] + b) >>> 0;
    H[2] = (H[2] + c) >>> 0; H[3] = (H[3] + d) >>> 0;
    H[4] = (H[4] + e) >>> 0; H[5] = (H[5] + f) >>> 0;
    H[6] = (H[6] + g) >>> 0; H[7] = (H[7] + h) >>> 0;
  }
  return H.map(v => v.toString(16).padStart(8, '0')).join('');
}

const TOKEN_META = [
  { name: 'Protocol Version', why: 'Enforces strict v1 schema parsing on both client and server.' },
  { name: 'Player ID (player_id)', why: 'Unique server-assigned account identifier bound to the Ed25519 keypair.' },
  { name: 'Player Nickname', why: 'Commits the display name into the signed digest so it cannot be spoofed.' },
  { name: 'Challenge Ticket ID', why: 'Single-use ticket identifier (marked used=1 atomically on submission).' },
  { name: 'Challenge Nonce (128-bit)', why: 'Server CSPRNG nonce preventing pre-computation or offline signature replay.' },
  { name: '5-Round Average (avg_ms)', why: 'Exact millisecond mean formatted to 3 decimal places.' },
  { name: 'Best Single Reaction (best_ms)', why: 'Fastest individual round in the 5-round set (3 decimal places).' },
  { name: 'All 5 Round Times CSV', why: 'Every individual round latency (r1,r2,r3,r4,r5) verified against avg_ms & best_ms.' },
  { name: '5 Server Wait Delays CSV', why: 'Must match the exact 5 random wait delays issued by /api/v1/challenge.' },
  { name: 'Client Binary SHA-256', why: 'Self-computed SHA-256 digest of the running executable on disk.' }
];

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function renderCanonicalTokens(canonStr) {
  const parts = canonStr.split('|');
  const grid = document.getElementById('sbTokenGrid');
  grid.innerHTML = TOKEN_META.map((m, idx) => {
    const val = (idx < parts.length) ? parts[idx] : '(missing field)';
    return `
      <div class="token-item">
        <div><span class="token-idx">[Field #${idx}]</span><span class="token-name">${escapeHtml(m.name)}</span></div>
        <span class="token-val">${escapeHtml(val)}</span>
        <div class="token-why">${escapeHtml(m.why)}</div>
      </div>
    `;
  }).join('');
}

function updateLiveSandbox() {
  const prev = document.getElementById('sbPrevHash').value;
  const canon = document.getElementById('sbCanon').value;
  const sig = document.getElementById('sbSig').value.toLowerCase();
  const ts = document.getElementById('sbTs').value;

  const combined = `${prev}|${canon}|${sig}|${ts}`;
  const computed = sha256HexPureJS(combined);
  const stored = INITIAL_BLOCK.entry_hash;

  document.getElementById('sbStoredHash').innerText = stored;
  const compEl = document.getElementById('sbComputedHash');
  compEl.innerText = computed;

  const badge = document.getElementById('sbMatchBadge');
  if (computed === stored) {
    badge.className = 'pill pill-ok';
    badge.innerText = '✓ EXACT SHA-256 BLOCK MATCH';
    compEl.style.color = '#38ef7d';
  } else {
    badge.className = 'pill pill-bad';
    badge.innerText = '✗ HASH MISMATCH (TAMPER DETECTED)';
    compEl.style.color = '#ff4757';
  }

  renderCanonicalTokens(canon);
}

function resetLiveSandbox() {
  document.getElementById('sbPrevHash').value = INITIAL_BLOCK.prev_hash;
  document.getElementById('sbCanon').value = INITIAL_BLOCK.canonical_payload;
  document.getElementById('sbSig').value = INITIAL_BLOCK.ed25519_signature;
  document.getElementById('sbTs').value = INITIAL_BLOCK.submitted_at_str;
  updateLiveSandbox();
}

window.addEventListener('DOMContentLoaded', resetLiveSandbox);
