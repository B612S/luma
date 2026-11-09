// Resource Monitor: samples this Luma process (CPU/RSS), the system GPU, and
// draws three SVG charts on its own statistics page. The page is preferred on
// key 3, but the host may place it on the next free numeric key.

let luma;
let collecting = false;
let lastSampleAt = 0;
let previousTicks = -1;
let previousTime = 0;

const HISTORY = 60;
const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

function emptyHistory() {
    const values = [];
    for (let i = 0; i < HISTORY; ++i)
        values.push(0);
    return values;
}

let cpuHistory = emptyHistory();
let ramHistory = emptyHistory();
let gpuHistory = emptyHistory();
let lastCpu = 0;
let lastCpuSystem = 0;
let cpuCores = 1;
let lastRamMb = 0;
let lastRamPercent = 0;
let lastGpu = -1;
let lastGpuName = "";
let lastGpuPlayer = false;
let reportedPageError = "";

function base64(text) {
    let result = "";
    for (let i = 0; i < text.length; i += 3) {
        const a = text.charCodeAt(i);
        const b = i + 1 < text.length ? text.charCodeAt(i + 1) : 0;
        const c = i + 2 < text.length ? text.charCodeAt(i + 2) : 0;
        const triple = (a << 16) | (b << 8) | c;
        result += B64[(triple >> 18) & 63];
        result += B64[(triple >> 12) & 63];
        result += i + 1 < text.length ? B64[(triple >> 6) & 63] : "=";
        result += i + 2 < text.length ? B64[triple & 63] : "=";
    }
    return result;
}

function push(history, value) {
    history.push(value);
    if (history.length > HISTORY)
        history.shift();
}

function parseNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : 0;
}

function processMetrics(text) {
    const metrics = { ticks: -1, hz: 100, cores: 1, rssKb: 0, totalKb: 0,
                      gpu: -1, gpuName: "", gpuNode: "", gpuPlayer: false,
                      pmon: [] };
    const used = [];
    const nvidia = [];
    let usedNvidia = false;
    for (const line of text.split("\n")) {
        if (line.startsWith("TICKS "))
            metrics.ticks = parseNumber(line.slice(6));
        else if (line.startsWith("HZ "))
            metrics.hz = Math.max(1, parseNumber(line.slice(3)));
        else if (line.startsWith("RSS "))
            metrics.rssKb = parseNumber(line.slice(4));
        else if (line.startsWith("MEMTOTAL "))
            metrics.totalKb = parseNumber(line.slice(9));
        else if (line.startsWith("CORES "))
            metrics.cores = Math.max(1, parseNumber(line.slice(6)));
        else if (line.startsWith("USEDNVIDIA"))
            usedNvidia = true;
        else if (line.startsWith("USEDGPU ")) {
            // USEDGPU <vendor> <busy> <node> <name...>
            const rest = line.slice(8).split(" ");
            if (rest.length >= 3) {
                used.push({
                    vendor: rest[0],
                    busy: parseNumber(rest[1]),
                    node: rest[2],
                    name: rest.slice(3).join(" ")
                });
            }
        } else if (line.startsWith("PMON "))
            metrics.pmon.push(line.slice(5));
        else if (line.startsWith("NVIDIA "))
            nvidia.push(line.slice(7));
    }

    // Use only DRM nodes actually held open by the Luma process. Prefer a
    // render node over a card node; this is the GPU the player is rendering on.
    const renderNodes = used.filter((entry) => entry.node.indexOf("renderD") === 0);
    const chosen = (renderNodes.length ? renderNodes : used)[0];
    if (chosen) {
        metrics.gpu = chosen.busy;
        metrics.gpuName = chosen.name;
        metrics.gpuNode = chosen.node;
    }

    if (usedNvidia && nvidia.length > 0) {
        const line = nvidia[0];
        const comma = line.indexOf(",");
        if (comma >= 0) {
            metrics.gpu = parseNumber(line.slice(0, comma));
            metrics.gpuName = line.slice(comma + 1).trim();
            metrics.gpuNode = "nvidia";
        }
    }
    return metrics;
}

// nvidia-smi pmon reports per-process SM/memory/encoder/decoder utilization for
// compute and graphics processes. Use the busiest engine as the player's GPU
// usage: for a video player that is usually the decoder or the render engine.
function pmonUsage(line) {
    // gpu pid type sm mem enc dec command; some drivers omit the type column.
    const parts = line.trim().split(/\s+/);
    const base = parts.length > 2 && /^[CGM](\+[CGM])?$/.test(parts[2]) ? 3 : 2;
    if (parts.length < base + 4)
        return -1;
    let usage = -1;
    for (let i = base; i < base + 4; ++i) {
        const value = Number(parts[i]);
        if (Number.isFinite(value) && value > usage)
            usage = value;
    }
    return usage;
}

function collect() {
    if (!luma || collecting)
        return;
    collecting = true;
    // CPU ticks are sampled near the start of the shell command. Measure the
    // interval from this timestamp, not from the (potentially much later)
    // QProcess callback time, otherwise a slow sample inflates the percentage.
    const sampleTime = Date.now();
    const script = [
        "pid=$PPID",
        "awk '{print \"TICKS \" $14+$15}' /proc/$pid/stat 2>/dev/null",
        "getconf CLK_TCK 2>/dev/null | sed 's/^/HZ /'",
        "echo \"CORES $(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)\"",
        "awk '/VmRSS:/{print \"RSS \" $2}' /proc/$pid/status 2>/dev/null",
        "awk '/MemTotal:/{print \"MEMTOTAL \" $2}' /proc/meminfo 2>/dev/null",
        "used_nvidia=0",
        "for fd in /proc/$pid/fd/*; do",
        "  target=$(readlink \"$fd\" 2>/dev/null)",
        "  case \"$target\" in",
        "    /dev/nvidia*) used_nvidia=1 ;;",
        "    /dev/dri/card*|/dev/dri/renderD*)",
        "      node=${target##*/}",
        "      dev=/sys/class/drm/$node/device",
        "      [ -d \"$dev\" ] || continue",
        "      vendor=$(cat \"$dev/vendor\" 2>/dev/null)",
        "      busy=$(cat \"$dev/gpu_busy_percent\" 2>/dev/null)",
        "      [ -n \"$busy\" ] || busy=-1",
        "      case \"$vendor\" in",
        "        0x1002) name=AMD ;;",
        "        0x8086) name=Intel ;;",
        "        0x10de) name=NVIDIA; used_nvidia=1 ;;",
        "        *) name=GPU ;;",
        "      esac",
        "      echo \"USEDGPU $vendor $busy $node $name\"",
        "      ;;",
        "  esac",
        "done",
        "if [ \"$used_nvidia\" = 1 ]; then",
        "  echo \"USEDNVIDIA 1\"",
        "  if command -v nvidia-smi >/dev/null 2>&1; then",
        "    nvidia-smi --query-gpu=utilization.gpu,name --format=csv,noheader,nounits 2>/dev/null | head -n 1 | sed 's/^/NVIDIA /'",
        "    nvidia-smi pmon -c 1 -s u 2>/dev/null | awk -v pid=\"$pid\" '$1 !~ /^#/ && $2 == pid { print \"PMON \" $0 }'",
        "  fi",
        "fi"
    ].join("\n");

    luma.process.run("/bin/sh", ["-c", script], { timeoutMs: 3500 }, (response) => {
        collecting = false;
        if (!response || !response.ok || !response.stdout)
            return;
        const metrics = processMetrics(response.stdout);
        if (metrics.ticks < 0)
            return;

        // The host can measure the GPU time of its own video rendering. That
        // is the player's usage; fall back to the system-wide value only when
        // the renderer could not measure it.
        if (luma.stats) {
            const snapshot = luma.stats.snapshot();
            const hostGpu = Number(snapshot["gpu-usage"]);
            if (Number.isFinite(hostGpu) && hostGpu >= 0) {
                metrics.gpu = hostGpu;
                metrics.gpuPlayer = true;
            }
        }
        if (!metrics.gpuPlayer && metrics.pmon.length > 0) {
            const usage = pmonUsage(metrics.pmon[0]);
            if (usage >= 0) {
                metrics.gpu = usage;
                metrics.gpuPlayer = true;
            }
        }

        let cpu = 0;
        if (previousTicks >= 0 && sampleTime > previousTime) {
            const elapsed = (sampleTime - previousTime) / 1000;
            cpu = 100 * (metrics.ticks - previousTicks)
                  / (elapsed * metrics.hz);
            cpu = Math.max(0, Math.min(metrics.cores * 100, cpu));
        }
        previousTicks = metrics.ticks;
        previousTime = sampleTime;
        lastSampleAt = Date.now();

        lastCpu = cpu;
        cpuCores = metrics.cores;
        lastCpuSystem = cpu / Math.max(1, metrics.cores);
        lastRamMb = metrics.rssKb / 1024;
        lastRamPercent = metrics.totalKb > 0
            ? Math.min(100, lastRamMb * 1024 / metrics.totalKb * 100) : 0;
        lastGpu = metrics.gpu;
        lastGpuName = metrics.gpuName;
        lastGpuPlayer = metrics.gpuPlayer;

        push(cpuHistory, lastCpuSystem);
        push(ramHistory, lastRamPercent);
        push(gpuHistory, lastGpu >= 0 ? lastGpu : 0);
        publish();
    });
}

function chart(y, title, value, color, history) {
    const width = 300;
    const left = 14;
    const right = width - 14;
    const chartTop = y + 30;
    const chartBottom = y + 105;
    const span = right - left;
    let points = "";
    const count = Math.max(1, HISTORY - 1);
    for (let i = 0; i < history.length; ++i) {
        const x = left + span * i / count;
        const v = Math.max(0, Math.min(100, Number(history[i]) || 0));
        const py = chartBottom - (chartBottom - chartTop) * v / 100;
        points += (points === "" ? "" : " ") + x.toFixed(1) + "," + py.toFixed(1);
    }
    return `<text x="12" y="${y + 16}" fill="#f0f0f0" font-family="sans-serif" font-size="12" font-weight="bold">${title}</text>`
        + `<text x="288" y="${y + 16}" text-anchor="end" fill="${color}" font-family="sans-serif" font-size="11">${value}</text>`
        + `<line x1="${left}" y1="${chartBottom}" x2="${right}" y2="${chartBottom}" stroke="#666" stroke-width="0.5" opacity="0.5"/>`
        + `<line x1="${left}" y1="${chartTop}" x2="${right}" y2="${chartTop}" stroke="#666" stroke-width="0.5" opacity="0.25"/>`
        + `<polygon points="${left},${chartBottom} ${points} ${right},${chartBottom}" fill="${color}" opacity="0.14"/>`
        + `<polyline points="${points}" fill="none" stroke="${color}" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round"/>`;
}

function svgPage() {
    let gpuText = "n/a";
    if (lastGpu >= 0) {
        const scope = lastGpuPlayer ? "player" : "system";
        gpuText = lastGpu.toFixed(0) + "%"
                  + (lastGpuName ? " | " + lastGpuName : "") + " | " + scope;
    }
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="300" height="330">`
        + chart(0, "CPU", lastCpu.toFixed(0) + "% core | "
                                 + lastCpuSystem.toFixed(0) + "% sys",
                "#6ab0ff", cpuHistory)
        + chart(110, "RAM", lastRamMb.toFixed(0) + " MB", "#8de26a", ramHistory)
        + chart(220, "GPU", gpuText, "#ffb86a", gpuHistory)
        + `</svg>`;
    return svg;
}

function publish() {
    if (!luma || !luma.stats)
        return;
    const result = luma.stats.setPage("resources", luma.i18n.t("Resource usage"), [
        { type: "svg", svg: base64(svgPage()), height: 330 }
    ], 3);
    if (result && result.ok === false) {
        if (reportedPageError !== result.error) {
            reportedPageError = result.error;
            luma.log.warn(result.error);
        }
        return;
    }
    reportedPageError = "";
}

function sampleIfDue() {
    const now = Date.now();
    if (now - lastSampleAt < 2000)
        return;
    lastSampleAt = now;
    collect();
}

function activate(lumaApi) {
    luma = lumaApi;
    luma.events.on("startup", sampleIfDue);
    luma.events.on("fileOpened", sampleIfDue);
    luma.events.on("mediaChanged", sampleIfDue);
    luma.events.on("positionChanged", sampleIfDue);
    luma.events.on("pausedChanged", sampleIfDue);
    publish();
    sampleIfDue();
}

function deactivate() {
    // No host resources remain; the page is removed by the runtime.
}
