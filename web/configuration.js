"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  const truth = (value) => value === true || value === "true";
  const array = (value) => Array.isArray(value) ? value : [];
  const clone = (value) => structuredClone(value);
  const kinds = ["VideoSource", "VideoEncoder", "AudioSource", "AudioEncoder", "AudioOutput", "AudioDecoder", "Metadata", "Analytics", "PTZ", "Receiver"];
  let state, media, selection, dirty = false, busy = false;
  const label = (text) => text.replace(/([a-z])([A-Z])/g, "$1 $2");
  function node(tag, text, className) {
    const item = document.createElement(tag);
    if (text !== undefined) item.textContent = text;
    if (className) item.className = className;
    return item;
  }
  function button(text, action, kind = "secondary") {
    const item = node("button", text, `button ${kind}`);
    item.type = "button"; item.addEventListener("click", action); return item;
  }
  function message(text, failure = false) {
    $("configuration-message").textContent = text;
    $("configuration-message").className = `notice ${failure ? "error" : "success"}`;
    $("configuration-message").hidden = !text;
  }
  function popup(summary, issues = []) {
    $("config-error-summary").textContent = summary;
    const target = $("config-error-details"); target.replaceChildren();
    for (const issue of issues) {
      const row = node("li");
      row.append(node("strong", `${issue.file || "Settings"} · ${issue.path || "/"}`), node("p", issue.message)); target.append(row);
    }
    target.hidden = !issues.length;
    if (!$("config-error-dialog").open) $("config-error-dialog").showModal();
  }
  function leave() { return !dirty || window.confirm("Discard unsaved configuration changes?"); }
  function markDirty() { dirty = true; message("Unsaved changes. Save configuration to apply them."); }
  function mediaEntry() { return state.files.find((item) => item.name === "media_profiles.config"); }
  function restartNotice(required) {
    $("configuration-restart").hidden = !required;
    $("configuration-restart").textContent = "Saved settings require an application restart to take full effect. Active values in the overview still describe this run.";
  }
  async function load() {
    try {
      const response = await window.adminFetch("/admin/api/configuration", {cache: "no-store", signal: AbortSignal.timeout(10000)});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      state = await response.json(); media = JSON.parse(mediaEntry().document);
      restartNotice(state.restart_required); catalog();
      return true;
    } catch (error) { message(`Configuration unavailable: ${error.message}`, true); return false; }
  }
  async function save(file, document, success) {
    if (busy) return;
    busy = true; $("configuration-workspace").inert = true;
    try {
      const entry = state.files.find((item) => item.name === file);
      const response = await window.adminFetch("/admin/api/configuration", {
        method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "configuration"},
        body: JSON.stringify({file, revision: entry.revision, document}), signal: AbortSignal.timeout(20000)
      });
      const result = await response.json();
      if (!response.ok) { popup(result.message || result.error, result.validation_errors || []); message(result.message || result.error, true); return; }
      dirty = false;
      if (await load()) {
        if (success) success(); else $("configuration-editor").replaceChildren(node("p", "Saved. Select an item to continue editing."));
        message(result.restart_required ? "Configuration saved. Changes take full effect only after restarting the application." : "Configuration saved and applied. Existing RTSP sessions retain their previous settings; reconnect to use the updated profile or metadata.");
        if (result.restart_required) $("configuration-saved-dialog").showModal();
      }
    } catch (error) { popup(`Save could not be confirmed: ${error.message}. Reload to inspect the current file before retrying.`); }
    finally { busy = false; $("configuration-workspace").inert = false; }
  }
  function catalog() {
    const target = $("configuration-catalog"); target.replaceChildren(node("h3", "Media profiles"));
    for (const profile of array(media.MediaProfiles)) target.append(button(`${profile.Name}${truth(profile.fixed) ? " · fixed" : ""}`, () => { if (leave()) editProfile(profile); }));
    target.append(button("Create media profile", () => { if (leave()) editProfile(null); }));
    target.append(node("h3", "ONVIF configurations"));
    for (const [type, entries] of Object.entries(media)) {
      if (type === "MediaProfiles" || !array(entries).some((entry) => entry?.token)) continue;
      const group = node("details"); group.append(node("summary", label(type)));
      for (const entry of entries) if (entry.token) group.append(button(entry.Name || entry.token, () => { if (leave()) editEntity(type, entry); }));
      target.append(group);
    }
    const files = node("details"); files.append(node("summary", "Configuration files · advanced"));
    for (const entry of state.files.filter((item) => item.name !== "users.config")) files.append(button(entry.name, () => { if (leave()) editFile(entry); }));
    target.append(files);
  }
  function form(title, note) {
    $("configuration-selected").textContent = title;
    const editor = node("form", undefined, "detector-form configuration-form");
    editor.append(node("p", note, "table-note"));
    editor.addEventListener("invalid", (event) => { event.preventDefault(); popup(event.target.validationMessage); }, true);
    $("configuration-editor").replaceChildren(editor);
    return editor;
  }
  function input(parent, key, value, readOnly = false) {
    if (value && typeof value === "object" && !Array.isArray(value)) {
      const box = node("fieldset"); box.append(node("legend", label(key)));
      for (const [name, child] of Object.entries(value)) box.append(input(value, name, child, readOnly));
      return box;
    }
    const wrapper = node("label", `${label(key)}${readOnly ? " (read-only)" : ""}`);
    let control;
    if (Array.isArray(value) || (value === "" && ["rules", "analyticsModules", "compatibleModuleAnalytics", "AnalyticsModules"].includes(key))) {
      control = node("textarea"); control.rows = 4; control.value = JSON.stringify(value === "" ? [] : value, null, 2);
      control.addEventListener("input", () => {
        try { const parsed = JSON.parse(control.value); if (!Array.isArray(parsed)) throw new Error(); parent[key] = parsed; control.setCustomValidity(""); }
        catch { control.setCustomValidity("Enter a valid JSON array"); }
        markDirty();
      });
    } else {
      control = node("input");
      control.type = (typeof value === "boolean" || ["true", "false"].includes(value)) ? "checkbox" : key === "password" ? "password" : "text";
      if (control.type === "checkbox") control.checked = truth(value); else control.value = value ?? "";
      const update = () => { parent[key] = control.type === "checkbox" ? control.checked : control.value; markDirty(); };
      control.addEventListener("input", update);
      control.addEventListener("change", update);
    }
    control.dataset.configurationKey = key;
    control.readOnly = readOnly; if (control.type === "checkbox") control.disabled = readOnly;
    wrapper.append(control); return wrapper;
  }
  function actions(editor, onSubmit) {
    const row = node("div", undefined, "dialog-actions");
    const submit = node("button", "Save configuration", "button secondary"); submit.type = "submit";
    row.append(submit, button("Cancel edits", () => { dirty = false; selection = null; $("configuration-editor").replaceChildren(node("p", "Select a profile, configuration or file.")); }));
    editor.append(row);
    editor.addEventListener("submit", (event) => { event.preventDefault(); onSubmit(); });
    return row;
  }
  function compatible(type, candidate, profile) {
    const source = array(media.VideoSource).find((item) => item.token === profile.VideoSource);
    if (type === "Metadata" && !state.metadata_enabled) return false;
    if (type === "Analytics" && source) return array(source.CompatibleAnalyticsConfigurations).includes(candidate.token);
    if (type === "PTZ" && source) return array(source.CompatiblePtzNodes).includes(candidate.NodeToken);
    return true;
  }
  function editProfile(original) {
    dirty = !original; selection = {profile: original?.token};
    const profile = original ? clone(original) : {token: `WebProfile_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`, Name: "New profile", fixed: false};
    const editor = form(original ? `Media profile · ${profile.Name}` : "Create media profile", "Select a configuration to attach it. Choose Not attached to remove an association. Profiles and metadata apply to new connections without an application restart.");
    editor.append(input(profile, "Name", profile.Name), node("p", `Token: ${profile.token}`, "mono table-note"));
    const bindings = node("div", undefined, "configuration-bindings");
    function renderBindings() {
      bindings.replaceChildren();
      for (const type of kinds) {
        const candidates = array(media[type]); if (!candidates.length && !profile[type]) continue;
        const wrapper = node("label", label(type)); const select = node("select"); select.dataset.configurationType = type;
        const empty = node("option", "Not attached"); empty.value = ""; select.append(empty);
        for (const candidate of candidates) {
          const option = node("option", `${candidate.Name || candidate.token} · ${candidate.token}`);
          option.value = candidate.token; option.disabled = !compatible(type, candidate, profile); select.append(option);
        }
        select.value = profile[type] || "";
        select.addEventListener("change", () => { profile[type] = select.value; markDirty(); if (type === "VideoSource") renderBindings(); });
        wrapper.append(select); bindings.append(wrapper);
      }
    }
    renderBindings(); editor.append(bindings);
    const row = actions(editor, () => {
      if (!profile.Name.trim()) { popup("Profile name is required"); return; }
      const next = clone(media);
      if (original) next.MediaProfiles = array(next.MediaProfiles).map((item) => item.token === original.token ? profile : item);
      else next.MediaProfiles = [...array(next.MediaProfiles), profile];
      save("media_profiles.config", JSON.stringify(next), () => editProfile(media.MediaProfiles.find((item) => item.token === profile.token)));
    });
    if (original) {
      const remove = button("Delete profile", () => {
        if (!window.confirm(`Delete profile “${original.Name}”?`)) return;
        const next = clone(media); next.MediaProfiles = next.MediaProfiles.filter((item) => item.token !== original.token);
        save("media_profiles.config", JSON.stringify(next));
      }, "danger");
      remove.disabled = truth(original.fixed); remove.title = remove.disabled ? "Fixed profiles cannot be deleted" : ""; row.append(remove);
    }
  }
  function editEntity(type, original) {
    dirty = false; selection = {type, token: original.token};
    const value = clone(original);
    const editor = form(`${label(type)} · ${original.Name || original.token}`, type === "Metadata" ? "Metadata changes apply to new RTSP connections. Filters and namespace bindings are validated before saving." : "Saved settings require an application restart to take full effect. Some emulator stream parameters are fixed and only change the advertised configuration.");
    for (const [key, child] of Object.entries(value)) editor.append(input(value, key, child, ["token", "UseCount"].includes(key) || (type === "Analytics" && key === "rules")));
    if (type === "Analytics") editor.append(node("p", "Edit rule geometry and rule parameters in Detectors.", "table-note"));
    if (type === "Metadata") {
      for (const key of ["Analytics", "Filters", "AnalyticsModules"]) if (!(key in value)) editor.append(button(`Add ${label(key)}`, (event) => {
        event.currentTarget.disabled = true;
        value[key] = key === "Analytics" ? false : [];
        editor.insertBefore(input(value, key, value[key]), editor.lastElementChild); markDirty();
      }));
    }
    actions(editor, () => {
      const next = clone(media); next[type] = next[type].map((item) => item.token === original.token ? value : item);
      save("media_profiles.config", JSON.stringify(next), () => editEntity(type, media[type].find((item) => item.token === original.token)));
    });
  }
  function editFile(entry) {
    dirty = false; selection = {file: entry.name};
    const editor = form(entry.name, "Advanced JSON editor. The whole file is validated before saving. Application settings require a restart. Keep __UNCHANGED_SECRET__ to preserve an existing password; enter a new password to replace it.");
    const text = node("textarea", undefined, "configuration-json"); text.rows = 24; text.spellcheck = false;
    text.value = JSON.stringify(JSON.parse(entry.document), null, 2); text.setAttribute("aria-label", `${entry.name} JSON`);
    text.addEventListener("input", markDirty); editor.append(text);
    actions(editor, () => {
      try { JSON.parse(text.value); } catch { popup("Enter a valid JSON document"); return; }
      save(entry.name, text.value, () => editFile(state.files.find((item) => item.name === entry.name)));
    });
  }
  $("configuration-refresh").addEventListener("click", async () => {
    if (!leave() || busy) return;
    dirty = false;
    if (await load()) { selection = null; $("configuration-editor").replaceChildren(node("p", "Select a profile, configuration or file.")); message(""); }
  });
  window.addEventListener("beforeunload", (event) => { if (dirty) { event.preventDefault(); event.returnValue = ""; } });
  window.addEventListener("admin-configuration-saved", () => { if (!dirty && !busy) load(); });
  load();
})();
