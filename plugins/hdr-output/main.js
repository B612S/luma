// HDR Output: request HDR signalling from mpv. The compositor, display and
// driver must support HDR; Luma cannot force it on unsupported hardware.

let luma;
let active = false;
let original = {};

function remember(key) {
    const value = luma.mpv.getProperty(key);
    if (value !== undefined && value !== null && value !== "")
        original[key] = value;
}

function restore(key) {
    if (original[key] !== undefined)
        luma.mpv.setProperty(key, original[key]);
}

// The plugin is enabled or disabled in Settings → Plugins; there is no
// second switch in the plugin settings.
function apply() {
    const state = luma.media.state();
    if (!state.hasVideo)
        return;

    if (!active) {
        remember("target-colorspace-hint");
        remember("hdr-compute-peak");
        remember("tone-mapping");
        active = true;
    }

    luma.mpv.setProperty("target-colorspace-hint", "yes");
    luma.mpv.setProperty("hdr-compute-peak", "yes");
    luma.mpv.setProperty("tone-mapping",
                         String(luma.config.get("toneMapping", "auto")));
    luma.log.info("HDR output requested");
}

function restoreAll() {
    if (!active)
        return;
    restore("target-colorspace-hint");
    restore("hdr-compute-peak");
    restore("tone-mapping");
    active = false;
    original = {};
}

function activate(lumaApi) {
    luma = lumaApi;
    luma.log.info("HDR Output loaded");
    luma.events.on("startup", apply);
    luma.events.on("fileOpened", apply);
    luma.events.on("settingsChanged", apply);
    apply();
}

function deactivate() {
    restoreAll();
}
