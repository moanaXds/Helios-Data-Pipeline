<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Helios — Data Pipeline</title>
<link href="https://fonts.googleapis.com/css2?family=Syne:wght@400;700;800&family=JetBrains+Mono:wght@300;400;600&display=swap" rel="stylesheet">
<style>
  :root {
    --bg: #050810;
    --panel: #0c1020;
    --border: #1a2540;
    --orange: #ff6b2b;
    --amber: #ffb347;
    --gold: #ffd166;
    --blue: #4fc3f7;
    --teal: #26c6da;
    --text: #e8eaf0;
    --muted: #6b7a99;
    --glow: rgba(255,107,43,0.25);
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }

  html { scroll-behavior: smooth; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'JetBrains Mono', monospace;
    overflow-x: hidden;
    cursor: none;
  }

  /* Custom cursor */
  .cursor {
    position: fixed;
    width: 12px; height: 12px;
    background: var(--orange);
    border-radius: 50%;
    pointer-events: none;
    z-index: 9999;
    transform: translate(-50%,-50%);
    transition: transform 0.1s, opacity 0.2s;
    mix-blend-mode: exclusion;
  }
  .cursor-ring {
    position: fixed;
    width: 36px; height: 36px;
    border: 1px solid rgba(255,107,43,0.5);
    border-radius: 50%;
    pointer-events: none;
    z-index: 9998;
    transform: translate(-50%,-50%);
    transition: transform 0.15s ease-out;
  }

  /* Stars */
  .stars {
    position: fixed;
    inset: 0;
    z-index: 0;
    pointer-events: none;
    overflow: hidden;
  }
  .star {
    position: absolute;
    background: white;
    border-radius: 50%;
    animation: twinkle var(--d, 3s) var(--delay, 0s) infinite alternate;
  }
  @keyframes twinkle {
    from { opacity: 0.1; transform: scale(1); }
    to   { opacity: 0.7; transform: scale(1.4); }
  }

  /* Grid overlay */
  .grid-overlay {
    position: fixed;
    inset: 0;
    z-index: 0;
    background-image:
      linear-gradient(rgba(255,107,43,0.03) 1px, transparent 1px),
      linear-gradient(90deg, rgba(255,107,43,0.03) 1px, transparent 1px);
    background-size: 60px 60px;
    pointer-events: none;
  }

  /* Layout */
  .container {
    max-width: 900px;
    margin: 0 auto;
    padding: 0 24px;
    position: relative;
    z-index: 1;
  }

  /* ── HERO ── */
  .hero {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    text-align: center;
    position: relative;
    padding: 80px 24px 60px;
  }

  .hero-badge {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    border: 1px solid var(--border);
    background: rgba(255,107,43,0.08);
    border-radius: 999px;
    padding: 6px 16px;
    font-size: 11px;
    letter-spacing: 0.15em;
    text-transform: uppercase;
    color: var(--orange);
    margin-bottom: 40px;
    animation: fadeDown 0.8s ease both;
  }
  .hero-badge::before {
    content: '';
    width: 6px; height: 6px;
    background: var(--orange);
    border-radius: 50%;
    animation: pulse 1.5s infinite;
  }
  @keyframes pulse {
    0%,100% { opacity: 1; box-shadow: 0 0 0 0 var(--glow); }
    50% { opacity: 0.6; box-shadow: 0 0 0 6px transparent; }
  }

  .hero-title {
    font-family: 'Syne', sans-serif;
    font-size: clamp(56px, 10vw, 110px);
    font-weight: 800;
    line-height: 0.95;
    letter-spacing: -0.03em;
    animation: fadeDown 0.8s 0.1s ease both;
    position: relative;
  }
  .hero-title .line1 { display: block; color: var(--text); }
  .hero-title .line2 {
    display: block;
    background: linear-gradient(135deg, var(--orange), var(--amber), var(--gold));
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
    filter: drop-shadow(0 0 40px rgba(255,107,43,0.4));
  }

  .hero-sub {
    margin-top: 24px;
    font-size: 13px;
    color: var(--muted);
    letter-spacing: 0.05em;
    max-width: 480px;
    line-height: 1.8;
    animation: fadeDown 0.8s 0.2s ease both;
  }

  .hero-tags {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 10px;
    margin-top: 36px;
    animation: fadeDown 0.8s 0.3s ease both;
  }
  .tag {
    border: 1px solid var(--border);
    background: var(--panel);
    padding: 6px 14px;
    border-radius: 4px;
    font-size: 11px;
    color: var(--muted);
    transition: all 0.2s;
  }
  .tag:hover {
    border-color: var(--orange);
    color: var(--orange);
    background: rgba(255,107,43,0.08);
  }

  .hero-cta {
    display: flex;
    gap: 12px;
    margin-top: 48px;
    animation: fadeDown 0.8s 0.4s ease both;
  }
  .btn-primary {
    background: var(--orange);
    color: #000;
    border: none;
    padding: 12px 28px;
    font-family: 'Syne', sans-serif;
    font-weight: 700;
    font-size: 13px;
    letter-spacing: 0.05em;
    border-radius: 4px;
    cursor: none;
    transition: all 0.2s;
    text-decoration: none;
  }
  .btn-primary:hover { background: var(--amber); transform: translateY(-1px); }
  .btn-ghost {
    border: 1px solid var(--border);
    background: transparent;
    color: var(--muted);
    padding: 12px 28px;
    font-family: 'Syne', sans-serif;
    font-weight: 700;
    font-size: 13px;
    letter-spacing: 0.05em;
    border-radius: 4px;
    cursor: none;
    transition: all 0.2s;
    text-decoration: none;
  }
  .btn-ghost:hover { border-color: var(--orange); color: var(--orange); transform: translateY(-1px); }

  /* Hero glow orb */
  .hero-orb {
    position: absolute;
    width: 500px; height: 500px;
    background: radial-gradient(circle, rgba(255,107,43,0.12) 0%, transparent 70%);
    border-radius: 50%;
    top: 50%; left: 50%;
    transform: translate(-50%, -55%);
    pointer-events: none;
    animation: breathe 4s ease-in-out infinite;
  }
  @keyframes breathe {
    0%,100% { transform: translate(-50%,-55%) scale(1); }
    50% { transform: translate(-50%,-55%) scale(1.1); }
  }

  /* Scroll indicator */
  .scroll-hint {
    position: absolute;
    bottom: 32px;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 8px;
    font-size: 10px;
    letter-spacing: 0.15em;
    text-transform: uppercase;
    color: var(--muted);
    animation: fadeDown 1s 0.8s ease both;
  }
  .scroll-line {
    width: 1px;
    height: 40px;
    background: linear-gradient(to bottom, var(--orange), transparent);
    animation: scrollPulse 1.5s ease-in-out infinite;
  }
  @keyframes scrollPulse {
    0%,100% { opacity: 0.3; }
    50% { opacity: 1; }
  }

  /* ── PIPELINE DIAGRAM ── */
  .section {
    padding: 80px 0;
    position: relative;
    z-index: 1;
  }

  .section-label {
    font-size: 10px;
    letter-spacing: 0.2em;
    text-transform: uppercase;
    color: var(--orange);
    margin-bottom: 12px;
  }

  .section-title {
    font-family: 'Syne', sans-serif;
    font-size: clamp(28px, 5vw, 44px);
    font-weight: 800;
    line-height: 1.1;
    margin-bottom: 48px;
  }

  .pipeline-wrapper {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 48px 32px;
    position: relative;
    overflow: hidden;
  }
  .pipeline-wrapper::before {
    content: '';
    position: absolute;
    inset: 0;
    background: linear-gradient(135deg, rgba(255,107,43,0.05) 0%, transparent 60%);
    pointer-events: none;
  }

  .pipeline-nodes {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0;
    flex-wrap: wrap;
    row-gap: 24px;
  }

  .p-node {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 10px;
    flex: 0 0 auto;
  }

  .node-box {
    border: 1px solid var(--border);
    background: var(--bg);
    border-radius: 8px;
    padding: 20px 24px;
    text-align: center;
    position: relative;
    transition: all 0.3s;
    cursor: none;
    min-width: 130px;
  }
  .node-box:hover {
    border-color: var(--orange);
    box-shadow: 0 0 24px var(--glow);
    transform: translateY(-3px);
  }
  .node-box.active {
    border-color: var(--orange);
    animation: nodeGlow 2s ease-in-out infinite;
  }
  @keyframes nodeGlow {
    0%,100% { box-shadow: 0 0 12px rgba(255,107,43,0.3); }
    50% { box-shadow: 0 0 28px rgba(255,107,43,0.6); }
  }
  .node-icon { font-size: 24px; margin-bottom: 6px; }
  .node-name {
    font-family: 'Syne', sans-serif;
    font-weight: 700;
    font-size: 13px;
    color: var(--text);
  }
  .node-role { font-size: 10px; color: var(--muted); margin-top: 4px; }

  .p-arrow {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
    padding: 0 8px;
  }
  .arrow-line {
    width: 48px;
    height: 1px;
    background: linear-gradient(90deg, var(--border), var(--orange), var(--border));
    position: relative;
  }
  .arrow-dot {
    width: 6px; height: 6px;
    background: var(--orange);
    border-radius: 50%;
    animation: slideDot 2s linear infinite;
    position: absolute;
    top: -3px;
  }
  @keyframes slideDot {
    from { left: 0; opacity: 0; }
    20%  { opacity: 1; }
    80%  { opacity: 1; }
    to   { left: calc(100% - 6px); opacity: 0; }
  }
  .arrow-label { font-size: 9px; color: var(--muted); letter-spacing: 0.1em; white-space: nowrap; }

  /* IPC row */
  .ipc-row {
    display: flex;
    justify-content: center;
    gap: 16px;
    margin-top: 40px;
    flex-wrap: wrap;
  }
  .ipc-chip {
    display: flex;
    align-items: center;
    gap: 8px;
    border: 1px solid var(--border);
    background: rgba(79,195,247,0.05);
    border-color: rgba(79,195,247,0.2);
    padding: 8px 16px;
    border-radius: 999px;
    font-size: 11px;
    color: var(--blue);
    transition: all 0.2s;
  }
  .ipc-chip:hover {
    background: rgba(79,195,247,0.12);
    border-color: var(--blue);
    transform: translateY(-2px);
  }
  .ipc-dot {
    width: 6px; height: 6px;
    background: var(--blue);
    border-radius: 50%;
    animation: pulse 2s infinite;
  }

  /* ── FEATURES GRID ── */
  .features-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    gap: 16px;
  }
  .feature-card {
    border: 1px solid var(--border);
    background: var(--panel);
    border-radius: 8px;
    padding: 24px;
    position: relative;
    overflow: hidden;
    transition: all 0.3s;
    opacity: 0;
    transform: translateY(20px);
  }
  .feature-card.revealed {
    opacity: 1;
    transform: translateY(0);
  }
  .feature-card::after {
    content: '';
    position: absolute;
    bottom: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, var(--orange), var(--amber));
    transform: scaleX(0);
    transform-origin: left;
    transition: transform 0.3s;
  }
  .feature-card:hover::after { transform: scaleX(1); }
  .feature-card:hover { border-color: rgba(255,107,43,0.4); transform: translateY(-3px); }
  .feat-icon { font-size: 22px; margin-bottom: 14px; }
  .feat-title {
    font-family: 'Syne', sans-serif;
    font-weight: 700;
    font-size: 14px;
    margin-bottom: 8px;
    color: var(--text);
  }
  .feat-desc { font-size: 11px; color: var(--muted); line-height: 1.8; }

  /* ── CODE BLOCK ── */
  .code-section { padding: 80px 0; }
  .code-tabs {
    display: flex;
    gap: 0;
    border-bottom: 1px solid var(--border);
    margin-bottom: 0;
  }
  .code-tab {
    padding: 10px 20px;
    font-size: 11px;
    color: var(--muted);
    cursor: none;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
    letter-spacing: 0.05em;
  }
  .code-tab.active { color: var(--orange); border-bottom-color: var(--orange); }
  .code-tab:hover { color: var(--text); }

  .code-panel {
    background: var(--panel);
    border: 1px solid var(--border);
    border-top: none;
    border-radius: 0 0 8px 8px;
    padding: 28px 24px;
    display: none;
    position: relative;
  }
  .code-panel.active { display: block; }
  .code-panel pre {
    font-family: 'JetBrains Mono', monospace;
    font-size: 12px;
    line-height: 1.9;
    color: var(--text);
    overflow-x: auto;
  }
  .kw { color: var(--orange); }
  .cm { color: var(--muted); }
  .st { color: var(--gold); }
  .fn { color: var(--blue); }
  .nm { color: var(--teal); }
  .copy-btn {
    position: absolute;
    top: 16px; right: 16px;
    background: var(--border);
    border: none;
    color: var(--muted);
    padding: 6px 12px;
    border-radius: 4px;
    font-family: 'JetBrains Mono', monospace;
    font-size: 10px;
    cursor: none;
    transition: all 0.2s;
    letter-spacing: 0.05em;
  }
  .copy-btn:hover { background: var(--orange); color: #000; }

  /* ── IPC TABLE ── */
  .ipc-table-wrap {
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
  }
  table { width: 100%; border-collapse: collapse; }
  thead { background: rgba(255,107,43,0.08); }
  th {
    padding: 14px 20px;
    text-align: left;
    font-size: 10px;
    letter-spacing: 0.15em;
    text-transform: uppercase;
    color: var(--orange);
    font-weight: 400;
  }
  td {
    padding: 14px 20px;
    font-size: 12px;
    color: var(--muted);
    border-top: 1px solid var(--border);
    transition: all 0.2s;
  }
  tr:hover td { background: rgba(255,107,43,0.04); color: var(--text); }
  td:first-child { color: var(--text); font-weight: 600; }
  td code {
    background: var(--bg);
    padding: 2px 8px;
    border-radius: 3px;
    font-size: 11px;
    color: var(--gold);
    border: 1px solid var(--border);
  }

  /* ── EDUCATIONAL VALUE ── */
  .edu-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 12px;
  }
  .edu-item {
    border: 1px solid var(--border);
    background: var(--panel);
    border-radius: 6px;
    padding: 16px 20px;
    display: flex;
    align-items: center;
    gap: 14px;
    transition: all 0.2s;
    opacity: 0;
    transform: translateX(-10px);
  }
  .edu-item.revealed { opacity: 1; transform: translateX(0); }
  .edu-item:hover { border-color: var(--teal); background: rgba(38,198,218,0.05); }
  .edu-num {
    font-family: 'Syne', sans-serif;
    font-weight: 800;
    font-size: 20px;
    color: rgba(255,107,43,0.3);
    min-width: 28px;
  }
  .edu-text { font-size: 11px; color: var(--muted); line-height: 1.6; }
  .edu-text strong { display: block; color: var(--text); font-size: 12px; margin-bottom: 2px; }

  /* ── FOOTER ── */
  .footer {
    border-top: 1px solid var(--border);
    padding: 48px 0;
    text-align: center;
    position: relative;
    z-index: 1;
  }
  .footer-title {
    font-family: 'Syne', sans-serif;
    font-size: 32px;
    font-weight: 800;
    background: linear-gradient(135deg, var(--orange), var(--amber));
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
    margin-bottom: 12px;
  }
  .footer-sub { font-size: 12px; color: var(--muted); line-height: 1.8; }
  .footer-links {
    display: flex;
    justify-content: center;
    gap: 24px;
    margin-top: 32px;
    flex-wrap: wrap;
  }
  .footer-link {
    font-size: 11px;
    color: var(--muted);
    text-decoration: none;
    letter-spacing: 0.1em;
    transition: color 0.2s;
  }
  .footer-link:hover { color: var(--orange); }

  /* ── DIVIDER ── */
  .divider {
    border: none;
    height: 1px;
    background: linear-gradient(90deg, transparent, var(--border), transparent);
    margin: 0;
  }

  /* ── ANIMATIONS ── */
  @keyframes fadeDown {
    from { opacity: 0; transform: translateY(-16px); }
    to   { opacity: 1; transform: translateY(0); }
  }

  /* Stat counters */
  .stats-row {
    display: flex;
    justify-content: center;
    gap: 0;
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    margin-top: 48px;
    animation: fadeDown 0.8s 0.5s ease both;
  }
  .stat {
    flex: 1;
    padding: 24px 20px;
    text-align: center;
    border-right: 1px solid var(--border);
    position: relative;
    overflow: hidden;
    transition: background 0.2s;
  }
  .stat:last-child { border-right: none; }
  .stat:hover { background: rgba(255,107,43,0.05); }
  .stat-num {
    font-family: 'Syne', sans-serif;
    font-size: 28px;
    font-weight: 800;
    color: var(--orange);
    display: block;
  }
  .stat-label { font-size: 10px; color: var(--muted); letter-spacing: 0.12em; text-transform: uppercase; margin-top: 4px; }

  /* Terminal animation */
  .terminal-wrap {
    background: #0a0d16;
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    font-family: 'JetBrains Mono', monospace;
  }
  .term-bar {
    background: var(--panel);
    border-bottom: 1px solid var(--border);
    padding: 10px 16px;
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .term-dot { width: 10px; height: 10px; border-radius: 50%; }
  .term-dot.r { background: #ff5f56; }
  .term-dot.y { background: #ffbd2e; }
  .term-dot.g { background: #27c93f; }
  .term-title { font-size: 11px; color: var(--muted); margin-left: 8px; }
  .term-body { padding: 20px 24px; min-height: 140px; }
  .term-line { font-size: 12px; line-height: 2; opacity: 0; }
  .term-line.show { opacity: 1; }
  .term-prompt { color: var(--orange); }
  .term-cmd { color: var(--text); }
  .term-out { color: var(--teal); }
  .term-ok { color: #27c93f; }
  .term-cursor {
    display: inline-block;
    width: 8px; height: 14px;
    background: var(--orange);
    vertical-align: middle;
    animation: blink 0.8s step-end infinite;
  }
  @keyframes blink { 0%,100% { opacity: 1; } 50% { opacity: 0; } }
</style>
</head>
<body>

<div class="cursor" id="cursor"></div>
<div class="cursor-ring" id="cursorRing"></div>
<div class="grid-overlay"></div>
<div class="stars" id="stars"></div>

<!-- ── HERO ── -->
<section class="hero">
  <div class="hero-orb"></div>
  <div class="hero-badge">⚡ OS Coursework · C++ · Unix/Linux</div>
  <h1 class="hero-title">
    <span class="line1">HELIOS</span>
    <span class="line2">DATA PIPELINE</span>
  </h1>
  <p class="hero-sub">
    A high-performance clickstream processing system built in C++, demonstrating IPC, process orchestration, and concurrent data processing on Unix/Linux.
  </p>
  <div class="hero-tags">
    <span class="tag">fork() / exec()</span>
    <span class="tag">Named Pipes · FIFO</span>
    <span class="tag">Shared Memory</span>
    <span class="tag">Semaphores</span>
    <span class="tag">Thread Pool</span>
    <span class="tag">sigaction()</span>
    <span class="tag">C++11</span>
    <span class="tag">POSIX</span>
  </div>
  <div class="hero-cta">
    <a class="btn-primary" href="#architecture">Explore Architecture</a>
    <a class="btn-ghost" href="#quickstart">Quick Start</a>
  </div>
  <div class="stats-row">
    <div class="stat">
      <span class="stat-num">4</span>
      <span class="stat-label">Processes</span>
    </div>
    <div class="stat">
      <span class="stat-num">3</span>
      <span class="stat-label">IPC Types</span>
    </div>
    <div class="stat">
      <span class="stat-num">N</span>
      <span class="stat-label">Worker Threads</span>
    </div>
    <div class="stat">
      <span class="stat-num">∞</span>
      <span class="stat-label">Scale</span>
    </div>
  </div>
  <div class="scroll-hint">
    <span>scroll</span>
    <div class="scroll-line"></div>
  </div>
</section>

<hr class="divider">

<!-- ── ARCHITECTURE ── -->
<section class="section" id="architecture">
  <div class="container">
    <p class="section-label">01 — Architecture</p>
    <h2 class="section-title">Four-process pipeline,<br>zero compromises.</h2>

    <div class="pipeline-wrapper">
      <div class="pipeline-nodes">
        <div class="p-node">
          <div class="node-box active">
            <div class="node-icon">🎛️</div>
            <div class="node-name">Dispatcher</div>
            <div class="node-role">Orchestrator</div>
          </div>
        </div>
        <div class="p-arrow">
          <div class="arrow-line"><div class="arrow-dot"></div></div>
          <div class="arrow-label">fork/exec</div>
        </div>
        <div class="p-node">
          <div class="node-box">
            <div class="node-icon">📥</div>
            <div class="node-name">Ingester</div>
            <div class="node-role">Data Source</div>
          </div>
        </div>
        <div class="p-arrow">
          <div class="arrow-line"><div class="arrow-dot"></div></div>
          <div class="arrow-label">FIFO</div>
        </div>
        <div class="p-node">
          <div class="node-box">
            <div class="node-icon">⚙️</div>
            <div class="node-name">Processor</div>
            <div class="node-role">Thread Pool</div>
          </div>
        </div>
        <div class="p-arrow">
          <div class="arrow-line"><div class="arrow-dot"></div></div>
          <div class="arrow-label">Shared Mem</div>
        </div>
        <div class="p-node">
          <div class="node-box">
            <div class="node-icon">📊</div>
            <div class="node-name">Reporter</div>
            <div class="node-role">Aggregator</div>
          </div>
        </div>
      </div>

      <div class="ipc-row">
        <div class="ipc-chip"><div class="ipc-dot"></div>/tmp/clickstream_pipe</div>
        <div class="ipc-chip"><div class="ipc-dot"></div>/clickstream_shm</div>
        <div class="ipc-chip"><div class="ipc-dot"></div>/clickstream_done_sem</div>
      </div>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── FEATURES ── -->
<section class="section" id="features">
  <div class="container">
    <p class="section-label">02 — Features</p>
    <h2 class="section-title">Built for real<br>OS coursework.</h2>

    <div class="features-grid" id="featGrid">
      <div class="feature-card">
        <div class="feat-icon">🧵</div>
        <div class="feat-title">Concurrent Processing</div>
        <div class="feat-desc">Configurable thread pool handles CPU-bound workloads with parallel worker threads and a bounded work queue.</div>
      </div>
      <div class="feature-card">
        <div class="feat-icon">🔗</div>
        <div class="feat-title">Clean IPC Architecture</div>
        <div class="feat-desc">Three distinct IPC mechanisms — FIFO, shared memory, and semaphores — each used correctly for their semantic purpose.</div>
      </div>
      <div class="feature-card">
        <div class="feat-icon">📡</div>
        <div class="feat-title">Robust Signal Handling</div>
        <div class="feat-desc">SIGINT, SIGTERM, SIGCHLD, SIGUSR1 handled via sigaction(). Graceful teardown propagates to all child processes.</div>
      </div>
      <div class="feature-card">
        <div class="feat-icon">🗂️</div>
        <div class="feat-title">Per-Process Logging</div>
        <div class="feat-desc">Each process writes its own structured log via dup2() redirection. Debug any component in isolation.</div>
      </div>
      <div class="feature-card">
        <div class="feat-icon">⚙️</div>
        <div class="feat-title">Runtime Configurable</div>
        <div class="feat-desc">Tune threads, queue size, and I/O paths at launch. No recompile needed for different workload profiles.</div>
      </div>
      <div class="feature-card">
        <div class="feat-icon">🌐</div>
        <div class="feat-title">Cross-Platform</div>
        <div class="feat-desc">Works on both macOS and Linux. Pure POSIX APIs — no platform-specific extensions required.</div>
      </div>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── QUICK START ── -->
<section class="section code-section" id="quickstart">
  <div class="container">
    <p class="section-label">03 — Quick Start</p>
    <h2 class="section-title">Zero to pipeline<br>in 4 commands.</h2>

    <div class="terminal-wrap" id="terminal">
      <div class="term-bar">
        <div class="term-dot r"></div>
        <div class="term-dot y"></div>
        <div class="term-dot g"></div>
        <span class="term-title">bash — helios</span>
      </div>
      <div class="term-body" id="termBody">
        <div class="term-line" id="t0"><span class="term-prompt">→ </span><span class="term-cmd">git clone https://github.com/moanaXds/helios</span></div>
        <div class="term-line" id="t1"><span class="term-prompt">→ </span><span class="term-cmd">make clean && make all</span></div>
        <div class="term-line" id="t2"><span class="term-out">  ✓ dispatcher  ✓ ingester  ✓ processor  ✓ reporter</span></div>
        <div class="term-line" id="t3"><span class="term-prompt">→ </span><span class="term-cmd">mkdir -p output && ./run.sh</span></div>
        <div class="term-line" id="t4"><span class="term-ok">  [dispatcher] forked ingester  PID=4821</span></div>
        <div class="term-line" id="t5"><span class="term-ok">  [dispatcher] forked processor  PID=4822</span></div>
        <div class="term-line" id="t6"><span class="term-ok">  [dispatcher] forked reporter   PID=4823</span></div>
        <div class="term-line" id="t7"><span class="term-out">  [reporter]   output/report.csv written ✓</span></div>
        <div class="term-line" id="t8"><span class="term-cursor"></span></div>
      </div>
    </div>

    <br>

    <div class="code-tabs">
      <div class="code-tab active" onclick="switchTab(this, 'tab-run')">run.sh</div>
      <div class="code-tab" onclick="switchTab(this, 'tab-make')">Makefile</div>
      <div class="code-tab" onclick="switchTab(this, 'tab-input')">Input CSV</div>
    </div>

    <div class="code-panel active" id="tab-run">
      <button class="copy-btn" onclick="copyCode(this)">copy</button>
      <pre><span class="cm">#!/bin/bash</span>
<span class="nm">INPUT</span>=<span class="st">${1:-./input}</span>
<span class="nm">OUTPUT</span>=<span class="st">${2:-./output}</span>
<span class="nm">THREADS</span>=<span class="st">${3:-4}</span>
<span class="nm">QUEUE</span>=<span class="st">${4:-8}</span>

<span class="fn">mkdir</span> -p <span class="st">"$OUTPUT"</span> logs
<span class="fn">./build/dispatcher</span> <span class="st">"$INPUT" "$OUTPUT" "$THREADS" "$QUEUE"</span></pre>
    </div>

    <div class="code-panel" id="tab-make">
      <button class="copy-btn" onclick="copyCode(this)">copy</button>
      <pre><span class="nm">CXX</span>    = g++
<span class="nm">CXXFLAGS</span> = -std=c++11 -Wall -pthread
<span class="nm">BUILD</span>  = build

<span class="fn">all</span>: dispatcher ingester processor reporter

<span class="fn">dispatcher</span>:
    <span class="st">$(CXX) $(CXXFLAGS) src/dispatcher.cpp -o $(BUILD)/dispatcher</span>

<span class="fn">clean</span>:
    <span class="st">rm -rf $(BUILD)/*</span></pre>
    </div>

    <div class="code-panel" id="tab-input">
      <button class="copy-btn" onclick="copyCode(this)">copy</button>
      <pre><span class="cm"># clickstream_batch_01.csv</span>
<span class="kw">user_id,session_id,event_type,timestamp,page_url</span>
<span class="st">user123,sess456,click,2024-05-25T10:30:45Z,/products</span>
<span class="st">user124,sess457,view,2024-05-25T10:31:12Z,/home</span>
<span class="st">user125,sess458,click,2024-05-25T10:31:58Z,/checkout</span></pre>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── IPC RESOURCES ── -->
<section class="section" id="ipc">
  <div class="container">
    <p class="section-label">04 — IPC Resources</p>
    <h2 class="section-title">Three channels.<br>One clean pipeline.</h2>

    <div class="ipc-table-wrap">
      <table>
        <thead>
          <tr>
            <th>Mechanism</th>
            <th>Path</th>
            <th>Purpose</th>
            <th>Cleanup</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>FIFO</td>
            <td><code>/tmp/clickstream_pipe</code></td>
            <td>Event streaming: Ingester → Processor</td>
            <td><code>rm -f /tmp/clickstream_pipe</code></td>
          </tr>
          <tr>
            <td>Shared Memory</td>
            <td><code>/clickstream_shm</code></td>
            <td>Shared state across all processes</td>
            <td><code>ipcrm -M clickstream_shm</code></td>
          </tr>
          <tr>
            <td>Semaphore</td>
            <td><code>/clickstream_done_sem</code></td>
            <td>Completion signal from Processor</td>
            <td><code>ipcrm -S clickstream_done_sem</code></td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── CONFIG ── -->
<section class="section">
  <div class="container">
    <p class="section-label">05 — Configuration</p>
    <h2 class="section-title">Tune at runtime.<br>No recompile.</h2>

    <div class="ipc-table-wrap">
      <table>
        <thead>
          <tr>
            <th>Parameter</th>
            <th>Default</th>
            <th>Tip</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>input_dir</td>
            <td><code>./input</code></td>
            <td>Any dir with .csv files works</td>
          </tr>
          <tr>
            <td>output_dir</td>
            <td><code>./output</code></td>
            <td>Auto-created by run.sh</td>
          </tr>
          <tr>
            <td>num_threads</td>
            <td><code>4</code></td>
            <td>Set to # of CPU cores for peak throughput</td>
          </tr>
          <tr>
            <td>queue_size</td>
            <td><code>8</code></td>
            <td>Increase if ingester consistently blocks</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── EDUCATIONAL ── -->
<section class="section">
  <div class="container">
    <p class="section-label">06 — What You'll Learn</p>
    <h2 class="section-title">Six core OS concepts.<br>One project.</h2>

    <div class="edu-grid" id="eduGrid">
      <div class="edu-item">
        <div class="edu-num">01</div>
        <div class="edu-text">
          <strong>Process Management</strong>
          fork(), exec(), waitpid() — the full lifecycle
        </div>
      </div>
      <div class="edu-item">
        <div class="edu-num">02</div>
        <div class="edu-text">
          <strong>Signal Handling</strong>
          sigaction(), sigsuspend(), graceful teardown
        </div>
      </div>
      <div class="edu-item">
        <div class="edu-num">03</div>
        <div class="edu-text">
          <strong>IPC Patterns</strong>
          FIFOs, shared memory, POSIX semaphores
        </div>
      </div>
      <div class="edu-item">
        <div class="edu-num">04</div>
        <div class="edu-text">
          <strong>Concurrency</strong>
          Thread pools, bounded queues, mutex/cond vars
        </div>
      </div>
      <div class="edu-item">
        <div class="edu-num">05</div>
        <div class="edu-text">
          <strong>System Logging</strong>
          Per-process log files via dup2() redirection
        </div>
      </div>
      <div class="edu-item">
        <div class="edu-num">06</div>
        <div class="edu-text">
          <strong>Resource Cleanup</strong>
          Robust teardown of IPC artifacts on any exit path
        </div>
      </div>
    </div>
  </div>
</section>

<hr class="divider">

<!-- ── FOOTER ── -->
<footer class="footer">
  <div class="container">
    <div class="footer-title">HELIOS</div>
    <p class="footer-sub">
      Educational OS project · C++11 · POSIX · MIT License<br>
      Built to understand how Unix systems actually work.
    </p>
    <div class="footer-links">
      <a class="footer-link" href="#">GitHub</a>
      <a class="footer-link" href="#">Report Issue</a>
      <a class="footer-link" href="#">Docs</a>
      <a class="footer-link" href="#">License</a>
    </div>
  </div>
</footer>

<script>
  // Stars
  const starsEl = document.getElementById('stars');
  for (let i = 0; i < 120; i++) {
    const s = document.createElement('div');
    s.className = 'star';
    const size = Math.random() * 2 + 0.5;
    s.style.cssText = `
      width:${size}px;height:${size}px;
      top:${Math.random()*100}%;left:${Math.random()*100}%;
      --d:${(Math.random()*3+2).toFixed(1)}s;
      --delay:${(Math.random()*4).toFixed(1)}s;
    `;
    starsEl.appendChild(s);
  }

  // Cursor
  const cursor = document.getElementById('cursor');
  const ring   = document.getElementById('cursorRing');
  let mx=0, my=0, rx=0, ry=0;
  document.addEventListener('mousemove', e => { mx=e.clientX; my=e.clientY; });
  (function animCursor() {
    cursor.style.left = mx+'px'; cursor.style.top = my+'px';
    rx += (mx-rx)*0.12; ry += (my-ry)*0.12;
    ring.style.left = rx+'px'; ring.style.top = ry+'px';
    requestAnimationFrame(animCursor);
  })();

  // Tab switching
  function switchTab(el, id) {
    document.querySelectorAll('.code-tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.code-panel').forEach(p => p.classList.remove('active'));
    el.classList.add('active');
    document.getElementById(id).classList.add('active');
  }

  // Copy
  function copyCode(btn) {
    const pre = btn.nextElementSibling;
    navigator.clipboard.writeText(pre.innerText);
    btn.textContent = 'copied!';
    setTimeout(() => btn.textContent = 'copy', 1600);
  }

  // Terminal type-in
  const lines = ['t0','t1','t2','t3','t4','t5','t6','t7','t8'];
  lines.forEach((id, i) => {
    setTimeout(() => {
      const el = document.getElementById(id);
      if (el) el.classList.add('show');
    }, 600 + i * 340);
  });

  // Scroll reveal
  const revealObs = new IntersectionObserver(entries => {
    entries.forEach(e => {
      if (e.isIntersecting) {
        const cards = e.target.querySelectorAll('.feature-card, .edu-item');
        cards.forEach((c, i) => {
          setTimeout(() => c.classList.add('revealed'), i * 80);
        });
        revealObs.unobserve(e.target);
      }
    });
  }, { threshold: 0.1 });

  const fg = document.getElementById('featGrid');
  const eg = document.getElementById('eduGrid');
  if (fg) revealObs.observe(fg);
  if (eg) revealObs.observe(eg);
</script>
</body>
</html>