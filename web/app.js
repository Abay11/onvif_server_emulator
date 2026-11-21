"use strict";

(() => {
  const byId = (id) => document.getElementById(id);
  const restartButton = byId("restart");
  const refreshButton = byId("refresh");
  const dialog = byId("restart-dialog");
  const labels = {
    application: "Application", network: "Network", discovery: "Discovery", events: "Events",
    media: "Media", simulation: "Simulation", configuration_directory: "Configuration directory",
    logging_level: "Logging level", logging_output: "Logging output", onvif_authentication: "ONVIF authentication",
    web_authentication: "WebUI authentication", bind_ipv4: "Bind IPv4", http_port: "HTTP port",
    rtsp_port: "RTSP port", advertised_ipv4: "Advertised IPv4", http_endpoint: "HTTP endpoint",
    rtsp_endpoint: "RTSP endpoint", rtp_port_min: "First RTP port", rtp_port_max: "Last RTCP port",
    http_port_forwarding: "HTTP forwarding simulation", rtsp_port_forwarding: "RTSP forwarding simulation",
    enabled: "Enabled", interface_ipv4: "Multicast interface", multicast_endpoint: "Multicast endpoint",
    pullpoint_endpoint: "PullPoint base URL", pullpoint_timeout_seconds: "PullPoint timeout (seconds)",
    metadata_enabled: "Metadata transport", file_streaming_enabled: "File streaming",
    network_delay_ms: "Network delay (milliseconds)", multichannel_enabled: "Multichannel simulation",
    channel_count: "Configured channels", Manufacturer: "Manufacturer", Model: "Model",
    FirmwareVersion: "Firmware version", SerialNumber: "Serial number", HardwareId: "Hardware ID"
  };
  let current = null;
  let busy = false;
  let refreshing = false;
  let configSignature = "";
  let statusError = false;

  function node(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = String(text);
    if (className) element.className = className;
    return element;
  }

  function notify(text, type = "") {
    const notice = byId("notice");
    notice.textContent = text;
    notice.className = `notice ${type}`;
    notice.hidden = !text;
  }

  function connection(text, type) {
    byId("connection-label").textContent = text;
    byId("connection").className = `connection ${type}`;
  }

  function display(value) {
    if (value === "true" || value === true) return "Enabled";
    if (value === "false" || value === false) return "Disabled";
    return value === "" || value === null || value === undefined ? "—" : String(value);
  }

  function list(target, entries) {
    target.replaceChildren();
    for (const [key, value] of entries) {
      target.append(node("dt", labels[key] || key), node("dd", display(value), /endpoint/.test(key) ? "endpoint" : ""));
    }
  }

  function uptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (hours >= 24) return `${Math.floor(hours / 24)}d ${hours % 24}h`;
    if (hours) return `${hours}h ${minutes}m`;
    return `${minutes}m ${seconds % 60}s`;
  }

  function render(data) {
    current = data;
    const settings = data.settings;
    byId("authentication-active").textContent = `Active scheme: ${settings.application.onvif_authentication}`;
    const profiles = data.profiles;
    byId("uptime").textContent = uptime(data.uptime_seconds);
    byId("process").textContent = `Process ${data.pid} · Current run`;
    byId("http-port").textContent = `:${settings.network.http_port}`;
    byId("http-bind").textContent = `${settings.network.bind_ipv4} · ONVIF & administration`;
    byId("profile-count").textContent = profiles.length;
    byId("metadata-status").textContent = `Metadata transport ${settings.media.metadata_enabled === "true" ? "enabled" : "disabled"}`;
    byId("last-update").textContent = `Updated ${new Date().toLocaleTimeString()}`;
    restartButton.disabled = busy || !data.reboot_available || data.reboot_pending;
    restartButton.title = data.reboot_available ? "" : "Restart is unavailable in this application";
    connection(busy || data.reboot_pending ? "Restarting" : "Online", busy || data.reboot_pending ? "connecting" : "online");
    const signature = JSON.stringify([settings, profiles]);
    if (signature === configSignature) return;
    configSignature = signature;
    list(byId("network-summary"), [
      ["http_endpoint", settings.network.http_endpoint], ["rtsp_endpoint", settings.network.rtsp_endpoint],
      ["pullpoint_endpoint", settings.events.pullpoint_endpoint],
      ["RTP / RTCP ports", `${settings.network.rtp_port_min}–${settings.network.rtp_port_max}`]
    ]);
    list(byId("device-summary"), Object.entries(settings.device));
    const container = byId("settings");
    const opened = new Set([...container.querySelectorAll("details[open]")].map((entry) => entry.dataset.section));
    const firstRender = !container.querySelector("details");
    container.replaceChildren();
    for (const key of ["application", "network", "discovery", "events", "media", "simulation"]) {
      const details = node("details");
      details.dataset.section = key;
      details.open = opened.has(key) || (firstRender && key === "application");
      const values = node("dl", undefined, "key-values");
      list(values, Object.entries(settings[key]).filter(([name]) => name !== "onvif_authentication"));
      details.append(node("summary", labels[key]), values);
      container.append(details);
    }
    const profileBody = byId("profiles-body");
    profileBody.replaceChildren();
    for (const profile of profiles) {
      const row = node("tr");
      const name = node("td", profile.Name || profile.token);
      name.append(node("span", profile.token, "subline mono"));
      row.append(name, node("td", profile.VideoEncoder || "—", "mono"), node("td", profile.AudioEncoder || "—", "mono"), node("td", profile.Metadata || "Not attached", profile.Metadata ? "mono" : "muted"));
      profileBody.append(row);
    }
    if (!profiles.length) {
      const cell = node("td", "No media profiles configured.");
      cell.colSpan = 4;
      const row = node("tr");
      row.append(cell);
      profileBody.append(row);
    }
    const serviceBody = byId("services-body");
    serviceBody.replaceChildren();
    for (const service of Array.isArray(settings.services) ? settings.services : []) {
      const row = node("tr");
      const serviceName = service.endpoint.split("/").filter(Boolean).pop().replaceAll("_", " ");
      const name = node("td", serviceName.replace(/\b\w/g, (letter) => letter.toUpperCase()));
      const listing = node("td");
      listing.append(node("span", service.advertised === "true" ? "Listed" : "Not listed", service.advertised === "true" ? "badge" : "badge inactive"));
      row.append(name, node("td", service.endpoint, "endpoint"), listing);
      serviceBody.append(row);
    }
  }

  async function getStatus() {
    const response = await window.adminFetch("/admin/api/status", {cache: "no-store", signal: AbortSignal.timeout(4000)});
    if (!response.ok) throw new Error(`Status request failed (HTTP ${response.status}).`);
    return response.json();
  }

  async function refresh(manual = false) {
    if (busy || refreshing) return;
    refreshing = true;
    refreshButton.disabled = true;
    try {
      render(await getStatus());
      if (manual || statusError) notify("");
      statusError = false;
    } catch (error) {
      statusError = true;
      connection("Offline", "offline");
      restartButton.disabled = true;
      notify(`Application status is unavailable. ${error.message} Displayed settings may be out of date.`, "error");
    } finally {
      refreshing = false;
      refreshButton.disabled = busy;
    }
  }

  async function restart() {
    if (busy || !current?.reboot_available) return;
    busy = true;
    const previousInstance = current.instance_id;
    restartButton.disabled = true;
    refreshButton.disabled = true;
    restartButton.textContent = "Restarting…";
    connection("Restarting", "connecting");
    notify("Restarting the application. Waiting for it to reconnect…");
    try {
      try {
        const response = await window.adminFetch("/admin/api/reboot", {
          method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "reboot"},
          body: JSON.stringify({confirm: true}), signal: AbortSignal.timeout(5000)
        });
        if (response.status !== 202) {
          const result = await response.json();
          throw new Error(result.error || `Restart failed (HTTP ${response.status}).`);
        }
      } catch (error) {
        // Exec may close the connection before the browser finishes reading it.
        // A failed transport is ambiguous; only a new instance confirms success.
        if (!(error instanceof TypeError) && error.name !== "TimeoutError") throw error;
      }
      const deadline = Date.now() + 45000;
      while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 750));
        let data;
        try { data = await getStatus(); } catch { continue; }
        if (data.instance_id !== previousInstance) {
          busy = false;
          render(data);
          notify("Application restarted. The current configuration is now loaded.", "success");
          return;
        }
      }
      throw new Error("Restart could not be confirmed. If the HTTP address or port changed, open the new address. Otherwise, check the application logs.");
    } catch (error) {
      notify(error.message, "error");
    } finally {
      busy = false;
      restartButton.textContent = "Restart application";
      refreshButton.disabled = false;
      await refresh();
    }
  }

  // Each view owns a cursor. Filtering and pausing never consume the server's
  // history or interfere with other browsers and ONVIF consumers.
  let journalCursor = 0;
  let journalInstance = null;
  let journalPaused = false;
  let journalGeneration = 0;
  let journalTimer;
  let journalRequest = null;
  let journalRows = [];
  let journalDropped = 0;
  let journalFilter = {category: "", level: "", q: ""};

  function drawJournal() {
    const body = byId("journal-body");
    body.replaceChildren();
    for (const entry of journalRows) {
      const row = node("tr");
      const time = node("td", new Date(entry.time).toLocaleString(), "journal-time");
      time.title = entry.time;
      const type = node("td");
      type.append(node("span", entry.level, `badge journal-${entry.level}`), node("span", entry.category, "subline"));
      row.append(time, type, node("td", entry.source, "journal-source mono"), node("td", entry.message, "journal-message"));
      body.append(row);
    }
    if (!journalRows.length) {
      const row = node("tr");
      const empty = node("td", "No matching events in the retained history.");
      empty.colSpan = 4;
      row.append(empty);
      body.append(row);
    }
    byId("journal-gap").hidden = !journalDropped;
    byId("journal-gap").textContent = `${journalDropped} earlier events are no longer in the server buffer. History is incomplete.`;
  }

  async function updateJournal() {
    clearTimeout(journalTimer);
    if (journalPaused || document.hidden) {
      journalTimer = setTimeout(updateJournal, 1000);
      return;
    }
    const generation = journalGeneration;
    const controller = new AbortController();
    journalRequest = controller;
    const timeout = setTimeout(() => controller.abort(), 5000);
    let delay = 1000;
    try {
      const query = new URLSearchParams({...journalFilter, after: String(journalCursor), limit: "100"});
      const response = await window.adminFetch(`/admin/api/journal?${query}`, {cache: "no-store", signal: controller.signal});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const batch = await response.json();
      if (generation !== journalGeneration) return;
      if (journalInstance && journalInstance !== batch.instance_id) {
        journalInstance = batch.instance_id;
        journalCursor = 0;
        journalRows = [];
        journalDropped = 0;
        drawJournal();
        byId("journal-state").textContent = "Application restarted. Loading the new journal…";
        delay = 0;
      } else {
        journalInstance = batch.instance_id;
        if (batch.reset) { journalRows = []; journalDropped = 0; }
        journalCursor = batch.cursor;
        journalDropped += batch.dropped;
        // Keep browser memory and DOM bounded independently of server retention.
        journalRows = [...batch.entries].reverse().concat(journalRows).slice(0, 500);
        drawJournal();
        byId("journal-state").textContent = `${batch.has_more ? "Catching up" : "Live"} · ${journalRows.length} most recent matching events shown · Updated ${new Date().toLocaleTimeString()}`;
        delay = batch.has_more ? 100 : 1000;
      }
    } catch (error) {
      if (generation === journalGeneration && !journalPaused) {
        byId("journal-state").textContent = `Journal disconnected (${error.message}). Retrying without advancing the cursor…`;
        delay = 3000;
      }
    } finally {
      clearTimeout(timeout);
      if (journalRequest === controller) journalRequest = null;
      if (generation === journalGeneration) journalTimer = setTimeout(updateJournal, delay);
    }
  }

  byId("journal-filters").addEventListener("submit", (event) => {
    event.preventDefault();
    journalFilter = {category: byId("journal-category").value, level: byId("journal-level").value, q: byId("journal-search").value};
    ++journalGeneration;
    journalRequest?.abort();
    journalCursor = 0;
    journalRows = [];
    journalDropped = 0;
    byId("journal-gap").hidden = true;
    byId("journal-state").textContent = journalPaused ? "Updates paused. Resume to apply filters." : "Loading filtered history…";
    drawJournal();
    updateJournal();
  });
  byId("journal-pause").addEventListener("click", () => {
    journalPaused = !journalPaused;
    ++journalGeneration;
    journalRequest?.abort();
    byId("journal-pause").setAttribute("aria-pressed", String(journalPaused));
    byId("journal-pause").textContent = journalPaused ? "Resume updates" : "Pause updates";
    byId("journal-state").textContent = journalPaused ? "Updates paused. Internal collection continues." : "Reconnecting…";
    updateJournal();
  });
  updateJournal();

  refreshButton.addEventListener("click", () => refresh(true));
  restartButton.addEventListener("click", () => {
    dialog.returnValue = "cancel";
    dialog.showModal();
  });
  dialog.addEventListener("close", () => { if (dialog.returnValue === "restart") restart(); });
  function navigation() {
    const target = location.hash || "#overview";
    for (const link of document.querySelectorAll(".nav-link")) {
      if (link.getAttribute("href") === target) link.setAttribute("aria-current", "location");
      else link.removeAttribute("aria-current");
    }
  }
  window.addEventListener("hashchange", navigation);
  navigation();
  refresh();
  setInterval(() => { if (!document.hidden) refresh(); }, 5000);
})();
