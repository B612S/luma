// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Luma contributors

// Upscaler: clean up compression artifacts and banding, then scale the result
// with a high-quality scaler or optional GPU CNN shaders.
//
// Player-specific choices:
//  - deblocking and denoising are libavfilter filters (`deblock`, `hqdn3d`).
//    They remove MPEG-2 blocking and analogue noise before scaling, but only
//    accept software frames. Hardware decoding is therefore switched to its
//    copy mode while cleanup is enabled; the user's option is restored when
//    cleanup is turned off.
//  - debanding and sharpening are vo_gpu options: they run on the GPU at the
//    output resolution and cost no extra copy.
//  - the filters are added in `fileOpened`, before mpv loads the next file, so
//    playback starts without a reconfiguration hitch. The chain survives file
//    changes, so switching episodes does not rebuild it.
//  - live streams skip CPU cleanup: a full filter chain on a 1080p60 stream
//    would drop frames for little benefit. The GPU steps still apply.

const LABEL_DEBLOCK = "@luma-upscale-deblock";
const LABEL_DENOISE = "@luma-upscale-denoise";
const ALL_LABELS = [LABEL_DEBLOCK, LABEL_DENOISE];
const SCALER_OPTIONS = ["scale", "scale-antiring", "cscale", "dscale"];

const UPSCALERS = {
    sharp: {
        scale: "ewa_lanczossharp",
        antiring: "0.5",
        cscale: "ewa_lanczossoft",
        dscale: "mitchell"
    },
    balanced: {
        scale: "spline36",
        antiring: "0.3",
        cscale: "bicubic",
        dscale: "mitchell"
    }
};

const DEBAND_LEVELS = {
    light: { iterations: "1", threshold: "32", range: "16", grain: "24" },
    strong: { iterations: "2", threshold: "48", range: "24", grain: "16" }
};

// These lookup tables only carry presets, so "off" is listed separately: it is
// a valid setting that must not fall back to the first preset.
const UPSCALER_VALUES = { sharp: true, balanced: true, off: true };
const CLEANUP_VALUES = { off: true, light: true, strong: true };
const DEBAND_VALUES = { off: true, light: true, strong: true };

const SHARPEN_LEVELS = {
    off: 0,
    light: 0.2,
    medium: 0.35,
    strong: 0.6
};

const CLEANUP_LEVELS = {
    light: {
        deblock: "deblock=filter=weak:block=8",
        denoise: "hqdn3d=luma_spatial=1.2:chroma_spatial=0.8:luma_tmp=1.5:chroma_tmp=1.5"
    },
    strong: {
        deblock: "deblock=filter=strong:block=8",
        denoise: "hqdn3d=luma_spatial=2.5:chroma_spatial=1.5:luma_tmp=3:chroma_tmp=3"
    }
};

// The neural mode ships its CNN shader sources in the plugin's assets/
// directory (MIT/Unlicense, by bloc97; each source keeps its license notice).
// They are read through luma.storage.readAsset() and installed into the plugin
// data directory, because mpv's glsl-shaders needs real file paths.
const SHADER_FILES = {
    clamp: "cnn_clamp_highlights.glsl",
    restoreM: "cnn_restore_m.glsl",
    upscaleM: "cnn_upscale_m.glsl",
    upscaleS: "cnn_upscale_s.glsl",
    upscaleDenoiseM: "cnn_upscale_denoise_m.glsl",
    auto2: "cnn_downscale_pre_x2.glsl",
    auto4: "cnn_downscale_pre_x4.glsl"
};

const SHADER_CHAINS = {
    restore: ["clamp", "restoreM", "upscaleM", "auto2", "auto4", "upscaleS"],
    denoise: ["clamp", "upscaleDenoiseM", "auto2", "auto4", "upscaleS"]
};

const SHADER_VALUES = { off: true, restore: true, denoise: true };

let luma = null;
let processing = true;
let shaderError = "";
let shadersInstalled = false;
let originals = {};
let appliedSignature = "";
let activeLabels = [];
let activeCleanup = "";
let chainError = "";
let retriedChain = false;
let appliedAt = 0;
let lastHwdecCheck = 0;
let hwdecFixAttempts = 0;
let lastStatsRefresh = 0;
let statsPageError = "";

function has(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
}

function t(key, fallback) {
    return luma.i18n.t(key, fallback);
}

function configString(key, fallback) {
    const value = luma.config.get(key, fallback);
    return value === undefined || value === null ? fallback : String(value);
}

function knownValue(value, known, fallback) {
    return has(known, value) ? value : fallback;
}

function readConfig() {
    return {
        upscaler: knownValue(configString("upscaler", "sharp"), UPSCALER_VALUES, "sharp"),
        cleanup: knownValue(configString("cleanup", "light"), CLEANUP_VALUES, "light"),
        deband: knownValue(configString("deband", "light"), DEBAND_VALUES, "light"),
        sharpening: knownValue(configString("sharpening", "medium"), SHARPEN_LEVELS, "medium"),
        shaders: knownValue(configString("shaders", "off"), SHADER_VALUES, "off")
    };
}

function mediaState() {
    if (!luma || !luma.media)
        return {};
    return luma.media.state() || {};
}

function snapshot() {
    if (!luma || !luma.stats)
        return {};
    return luma.stats.snapshot() || {};
}

function setManaged(name, value) {
    if (!has(originals, name)) {
        const current = luma.mpv.getProperty(name);
        if (current !== undefined && current !== null && current !== "")
            originals[name] = current;
    }
    luma.mpv.setProperty(name, value);
}

function restoreOption(name) {
    if (!has(originals, name))
        return;
    luma.mpv.setProperty(name, originals[name]);
    delete originals[name];
}

function restoreAllManaged() {
    for (const name of Object.keys(originals))
        luma.mpv.setProperty(name, originals[name]);
    originals = {};
}

function vfEntries() {
    const value = snapshot()["vf"];
    // MediaManager observes "vf" as MPV_FORMAT_NODE, so this is a list of
    // filter objects ({ name, label, enabled, params }), not a string.
    // QJSEngine exposes a QVariantList as an array-like object: Array.isArray()
    // is false, but length and index access work.
    if (!value || typeof value !== "object" || typeof value.length !== "number")
        return [];
    const entries = [];
    for (let i = 0; i < value.length; i++)
        entries.push(value[i]);
    return entries;
}

function labelInVf(label) {
    // mpv reports labels without the leading "@".
    const name = label.substring(1);
    const entries = vfEntries();
    if (entries.length > 0) {
        for (const entry of entries) {
            if (!entry || typeof entry !== "object")
                continue;
            if (String(entry.label || "") === name)
                return entry.enabled !== false;
        }
        return false;
    }
    // Defensive fallback: older hosts may expose the compact string form.
    const value = snapshot()["vf"];
    if (value === undefined || value === null)
        return false;
    return String(value).indexOf(name) !== -1;
}

function chainPresent() {
    if (activeLabels.length === 0)
        return false;
    for (const label of activeLabels) {
        if (!labelInVf(label))
            return false;
    }
    return true;
}

function softwareFrames() {
    const value = String(snapshot()["hwdec-current"] || "").trim().toLowerCase();
    return value === "" || value === "no" || value.endsWith("-copy");
}

// Copy-back decoders already give the CPU filters software frames; the
// plugin only has to keep hardware decoding out of its non-copy modes while
// cleanup is enabled, and restore the user's option afterwards.
function ensureHardwareCopy() {
    const option = String(luma.mpv.getProperty("hwdec") || "").trim();
    if (option === "")
        return false;
    const lower = option.toLowerCase();
    if (lower === "no" || lower.endsWith("-copy"))
        return false;
    setManaged("hwdec", "auto-copy");
    return true;
}

function applyUpscaler(value) {
    if (value === "off") {
        for (const name of SCALER_OPTIONS)
            restoreOption(name);
        return;
    }
    const preset = UPSCALERS[value] || UPSCALERS.sharp;
    setManaged("scale", preset.scale);
    setManaged("scale-antiring", preset.antiring);
    setManaged("cscale", preset.cscale);
    setManaged("dscale", preset.dscale);
}

function applyDeband(value) {
    if (value === "off") {
        // Explicitly disable it: "Off" must win over a deband=yes that the
        // plugin set for the previous file (or a user's own setting).
        setManaged("deband", "no");
        return;
    }
    const preset = DEBAND_LEVELS[value] || DEBAND_LEVELS.light;
    setManaged("deband", "yes");
    setManaged("deband-iterations", preset.iterations);
    setManaged("deband-threshold", preset.threshold);
    setManaged("deband-range", preset.range);
    setManaged("deband-grain", preset.grain);
}

function applySharpening(value) {
    const amount = has(SHARPEN_LEVELS, value) ? SHARPEN_LEVELS[value]
                                             : SHARPEN_LEVELS.medium;
    setManaged("sharpen", String(amount));
}

function shaderFilePath(name) {
    const directory = String(luma.dataDir || "");
    if (directory === "")
        return "";
    return directory.endsWith("/") ? directory + name : directory + "/" + name;
}

function pathListText(value) {
    if (value === undefined || value === null)
        return "";
    if (typeof value === "string")
        return value;
    if (typeof value === "object" && typeof value.length === "number") {
        const parts = [];
        for (let i = 0; i < value.length; i++)
            parts.push(String(value[i] || ""));
        return parts.join(":");
    }
    return String(value);
}

function installShaderFiles() {
    if (shadersInstalled)
        return true;
    if (!luma.storage || typeof luma.storage.readAsset !== "function") {
        shaderError = t("status.shaderUnsupported",
                        "This Luma version cannot read plugin assets");
        return false;
    }
    for (const key of Object.keys(SHADER_FILES)) {
        const name = SHADER_FILES[key];
        const source = luma.storage.readAsset(name);
        if (source === "") {
            shaderError = t("status.shaderReadFailed",
                            "Could not read the CNN shader assets");
            return false;
        }
        // Write once per session: this also refreshes files left over from an
        // older plugin version and recovers from a previous partial write.
        if (!luma.storage.writeText(name, source)) {
            shaderError = t("status.shaderWriteFailed",
                            "Could not write the CNN shader files");
            return false;
        }
    }
    shadersInstalled = true;
    return true;
}

// The shader mode writes the embedded sources into the plugin data directory
// and points mpv's glsl-shaders at them. The user's original shader list is
// saved and restored when the mode is turned off or the plugin is disabled.
function applyShaders(mode) {
    if (mode === "off") {
        restoreOption("glsl-shaders");
        shaderError = "";
        return;
    }

    const chain = SHADER_CHAINS[mode];
    if (!chain) {
        restoreOption("glsl-shaders");
        return;
    }
    if (!installShaderFiles()) {
        restoreOption("glsl-shaders");
        return;
    }

    const paths = [];
    for (const key of chain) {
        const path = shaderFilePath(SHADER_FILES[key]);
        if (path === "") {
            shaderError = t("status.shaderPath",
                            "Could not locate the shader data directory");
            restoreOption("glsl-shaders");
            return;
        }
        paths.push(path);
    }

    if (!has(originals, "glsl-shaders")) {
        const current = luma.mpv.getProperty("glsl-shaders");
        originals["glsl-shaders"] = current === undefined || current === null
            ? "" : current;
    }
    const base = pathListText(originals["glsl-shaders"]);
    const value = base === "" ? paths.join(":") : base + ":" + paths.join(":");
    if (!luma.mpv.setProperty("glsl-shaders", value)) {
        shaderError = t("status.shaderRejected", "mpv rejected the shader list");
        restoreOption("glsl-shaders");
        return;
    }
    shaderError = "";
}

function removeChain(force) {
    if (!luma || !luma.mpv)
        return;
    for (const label of ALL_LABELS) {
        if (force || activeLabels.indexOf(label) !== -1 || labelInVf(label))
            luma.mpv.command(["vf", "remove", label]);
    }
    activeLabels = [];
    activeCleanup = "";
}

function ensureChain(cleanup) {
    // A stale chain from a previous plugin runtime uses the same labels and
    // would make `vf add` fail; drop it first. Missing labels are harmless.
    removeChain(true);

    const preset = CLEANUP_LEVELS[cleanup];
    if (!preset) {
        activeLabels = [];
        chainError = "";
        return;
    }

    const filters = [
        { label: LABEL_DEBLOCK, spec: preset.deblock },
        { label: LABEL_DENOISE, spec: preset.denoise }
    ];
    let failed = 0;
    for (const filter of filters) {
        if (luma.mpv.command(["vf", "add", filter.label + ":" + filter.spec]))
            activeLabels.push(filter.label);
        else
            failed++;
    }
    chainError = failed > 0
        ? t("status.filterRejected", "mpv rejected a cleanup filter; see the mpv log")
        : "";
    if (failed > 0)
        luma.log.warn("cleanup filter rejected by mpv");
    activeCleanup = cleanup;
    appliedAt = Date.now();
}

function effectiveCleanup(config) {
    const state = mediaState();
    return processing && !state.live ? config.cleanup : "off";
}

// Signature includes only values that change the video chain. Calling apply()
// repeatedly (fileOpened, mediaChanged, settingsChanged) is cheap and does not
// touch mpv when nothing changed.
function apply(checkChain) {
    if (!luma)
        return;
    const state = mediaState();
    // Plugin activation happens before the QML tree creates MpvItem, so the
    // first `startup` event has no mpv to talk to. `appliedSignature` marks a
    // session that already applied the chain (mpv exists even if playback has
    // stopped since then). Avoid a blocking property read just to check this:
    // a synchronous read right after a file loads can cost the first frame.
    if (!state.hasMedia && appliedSignature === "") {
        refreshStatsPage();
        return;
    }

    const config = readConfig();
    const cleanup = processing && !state.live ? config.cleanup : "off";
    const signature = [processing, config.upscaler, cleanup, config.deband,
                       config.sharpening, config.shaders].join("|");
    if (signature === appliedSignature) {
        // A file change can drop the chain even though the options are still
        // right; restore it before the next file loads instead of waiting for
        // the first tick after playback starts.
        if (checkChain && processing && cleanup !== "off"
                && activeCleanup === cleanup && !chainPresent()) {
            ensureChain(cleanup);
        }
        refreshStatsPage();
        return;
    }
    appliedSignature = signature;
    chainError = "";
    retriedChain = false;
    hwdecFixAttempts = 0;

    if (!processing) {
        restoreAllManaged();
        removeChain(true);
        shaderError = "";
        appliedAt = 0;
        refreshStatsPage(true);
        return;
    }

    applyUpscaler(config.upscaler);
    applyDeband(config.deband);
    applySharpening(config.sharpening);
    applyShaders(config.shaders);

    if (cleanup === "off") {
        removeChain();
        restoreOption("hwdec");
        appliedAt = 0;
    } else {
        ensureHardwareCopy();
        // Only rebuild the CPU chain when its level changed or mpv lost it;
        // changing the scaler or sharpening must not reconfigure the video.
        if (activeCleanup !== cleanup || !chainPresent())
            ensureChain(cleanup);
    }
    refreshStatsPage(true);
}

function optionLabel(setting, value) {
    return t("settings." + setting + ".options." + value, value);
}

function cleanupText(config) {
    if (mediaState().live && config.cleanup !== "off")
        return t("state.skippedLive", "Skipped (live stream)");
    if (config.cleanup === "off")
        return optionLabel("cleanup", "off");
    return optionLabel("cleanup", config.cleanup);
}

function sourceText(values) {
    const parts = [];
    const width = Number(values["width"]);
    const height = Number(values["height"]);
    if (width > 0 && height > 0)
        parts.push(width + "×" + height);
    const codec = String(values["video-codec"] || "").trim();
    if (codec !== "")
        parts.push(codec);
    const bitrate = Number(values["video-bitrate"]);
    if (isFinite(bitrate) && bitrate > 0)
        parts.push((bitrate / 1000000).toFixed(1) + " Mbps");
    return parts.length > 0 ? parts.join(" · ") : t("state.unknown", "Unknown");
}

function statsContent() {
    const config = readConfig();
    const values = snapshot();
    const hwdec = String(values["hwdec-current"] || "").trim();
    const content = [
        { type: "label", text: t("stats.source", "Source") + ": "
                       + sourceText(values) },
        { type: "label", text: t("stats.upscaler", "Upscaler") + ": "
                       + optionLabel("upscaler", config.upscaler) },
        { type: "label", text: t("stats.cleanup", "Artifact removal") + ": "
                       + cleanupText(config) },
        { type: "label", text: t("stats.deband", "Debanding") + ": "
                       + optionLabel("deband", config.deband) },
        { type: "label", text: t("stats.sharpening", "Sharpening") + ": "
                       + optionLabel("sharpening", config.sharpening) },
        { type: "label", text: t("stats.shaders", "Neural upscale") + ": "
                       + optionLabel("shaders", config.shaders) },
        { type: "label", text: t("stats.hardware", "Hardware decoding") + ": "
                       + (hwdec !== "" ? hwdec : t("state.unknown", "Unknown")) },
        { type: "label", text: t("stats.filters", "Cleanup filters") + ": "
                       + (processing && chainPresent()
                          ? t("state.active", "Active")
                          : t("state.inactive", "Inactive")) }
    ];
    if (!processing)
        content.push({ type: "label", text: t("stats.status", "Status") + ": "
                                     + t("state.bypassed", "Bypassed by shortcut") });
    else if (shaderError !== "")
        content.push({ type: "label", text: t("stats.status", "Status") + ": "
                                     + shaderError });
    else if (chainError !== "")
        content.push({ type: "label", text: t("stats.status", "Status") + ": "
                                     + chainError });
    return content;
}

function refreshStatsPage(force) {
    if (!luma || !luma.stats)
        return;
    const now = Date.now();
    if (!force && now - lastStatsRefresh < 1500)
        return;
    lastStatsRefresh = now;

    const result = luma.stats.setPage(
        "upscaler", t("stats.title", "Upscaler stats"), statsContent());
    if (result && result.ok === false) {
        if (statsPageError !== result.error) {
            statsPageError = result.error;
            luma.log.warn(result.error);
        }
        return;
    }
    statsPageError = "";
}

// F10 is a temporary A/B bypass for the current session; the switch on the
// Plugins page remains the only persistent way to turn the plugin off.
function toggleProcessing() {
    processing = !processing;
    // Apply first: changing filters or hwdec can block the GUI thread for a
    // moment, and an OSD shown before that would spend its display time while
    // the player is still reconfiguring.
    apply();
    luma.ui.osd(processing ? t("osd.enabled", "Upscaler processing enabled")
                           : t("osd.disabled", "Upscaler processing disabled"));
    refreshStatsPage(true);
}

function tick() {
    if (!luma)
        return;

    if (appliedSignature === "")
        apply();

    const state = mediaState();
    const config = readConfig();
    if (!processing || !state.hasVideo) {
        refreshStatsPage();
        return;
    }

    const cleanup = effectiveCleanup(config);
    if (cleanup !== "off") {
        if (!chainPresent()) {
            if (!retriedChain && appliedAt > 0 && Date.now() - appliedAt > 2000) {
                // The chain can be lost across a file change, or when it was
                // added while the previous item was audio-only. Retry once.
                retriedChain = true;
                ensureChain(cleanup);
            } else if (retriedChain && chainError === "") {
                chainError = t("status.filterDisabled",
                               "mpv disabled the cleanup filters; see the mpv log");
            }
        }

        if (Date.now() - lastHwdecCheck > 5000 && !softwareFrames()) {
            lastHwdecCheck = Date.now();
            // Correct a decoder that a settings reload or the hardware-decoding
            // shortcut turned back into a non-copy mode. Do not restart it on
            // every tick if the option cannot take effect.
            if (hwdecFixAttempts < 2 && ensureHardwareCopy())
                hwdecFixAttempts++;
        }
    }
    refreshStatsPage();
}

function activate(lumaApi) {
    luma = lumaApi;
    processing = true;
    shaderError = "";
    shadersInstalled = false;
    originals = {};
    appliedSignature = "";
    activeLabels = [];
    activeCleanup = "";
    chainError = "";
    retriedChain = false;
    appliedAt = 0;
    lastHwdecCheck = 0;
    hwdecFixAttempts = 0;
    lastStatsRefresh = 0;
    statsPageError = "";

    luma.log.info("Upscaler loaded");
    if (luma.shortcuts && luma.shortcuts.on)
        luma.shortcuts.on("toggle", toggleProcessing);

    luma.events.on("startup", () => { apply(); refreshStatsPage(true); });
    luma.events.on("fileOpened", () => {
        retriedChain = false;
        chainError = "";
        apply(true);
        refreshStatsPage(true);
    });
    luma.events.on("mediaChanged", () => { apply(); refreshStatsPage(true); });
    luma.events.on("liveStreamChanged", () => { apply(); refreshStatsPage(true); });
    luma.events.on("settingsChanged", () => {
        retriedChain = false;
        apply();
        refreshStatsPage(true);
    });
    luma.events.on("languageChanged", () => refreshStatsPage(true));
    // While paused there are no position ticks; still finish a deferred chain
    // check when playback state changes.
    luma.events.on("pausedChanged", tick);
    luma.events.on("positionChanged", tick);

    apply();
    refreshStatsPage(true);
}

function deactivate() {
    if (!luma)
        return;
    removeChain(true);
    restoreAllManaged();
    appliedSignature = "";
    activeLabels = [];
    activeCleanup = "";
    shaderError = "";
    shadersInstalled = false;
    processing = true;
    luma.log.info("Upscaler unloaded");
    luma = null;
}
