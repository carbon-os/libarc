// samples/app/main_ipc_full.cpp
//
// End-to-end IPC capability showcase for libui + libwebview.
//
// Channels exercised
// ──────────────────
//   renderer → host  invoke  "ping"           {}                  → { pong: int, elapsed_ms: double }
//   renderer → host  invoke  "echo/text"      { text: str }       → { text: str, length: int }
//   renderer → host  invoke  "echo/binary"    <bytes>             → <same bytes reflected>
//   renderer → host  send    "log"            { level, text }     (fire-and-forget)
//   host     → rend  send    "host/tick"      { seq: int,         (every ~3 s)
//                                               time: str }
//   host     → rend  invoke  "host/query"     { question: str }   → { answer: str }
//   binary   host←→rend      "bin/upload"                        (renderer sends, host reflects)
//   binary   host→rend       "bin/push"                          (host pushes 8-byte counter frame)

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ui/ui.h>
#include <webview/webview.hpp>

// ── Embedded page ─────────────────────────────────────────────────────────────

static const std::string PAGE_HTML = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<title>libwebview — Full IPC Showcase</title>
<style>
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

:root {
  --bg:       rgba(245,245,247,0.85);
  --card:     rgba(255,255,255,0.78);
  --card-b:   rgba(0,0,0,0.07);
  --text:     #111;
  --muted:    #666;
  --accent:   #0a7aff;
  --danger:   #d9291a;
  --success:  #1a8a3e;
  --code-bg:  rgba(0,0,0,0.04);
  --radius:   14px;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: var(--bg);
  color: var(--text);
  min-height: 100vh;
  padding: 32px 24px 48px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
}

h1  { font-size: 1.3rem; font-weight: 700; letter-spacing: -0.02em; }
h2  { font-size: 0.72rem; text-transform: uppercase; letter-spacing: 0.1em;
      color: var(--muted); margin-bottom: 10px; }

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 16px;
  width: 100%;
  max-width: 860px;
}

.card {
  background: var(--card);
  border: 1px solid var(--card-b);
  border-radius: var(--radius);
  padding: 20px;
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  display: flex;
  flex-direction: column;
  gap: 10px;
  box-shadow: 0 2px 16px rgba(0,0,0,0.06);
}

.row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }

input[type="text"] {
  flex: 1;
  min-width: 120px;
  border: 1px solid rgba(0,0,0,0.14);
  border-radius: 8px;
  padding: 8px 12px;
  font-size: 0.88rem;
  background: rgba(255,255,255,0.9);
  outline: none;
  transition: border-color .15s;
}
input[type="text"]:focus { border-color: var(--accent); }

button {
  cursor: pointer;
  border: none;
  border-radius: 8px;
  padding: 8px 18px;
  font-size: 0.88rem;
  font-weight: 500;
  transition: opacity .15s, transform .1s;
  white-space: nowrap;
}
button:active { transform: scale(0.96); }

.btn-primary   { background: var(--accent); color: #fff; }
.btn-secondary { background: rgba(0,0,0,0.07); color: var(--text); }
.btn-success   { background: #dcf5e7; color: var(--success); }
.btn-danger    { background: rgba(255,59,48,0.12); color: var(--danger); }

.result {
  font-size: 0.82rem;
  color: var(--muted);
  background: var(--code-bg);
  border-radius: 8px;
  padding: 8px 12px;
  min-height: 36px;
  word-break: break-all;
  transition: color .2s;
}
.result.ok  { color: var(--success); }
.result.err { color: var(--danger);  }

/* Binary visualiser */
.hex-row {
  font-family: "SF Mono", Menlo, "Courier New", monospace;
  font-size: 0.76rem;
  color: #555;
  background: var(--code-bg);
  border-radius: 8px;
  padding: 8px 12px;
  min-height: 36px;
  word-break: break-all;
  line-height: 1.8;
}
.hex-row span { display: inline-block; margin-right: 4px; }
.hex-row span.hi { color: var(--accent); font-weight: 600; }

/* Log */
.log {
  background: rgba(0,0,0,0.035);
  border: 1px solid rgba(0,0,0,0.06);
  border-radius: var(--radius);
  padding: 10px 14px;
  font-size: 0.76rem;
  color: var(--muted);
  max-height: 130px;
  overflow-y: auto;
  display: flex;
  flex-direction: column-reverse;
  gap: 2px;
  width: 100%;
  max-width: 860px;
}
.log .entry {
  padding: 2px 0;
  border-bottom: 1px solid rgba(0,0,0,0.04);
  display: flex;
  gap: 6px;
}
.log .tag { font-weight: 600; min-width: 86px; }
.log .tag.rx   { color: #0a7aff; }
.log .tag.tx   { color: #8e44ad; }
.log .tag.bin  { color: #e67e22; }
.log .tag.info { color: var(--muted); }
</style>
</head>
<body>

<h1>libwebview — Full IPC Showcase</h1>

<div class="grid">

  <!-- ── Ping / Pong ───────────────────────────────── -->
  <div class="card">
    <h2>🏓 Ping / Pong (renderer → host invoke)</h2>
    <div class="row">
      <button class="btn-primary" id="btn-ping">Send Ping</button>
    </div>
    <div class="result" id="res-ping">Waiting…</div>
  </div>

  <!-- ── Text Echo ─────────────────────────────────── -->
  <div class="card">
    <h2>🔁 Text Echo (renderer → host invoke)</h2>
    <div class="row">
      <input type="text" id="in-echo" placeholder="Type anything…" value="Hello, IPC!"/>
      <button class="btn-primary" id="btn-echo">Echo</button>
    </div>
    <div class="result" id="res-echo">Waiting…</div>
  </div>

  <!-- ── Fire & Forget ─────────────────────────────── -->
  <div class="card">
    <h2>🔥 Fire &amp; Forget (renderer → host, no reply)</h2>
    <div class="row">
      <input type="text" id="in-log" placeholder="Log message…" value="test message"/>
      <button class="btn-secondary" id="btn-log-info">Info</button>
      <button class="btn-danger"    id="btn-log-warn">Warn</button>
    </div>
    <div class="result" id="res-log">Sent messages appear in host stdout and the event log below.</div>
  </div>

  <!-- ── Host → Renderer invoke ────────────────────── -->
  <div class="card">
    <h2>🤙 Host-Initiated Invoke (host → renderer)</h2>
    <p style="font-size:0.8rem;color:var(--muted)">
      Host asks the renderer a question; renderer replies from JS.
    </p>
    <div class="result" id="res-query">Waiting for host query…</div>
  </div>

  <!-- ── Binary Upload (renderer → host) ───────────── -->
  <div class="card">
    <h2>📦 Binary Upload (renderer → host invoke)</h2>
    <div class="row">
      <input type="text" id="in-bin" placeholder="Hex bytes e.g. DE AD BE EF" value="DE AD BE EF 01 02 03 04"/>
      <button class="btn-success" id="btn-upload">Upload</button>
    </div>
    <p style="font-size:0.75rem;color:var(--muted)">Sent →</p>
    <div class="hex-row" id="hex-sent">—</div>
    <p style="font-size:0.75rem;color:var(--muted)">Host reflection ←</p>
    <div class="hex-row" id="hex-recv">—</div>
  </div>

  <!-- ── Binary Push (host → renderer) ─────────────── -->
  <div class="card">
    <h2>📡 Binary Push (host → renderer, every 4 s)</h2>
    <p style="font-size:0.8rem;color:var(--muted)">
      Host pushes an 8-byte frame containing a sequence counter and timestamp.
    </p>
    <div class="hex-row" id="hex-push">Waiting for first push…</div>
  </div>

</div>

<!-- ── Host tick display ──────────────────────────────── -->
<div class="card" style="width:100%;max-width:860px;">
  <h2>⏱ Host Ticks (host → renderer fire-and-forget, every 3 s)</h2>
  <div class="row" id="tick-row" style="flex-wrap:wrap;gap:6px;"></div>
</div>

<!-- ── Event log ──────────────────────────────────────── -->
<div class="log" id="log">
  <div class="entry"><span class="tag info">info</span><span>IPC bridge ready.</span></div>
</div>

<script>
// ── Log helper ────────────────────────────────────────────────────────────────
const logEl = document.getElementById('log');
function addLog(tag, tagClass, text) {
  const e = document.createElement('div');
  e.className = 'entry';
  e.innerHTML = `<span class="tag ${tagClass}">${tag}</span><span>${text}</span>`;
  logEl.prepend(e);
  while (logEl.children.length > 40) logEl.removeChild(logEl.lastChild);
}

function setResult(id, text, cls = '') {
  const el = document.getElementById(id);
  el.textContent = text;
  el.className = 'result ' + cls;
}

// ── Hex renderer ──────────────────────────────────────────────────────────────
function hexSpans(buf, highlightFirst = 4) {
  const bytes = new Uint8Array(buf);
  return Array.from(bytes).map((b, i) => {
    const hex = b.toString(16).padStart(2, '0').toUpperCase();
    return `<span class="${i < highlightFirst ? 'hi' : ''}">${hex}</span>`;
  }).join('');
}

function parseHexInput(str) {
  const tokens = str.trim().split(/\s+/);
  const arr = [];
  for (const t of tokens) {
    const v = parseInt(t, 16);
    if (isNaN(v) || v < 0 || v > 255) throw new Error(`Bad byte: ${t}`);
    arr.push(v);
  }
  return new Uint8Array(arr).buffer;
}

// ── Ping / Pong ───────────────────────────────────────────────────────────────
document.getElementById('btn-ping').addEventListener('click', async () => {
  const t0 = performance.now();
  try {
    const res = await window.ipc.invoke('ping', {});
    const rtt = (performance.now() - t0).toFixed(2);
    setResult('res-ping',
      `pong #${res.pong}  |  host elapsed: ${res.elapsed_ms.toFixed(3)} ms  |  round-trip: ${rtt} ms`,
      'ok');
    addLog('rx  ping/pong', 'rx', `pong=${res.pong}, elapsed=${res.elapsed_ms.toFixed(3)} ms`);
  } catch(e) {
    setResult('res-ping', 'Error: ' + e, 'err');
  }
});

// ── Text Echo ─────────────────────────────────────────────────────────────────
document.getElementById('btn-echo').addEventListener('click', async () => {
  const text = document.getElementById('in-echo').value;
  try {
    const res = await window.ipc.invoke('echo/text', { text });
    setResult('res-echo', `"${res.text}"  (${res.length} chars)`, 'ok');
    addLog('rx  echo/text', 'rx', `length=${res.length}`);
  } catch(e) {
    setResult('res-echo', 'Error: ' + e, 'err');
  }
});
document.getElementById('in-echo')
  .addEventListener('keydown', e => { if (e.key === 'Enter') document.getElementById('btn-echo').click(); });

// ── Fire & Forget log ─────────────────────────────────────────────────────────
function sendLog(level) {
  const text = document.getElementById('in-log').value || '(empty)';
  window.ipc.send('log', { level, text });
  setResult('res-log', `Sent [${level}]: "${text}"`);
  addLog('tx  log', 'tx', `level=${level}, text="${text}"`);
}
document.getElementById('btn-log-info').addEventListener('click', () => sendLog('info'));
document.getElementById('btn-log-warn').addEventListener('click', () => sendLog('warn'));

// ── Host → Renderer invoke: answer questions from the host ────────────────────
window.ipc.handle('host/query', async (body) => {
  const q = body.question;
  addLog('rx  host/query', 'rx', `"${q}"`);
  document.getElementById('res-query').textContent = `Host asked: "${q}" → answering…`;
  // Simulate a short async lookup
  await new Promise(r => setTimeout(r, 80));
  const answer = `The answer to "${q}" is 42. (from renderer, ${Date.now()})`;
  document.getElementById('res-query').textContent = `✓ Replied: ${answer}`;
  addLog('tx  host/query', 'tx', answer.slice(0, 60));
  return { answer };
});

// ── Binary Upload ─────────────────────────────────────────────────────────────
document.getElementById('btn-upload').addEventListener('click', async () => {
  let buf;
  try {
    buf = parseHexInput(document.getElementById('in-bin').value);
  } catch(e) {
    document.getElementById('hex-sent').textContent = 'Parse error: ' + e.message;
    return;
  }
  document.getElementById('hex-sent').innerHTML = hexSpans(buf);
  document.getElementById('hex-recv').textContent = '…waiting…';
  addLog('tx  bin/upload', 'bin', `${buf.byteLength} bytes`);
  try {
    const reply = await window.ipc.invokeBinary('bin/upload', buf);
    document.getElementById('hex-recv').innerHTML = hexSpans(reply, buf.byteLength);
    addLog('rx  bin/upload', 'bin', `reflected ${reply.byteLength} bytes`);
  } catch(e) {
    document.getElementById('hex-recv').textContent = 'Error: ' + e;
  }
});

// ── Binary Push from host ─────────────────────────────────────────────────────
window.ipc.onBinary('bin/push', (buf) => {
  document.getElementById('hex-push').innerHTML = hexSpans(buf, 4);
  addLog('rx  bin/push', 'bin', `${buf.byteLength} bytes`);
});

// ── Host tick ─────────────────────────────────────────────────────────────────
const tickRow = document.getElementById('tick-row');
window.ipc.on('host/tick', (msg) => {
  const chip = document.createElement('div');
  chip.style.cssText =
    'background:rgba(10,122,255,0.1);color:#0a7aff;border-radius:6px;' +
    'padding:3px 10px;font-size:0.75rem;font-weight:500;';
  chip.textContent = `#${msg.seq} @ ${msg.time}`;
  tickRow.prepend(chip);
  while (tickRow.children.length > 8) tickRow.removeChild(tickRow.lastChild);
  addLog('rx  host/tick', 'rx', `seq=${msg.seq}, time=${msg.time}`);
});
</script>
</body>
</html>
)html";

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string current_time_string()
{
    const auto now  = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    return oss.str();
}

// Encode a buffer as "DE AD BE EF …" for stdout logging
static std::string hex_dump(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i) oss << ' ';
        oss << std::uppercase << std::hex
            << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

// Pack a uint32_t into 4 bytes (big-endian)
static void pack_u32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >>  8) & 0xFF);
    buf.push_back((v      ) & 0xFF);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main()
{
    // ── Window ────────────────────────────────────────────────────────────────
    ui::Window window(ui::WindowConfig{
        .title       = "Full IPC Showcase",
        .size        = {880, 780},
        .min_size    = ui::Size{600, 500},
        .resizable   = true,
        .style       = ui::WindowStyle::Default,
    });
    window.center();

    // ── WebView ───────────────────────────────────────────────────────────────
    auto ui_handle = window.native_handle();
    
    webview::WebView wv(
        webview::NativeHandle(
            ui_handle.get(), 
#if defined(_WIN32)
            webview::NativeHandleType::HWND      // Windows uses HWND
#elif defined(__APPLE__)
            webview::NativeHandleType::NSWindow  // macOS uses NSWindow
#else
            webview::NativeHandleType::GtkWidget // Linux uses GtkWindow
#endif
        ),
        webview::WebViewConfig{ 
            .devtools = true,
#if defined(_WIN32)
            // These will only be compiled on Windows
            .webview2_user_data_path = "C:\\MyApp\\Cache",
            .webview2_runtime_path   = "C:\\Users\\cloud\\Downloads\\webview2_runtime\\146.0.3856.97" // Leave empty to use the system Evergreen runtime
#endif
        }
    );

    // ── Shared state ──────────────────────────────────────────────────────────
    std::atomic<int> ping_counter{0};
    std::atomic<int> tick_seq{0};
    std::atomic<int> bin_push_seq{0};

    // =========================================================================
    // IPC handlers — renderer → host
    // =========================================================================

    // ── Ping / Pong ───────────────────────────────────────────────────────────
    // Renderer invokes, host records precise host-side elapsed time and replies.
    wv.ipc.handle("ping", [&](webview::Message& msg) {
        const auto t0 = std::chrono::steady_clock::now();

        // Simulate the tiniest bit of host work so elapsed_ms is non-zero
        std::this_thread::sleep_for(std::chrono::microseconds(200));

        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();

        const int n = ++ping_counter;
        std::cout << "[ping]  pong #" << n
                  << "  host elapsed: " << elapsed_ms << " ms\n";

        msg.reply({
            {"pong",       n},
            {"elapsed_ms", elapsed_ms}
        });
    });

    // ── Text Echo ─────────────────────────────────────────────────────────────
    // Reflect the text back plus its character count.
    wv.ipc.handle("echo/text", [](webview::Message& msg) {
        const std::string text = msg.body.value("text", "");
        std::cout << "[echo]  \"" << text << "\"  (" << text.size() << " chars)\n";
        msg.reply({
            {"text",   text},
            {"length", static_cast<int>(text.size())}
        });
    });

    // ── Fire-and-forget log ───────────────────────────────────────────────────
    // Renderer sends; host just prints — no reply needed.
    wv.ipc.on("log", [](webview::Message& msg) {
        const std::string level = msg.body.value("level", "info");
        const std::string text  = msg.body.value("text",  "");
        std::cout << "[log:" << level << "]  " << text << "\n";
    });

    // ── Binary Upload (reflect) ───────────────────────────────────────────────
    // Renderer sends arbitrary bytes; host logs them and reflects the same
    // bytes back as the reply payload so the renderer can compare.
    wv.ipc.handle("bin/upload", [](webview::BinaryMessage& msg) {
        std::cout << "[bin/upload]  " << msg.data.size()
                  << " bytes: " << hex_dump(msg.data) << "\n";
        // Reflect verbatim
        msg.reply(msg.data);
    });

    // =========================================================================
    // Background threads
    // =========================================================================

    // ── Host tick thread (JSON fire-and-forget every 3 s) ─────────────────────
    std::thread tick_thread([&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            const int seq = ++tick_seq;
            std::cout << "[host/tick]  seq=" << seq << "\n";
            wv.ipc.send("host/tick", {
                {"seq",  seq},
                {"time", current_time_string()}
            });
        }
    });
    tick_thread.detach();

    // ── Binary push thread (every 4 s) ────────────────────────────────────────
    // Pushes an 8-byte frame: [4-byte big-endian seq][4-byte unix timestamp]
    std::thread bin_push_thread([&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(4));
            const uint32_t seq  = static_cast<uint32_t>(++bin_push_seq);
            const uint32_t ts   = static_cast<uint32_t>(
                std::chrono::system_clock::now().time_since_epoch() /
                std::chrono::seconds(1));

            std::vector<uint8_t> frame;
            frame.reserve(8);
            pack_u32(frame, seq);
            pack_u32(frame, ts);

            std::cout << "[bin/push]  " << hex_dump(frame) << "\n";
            wv.ipc.send_binary("bin/push", frame);
        }
    });
    bin_push_thread.detach();

    // ── Host-initiated invoke thread (every 7 s) ──────────────────────────────
    // Host asks the renderer a question and waits for its async JS reply.
    // Questions cycle through a small list to vary the output.
    std::thread query_thread([&]() {
        static const char* questions[] = {
            "What is the meaning of life?",
            "How many roads must a man walk down?",
            "Is this the real life, or is it fantasy?",
            "Why is a raven like a writing desk?"
        };
        int qi = 0;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(7));
            const std::string q = questions[qi++ % 4];
            std::cout << "[host/query]  asking renderer: \"" << q << "\"\n";
            wv.ipc.invoke("host/query", {{"question", q}},
                [q](webview::Message& reply) {
                    const std::string answer =
                        reply.body.value("answer", "(no answer)");
                    std::cout << "[host/query]  renderer replied: \""
                              << answer << "\"\n";
                });
        }
    });
    query_thread.detach();

    // ── Load page ─────────────────────────────────────────────────────────────
    wv.load_html(PAGE_HTML);

    // ── Window close ──────────────────────────────────────────────────────────
    window.on_close([&]() -> bool { return true; });

    window.show();

    // ── Run ───────────────────────────────────────────────────────────────────
    window.run();
    return 0;
}