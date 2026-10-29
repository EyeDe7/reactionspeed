// Initial server-rendered state (see templates/dashboard.html).
const DASHBOARD_STATE = JSON.parse(document.getElementById('dashboard-state').textContent);
let currentAvgPage = DASHBOARD_STATE.avg_page;
let currentSinglePage = DASHBOARD_STATE.single_page;
let currentPerPage = DASHBOARD_STATE.per_page;
let totalPages = DASHBOARD_STATE.total_pages;
let dbDistribution = DASHBOARD_STATE.distribution;
let distMode = 'rounds';
let curveFitMode = 'lognorm';

function setDistMode(mode) {
  distMode = mode;
  document.getElementById('btnModeRounds').classList.toggle('btn-toggle-active', mode === 'rounds');
  document.getElementById('btnModeAverages').classList.toggle('btn-toggle-active', mode === 'averages');
  renderDbBellCurve();
}

function setCurveFit(fit) {
  curveFitMode = fit;
  document.getElementById('btnFitLognorm').classList.toggle('btn-toggle-active', fit === 'lognorm');
  document.getElementById('btnFitGaussian').classList.toggle('btn-toggle-active', fit === 'gaussian');
  renderDbBellCurve();
}

function evalGaussianPdf(x, mean, std) {
  const s = Math.max(std, 6.0);
  const z = (x - mean) / s;
  return (1.0 / (s * Math.sqrt(2.0 * Math.PI))) * Math.exp(-0.5 * z * z);
}

function evalLogNormalPdf(x, logMu, logSigma) {
  if (x <= 1.0) return 0.0;
  const s = Math.max(logSigma, 0.04);
  const z = (Math.log(x) - logMu) / s;
  return (1.0 / (x * s * Math.sqrt(2.0 * Math.PI))) * Math.exp(-0.5 * z * z);
}

function evalGlobalRefPdf(x) {
  if (x <= 80.0) return 0.0;
  const mu = Math.log(125.0);
  const sigma = 0.28;
  const shifted = x - 80.0;
  const z = (Math.log(shifted) - mu) / sigma;
  return (1.0 / (shifted * sigma * Math.sqrt(2.0 * Math.PI))) * Math.exp(-0.5 * z * z);
}

function renderDbBellCurve() {
  const canvas = document.getElementById('dbBellCurveCanvas');
  if (!canvas || !dbDistribution) return;

  const series = (distMode === 'rounds') ? dbDistribution.rounds : dbDistribution.averages;
  const players = (distMode === 'rounds') ? (dbDistribution.players_by_single || []) : (dbDistribution.players_by_avg || []);

  const unitLabel = (distMode === 'rounds') ? 'rounds' : 'sets';
  document.getElementById('kpiCount').innerText = series && series.count > 0 ? `${series.count} ${unitLabel}` : '0';
  document.getElementById('kpiMean').innerText = series && series.count > 0 ? `${series.mean_ms.toFixed(1)} ms` : '—';
  document.getElementById('kpiMedian').innerText = series && series.count > 0 ? `${series.median_ms.toFixed(1)} ms` : '—';
  document.getElementById('kpiStd').innerText = series && series.count > 0 ? `±${series.std_ms.toFixed(1)} ms` : '—';
  document.getElementById('kpiP10').innerText = series && series.count > 0 ? `${series.p10_ms.toFixed(1)} ms` : '—';
  document.getElementById('kpiMin').innerText = series && series.count > 0 ? `${series.min_ms.toFixed(1)} ms` : '—';

  const dpr = window.devicePixelRatio || 1;
  const cssW = canvas.clientWidth || 960;
  const cssH = canvas.clientHeight || 330;
  canvas.width = Math.floor(cssW * dpr);
  canvas.height = Math.floor(cssH * dpr);

  const ctx = canvas.getContext('2d');
  ctx.save();
  ctx.scale(dpr, dpr);

  ctx.fillStyle = '#07111c';
  ctx.fillRect(0, 0, cssW, cssH);

  const padL = 52, padR = 24, padT = 28, padB = 34;
  const plotW = cssW - padL - padR;
  const plotH = cssH - padT - padB;
  const baseY = padT + plotH;

  const minMs = 40.0;
  const maxMs = 380.0;
  const msToX = (ms) => padL + Math.max(0, Math.min(1, (ms - minMs) / (maxMs - minMs))) * plotW;

  // Grid lines & X-axis labels
  ctx.strokeStyle = 'rgba(35, 65, 96, 0.55)';
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 4]);
  for (let g = 0; g <= 4; g++) {
    const gy = padT + (plotH * g) / 4;
    ctx.beginPath();
    ctx.moveTo(padL, gy);
    ctx.lineTo(padL + plotW, gy);
    ctx.stroke();
  }
  ctx.font = '600 11.5px Inter, system-ui, sans-serif';
  ctx.fillStyle = '#8da2b5';
  ctx.textAlign = 'center';
  for (let ms = 50; ms <= 350; ms += 50) {
    const gx = msToX(ms);
    ctx.beginPath();
    ctx.moveTo(gx, padT);
    ctx.lineTo(gx, baseY);
    ctx.stroke();
    ctx.fillText(`${ms} ms`, gx, baseY + 18);
  }
  ctx.setLineDash([]);

  if (!series || series.count === 0) {
    ctx.fillStyle = '#8da2b5';
    ctx.font = '600 15px Inter, system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('No verified database scores yet — complete a 5-round set in the client to populate the Bell Curve!', padL + plotW / 2, padT + plotH / 2);
    ctx.restore();
    return;
  }

  const evalDbPdf = (x) => {
    if (curveFitMode === 'gaussian') {
      return evalGaussianPdf(x, series.mean_ms, series.curve_std_ms);
    }
    return evalLogNormalPdf(x, series.log_mu, series.log_sigma);
  };

  // Compute peak density for vertical scaling
  let maxDensity = 0.004;
  for (let x = minMs; x <= maxMs; x += 2.0) {
    maxDensity = Math.max(maxDensity, evalDbPdf(x), evalGlobalRefPdf(x));
  }
  if (series.bins) {
    for (const b of series.bins) {
      maxDensity = Math.max(maxDensity, b.density);
    }
  }
  maxDensity *= 1.22; // Headroom at top for player callout badges

  const densityToY = (d) => baseY - Math.min(1.0, d / maxDensity) * plotH;

  // Y-axis relative density labels
  ctx.textAlign = 'right';
  ctx.fillStyle = '#6c859d';
  ctx.font = '500 11px Inter, system-ui, sans-serif';
  for (let g = 0; g <= 4; g++) {
    const val = maxDensity * (4 - g) / 4;
    const gy = padT + (plotH * g) / 4;
    ctx.fillText((val * 100).toFixed(2) + '%', padL - 8, gy + 4);
  }

  // 1. Draw ±1σ Standard Deviation Shaded Band
  const sLeft = Math.max(minMs, series.mean_ms - series.curve_std_ms);
  const sRight = Math.min(maxMs, series.mean_ms + series.curve_std_ms);
  ctx.fillStyle = 'rgba(56, 239, 125, 0.08)';
  ctx.fillRect(msToX(sLeft), padT, msToX(sRight) - msToX(sLeft), plotH);

  // 2. Draw Empirical Histogram Bars from DB
  if (series.bins) {
    for (const b of series.bins) {
      if (b.count <= 0) continue;
      const x0 = msToX(b.bin_start) + 1;
      const x1 = msToX(b.bin_end) - 1;
      const by = densityToY(b.density);
      const bh = baseY - by;
      ctx.fillStyle = 'rgba(86, 204, 242, 0.22)';
      ctx.fillRect(x0, by, Math.max(2, x1 - x0), bh);
      ctx.strokeStyle = 'rgba(86, 204, 242, 0.55)';
      ctx.lineWidth = 1;
      ctx.strokeRect(x0, by, Math.max(2, x1 - x0), bh);
    }
  }

  // 3. Draw Global Reference Curve (dashed slate/cyan)
  ctx.save();
  ctx.strokeStyle = 'rgba(141, 162, 181, 0.55)';
  ctx.lineWidth = 1.8;
  ctx.setLineDash([6, 4]);
  ctx.beginPath();
  for (let px = 0; px <= plotW; px += 2) {
    const ms = minMs + (px / plotW) * (maxMs - minMs);
    const py = densityToY(evalGlobalRefPdf(ms));
    if (px === 0) ctx.moveTo(padL + px, py);
    else ctx.lineTo(padL + px, py);
  }
  ctx.stroke();
  ctx.restore();

  // 4. Draw Database-Calculated Bell Curve (gradient area fill + smooth stroke)
  const grad = ctx.createLinearGradient(0, padT, 0, baseY);
  grad.addColorStop(0, 'rgba(56, 239, 125, 0.36)');
  grad.addColorStop(1, 'rgba(56, 239, 125, 0.02)');

  ctx.beginPath();
  ctx.moveTo(padL, baseY);
  for (let px = 0; px <= plotW; px += 2) {
    const ms = minMs + (px / plotW) * (maxMs - minMs);
    const py = densityToY(evalDbPdf(ms));
    ctx.lineTo(padL + px, py);
  }
  ctx.lineTo(padL + plotW, baseY);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  ctx.beginPath();
  for (let px = 0; px <= plotW; px += 2) {
    const ms = minMs + (px / plotW) * (maxMs - minMs);
    const py = densityToY(evalDbPdf(ms));
    if (px === 0) ctx.moveTo(padL + px, py);
    else ctx.lineTo(padL + px, py);
  }
  ctx.strokeStyle = '#38ef7d';
  ctx.lineWidth = 2.8;
  ctx.stroke();

  // 5. Draw DB Mean (μ) and Median (P50) vertical markers + Unique Player markers
  const markers = [];
  markers.push({
    ms: series.mean_ms,
    label: `DB Mean: ${series.mean_ms.toFixed(1)}ms`,
    color: '#38ef7d',
    priority: 1
  });
  if (Math.abs(series.median_ms - series.mean_ms) >= 3.5) {
    markers.push({
      ms: series.median_ms,
      label: `Median: ${series.median_ms.toFixed(1)}ms`,
      color: '#56ccf2',
      priority: 2
    });
  }

  players.slice(0, 5).forEach((p, idx) => {
    const val = (distMode === 'rounds') ? p.best_single_ms : p.best_avg_ms;
    markers.push({
      ms: val,
      label: `#${idx + 1} ${p.nickname}: ${val.toFixed(1)}ms`,
      color: idx === 0 ? '#ffd166' : '#ff758f',
      priority: 0
    });
  });

  const tiers = [[], [], [], []];
  ctx.font = '700 11.5px Inter, system-ui, sans-serif';

  for (const m of markers) {
    const mx = msToX(m.ms);
    const curveY = densityToY(evalDbPdf(m.ms));

    ctx.save();
    ctx.strokeStyle = m.color;
    ctx.lineWidth = m.priority === 0 ? 2.0 : 1.5;
    if (m.priority > 0) ctx.setLineDash([4, 3]);
    ctx.beginPath();
    ctx.moveTo(mx, padT + 22);
    ctx.lineTo(mx, baseY);
    ctx.stroke();
    ctx.restore();

    ctx.beginPath();
    ctx.arc(mx, curveY, 4.5, 0, Math.PI * 2);
    ctx.fillStyle = m.color;
    ctx.fill();
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = '#07111c';
    ctx.stroke();

    const tw = ctx.measureText(m.label).width;
    const pw = tw + 14;
    const ph = 20;
    const px0 = Math.max(padL + 4, Math.min(padL + plotW - pw - 4, mx - pw / 2));
    const px1 = px0 + pw;

    let chosenTier = 0;
    for (let t = 0; t < tiers.length; t++) {
      const overlap = tiers[t].some(sp => !(px1 + 6 < sp.x0 || px0 - 6 > sp.x1));
      if (!overlap) {
        chosenTier = t;
        break;
      }
      chosenTier = t;
    }
    tiers[chosenTier].push({ x0: px0, x1: px1 });
    const py = padT + 6 + chosenTier * 24;

    ctx.fillStyle = 'rgba(10, 21, 34, 0.94)';
    ctx.strokeStyle = m.color;
    ctx.lineWidth = 1.3;
    ctx.beginPath();
    ctx.roundRect(px0, py, pw, ph, 5);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = m.color;
    ctx.textAlign = 'center';
    ctx.fillText(m.label, px0 + pw / 2, py + 14);
  }

  ctx.restore();
}

window.addEventListener('resize', renderDbBellCurve);
window.addEventListener('DOMContentLoaded', renderDbBellCurve);

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function rankBadgeHtml(rank) {
  if (rank === 1) return '<span class="rank rank-1">#1</span>';
  if (rank === 2) return '<span class="rank rank-2">#2</span>';
  if (rank === 3) return '<span class="rank rank-3">#3</span>';
  return `<span class="rank">#${Number(rank)}</span>`;
}

async function loadLeaderboardPages(avgP, singleP, perP) {
  try {
    const resp = await fetch(`/api/v1/leaderboard?avg_page=${avgP}&single_page=${singleP}&per_page=${perP}`);
    if (!resp.ok) return;
    const data = await resp.json();
    currentAvgPage = data.avg_page;
    currentSinglePage = data.single_page;
    currentPerPage = data.per_page;
    totalPages = data.total_pages;
    if (data.distribution) {
      dbDistribution = data.distribution;
      renderDbBellCurve();
    }

    const avgHtml = data.top_averages.map(r => `
      <tr>
        <td>${rankBadgeHtml(r.rank)}</td>
        <td class="player">${escapeHtml(r.nickname)}</td>
        <td class="score-primary">${Number(r.avg_ms).toFixed(2)} ms</td>
        <td class="score-sub">${Number(r.best_ms).toFixed(2)} ms</td>
        <td><code title="${escapeHtml(r.entry_hash)}">${escapeHtml((r.entry_hash || '').slice(0, 12))}…</code></td>
        <td><span class="pill ${r.signature_valid ? 'pill-ok' : 'pill-bad'}">${r.signature_valid ? 'SIGNED' : 'TAMPERED'}</span></td>
        <td class="muted">${escapeHtml(r.timestamp)}</td>
      </tr>
    `).join('') || '<tr><td colspan="7" class="empty">No verified 5-round sets submitted yet.</td></tr>';

    const singleHtml = data.top_singles.map(r => `
      <tr>
        <td>${rankBadgeHtml(r.rank)}</td>
        <td class="player">${escapeHtml(r.nickname)}</td>
        <td class="score-primary">${Number(r.best_ms).toFixed(2)} ms</td>
        <td class="score-sub">${Number(r.avg_ms).toFixed(2)} ms</td>
        <td><code title="${escapeHtml(r.entry_hash)}">${escapeHtml((r.entry_hash || '').slice(0, 12))}…</code></td>
        <td><span class="pill ${r.signature_valid ? 'pill-ok' : 'pill-bad'}">${r.signature_valid ? 'SIGNED' : 'TAMPERED'}</span></td>
        <td class="muted">${escapeHtml(r.timestamp)}</td>
      </tr>
    `).join('') || '<tr><td colspan="7" class="empty">No verified single rounds submitted yet.</td></tr>';

    document.getElementById('avgTableBody').innerHTML = avgHtml;
    document.getElementById('singleTableBody').innerHTML = singleHtml;

    document.getElementById('avgPageLabel').innerText = `Page ${currentAvgPage} / ${totalPages}`;
    document.getElementById('singlePageLabel').innerText = `Page ${currentSinglePage} / ${totalPages}`;
    document.getElementById('avgCountInfo').innerText = `Total Verified Sets: ${data.total_entries}`;
    document.getElementById('singleCountInfo').innerText = `Total Verified Sets: ${data.total_entries}`;

    document.getElementById('avgFirstBtn').disabled = (currentAvgPage <= 1);
    document.getElementById('avgPrevBtn').disabled = (currentAvgPage <= 1);
    document.getElementById('avgNextBtn').disabled = (currentAvgPage >= totalPages);
    document.getElementById('avgLastBtn').disabled = (currentAvgPage >= totalPages);

    document.getElementById('singleFirstBtn').disabled = (currentSinglePage <= 1);
    document.getElementById('singlePrevBtn').disabled = (currentSinglePage <= 1);
    document.getElementById('singleNextBtn').disabled = (currentSinglePage >= totalPages);
    document.getElementById('singleLastBtn').disabled = (currentSinglePage >= totalPages);

    const url = new URL(window.location);
    url.searchParams.set('avg_page', currentAvgPage);
    url.searchParams.set('single_page', currentSinglePage);
    url.searchParams.set('per_page', currentPerPage);
    window.history.replaceState({}, '', url);
  } catch (e) {
    console.error('Pagination error:', e);
  }
}

function changeAvgPage(p) {
  if (p < 1 || p > totalPages) return;
  loadLeaderboardPages(p, currentSinglePage, currentPerPage);
}

function changeSinglePage(p) {
  if (p < 1 || p > totalPages) return;
  loadLeaderboardPages(currentAvgPage, p, currentPerPage);
}

function changePerPage(perP) {
  loadLeaderboardPages(1, 1, parseInt(perP, 10) || 10);
}

function toggleKeyVis() {
  const el = document.getElementById('keyInput');
  el.type = (el.type === 'password') ? 'text' : 'password';
}

async function runKeyAudit() {
  const keyHex = document.getElementById('keyInput').value.trim();
  const box = document.getElementById('auditResult');
  box.style.display = 'block';
  if (keyHex.length !== 64) {
    box.innerHTML = '<span style="color:#ff4757;font-weight:700;">Error: Key must be exactly 64 hexadecimal characters.</span>';
    return;
  }
  box.innerHTML = '<span style="color:#90e0ef;">Running cryptographic Ed25519 re-signing & SHA-256 chain audit...</span>';
  try {
    const resp = await fetch('/api/v1/audit/verify-key', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ key_hex: keyHex })
    });
    const data = await resp.json();
    if (!resp.ok) {
      box.innerHTML = `<span style="color:#ff4757;font-weight:700;">Error: ${escapeHtml(data.error || 'Verification failed')}</span>`;
      return;
    }
    if (!data.found_player) {
      box.innerHTML = `
        <div style="color:#ffd166;font-weight:700;margin-bottom:6px;">Key Valid — No Scores Found Yet</div>
        <div style="font-size:13px;">Derived Ed25519 Public Key: <code class="hash-full">${escapeHtml(data.derived_public_key)}</code></div>
        <div style="font-size:13px;color:var(--muted);margin-top:6px;">${escapeHtml(data.message)}</div>
      `;
      return;
    }
    let rowsHtml = '';
    for (const s of data.verified_scores) {
      const matchBadge = (s.private_key_resign_exact_match === true)
        ? '<span class="pill pill-ok">✓ PRIVATE KEY RE-SIGN MATCH</span>'
        : (s.public_key_verified ? '<span class="pill pill-ok">✓ PUBLIC KEY VERIFIED</span>' : '<span class="pill pill-bad">✗ INVALID SIGNATURE</span>');
      const chainBadge = (s.entry_hash_valid && s.in_verified_chain)
        ? '<span class="pill pill-ok">✓ HASH CHAIN LINK INTACT</span>'
        : '<span class="pill pill-bad">✗ BROKEN HASH LINK</span>';
      rowsHtml += `
        <div class="block-item" style="margin-top:10px;">
          <div class="block-header">
            <div>
              <span class="block-id">Block #${Number(s.block_id)}</span>
              <strong style="margin-left:10px;color:#38ef7d;">Avg: ${Number(s.avg_ms)} ms</strong>
              <span style="margin-left:10px;color:#ffd166;">Best: ${Number(s.best_ms)} ms</span>
              <span style="margin-left:10px;color:#8da2b5;font-size:12px;">[${s.rounds_ms.map(Number).join('ms, ')}ms]</span>
            </div>
            <div style="display:flex;gap:8px;align-items:center;">
              ${matchBadge} ${chainBadge}
            </div>
          </div>
          <div class="hash-grid">
            <div><span class="hash-label">Entry Hash:</span> <code class="hash-full">${escapeHtml(s.entry_hash)}</code></div>
            <div><span class="hash-label">Prev Hash:</span> <code class="hash-full">${escapeHtml(s.prev_hash)}</code></div>
            <div><span class="hash-label">Ed25519 Signature:</span> <code class="hash-full">${escapeHtml(s.ed25519_signature)}</code></div>
            <div><span class="hash-label">Signed Payload:</span> <code class="hash-full">${escapeHtml(s.canonical_payload)}</code></div>
          </div>
        </div>
      `;
    }
    box.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:10px;">
        <div>
          <strong style="color:#38ef7d;font-size:15px;">✓ Verified Player: ${escapeHtml(data.nickname)} (${escapeHtml(data.player_id)})</strong>
          <div style="font-size:12.5px;color:var(--muted);">Mode: ${escapeHtml(data.key_type)} | Public Key: <code class="hash-full">${escapeHtml(data.derived_public_key)}</code></div>
        </div>
        <div class="pill pill-ok">Total Verified Sets: ${Number(data.total_player_scores)}</div>
      </div>
      ${rowsHtml || '<div class="empty">Player registered, but no 5-round sets submitted yet.</div>'}
    `;
  } catch (err) {
    box.innerHTML = `<span style="color:#ff4757;">Request error: ${escapeHtml(err)}</span>`;
  }
}
