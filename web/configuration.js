"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  const truth = (value) => value === true || value === "true";
  const array = (value) => Array.isArray(value) ? value : [];
  const clone = (value) => structuredClone(value);
  const kinds = ["VideoSource", "VideoEncoder", "AudioSource", "AudioEncoder", "AudioOutput", "AudioDecoder", "Metadata", "Analytics", "PTZ", "Receiver"];
  const ownerByType = new Map();
  let state, media, dirty = false, channelsDirty = false, busy = false;
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
  function message(text, failure = false, view = "configuration") {
    const notice = $(`${view}-message`);
    notice.textContent = text;
    notice.className = `notice ${failure ? "error" : "success"}`;
    notice.hidden = !text;
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
  function markDirty(view = "configuration") {
    if (view === "channels") channelsDirty = true;
    else dirty = true;
    message("Unsaved changes. Save configuration to apply them.", false, view);
  }
  function mediaEntry() { return state.files.find((item) => item.name === "media_profiles.config"); }
  function restartNotice(required) {
    $("configuration-restart").hidden = !required;
    $("configuration-restart").textContent = "Saved settings require an application restart to take full effect. Active values in the overview still describe this run.";
    $("channels-restart").hidden = !required;
    $("channels-restart").textContent = "Restart the application to apply saved changes.";
  }
  async function load() {
    try {
      const response = await window.adminFetch("/admin/api/configuration", {cache: "no-store", signal: AbortSignal.timeout(10000)});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      state = await response.json(); media = JSON.parse(mediaEntry().document);
      ownerByType.clear();
      for (const channel of array(state.active_channels)) {
        for (const [type, tokens] of Object.entries(channel.configurations || {})) {
          if (!ownerByType.has(type)) ownerByType.set(type, new Map());
          for (const token of tokens) ownerByType.get(type).set(token, channel.token);
        }
      }
      restartNotice(state.restart_required); catalog();
      return true;
    } catch (error) {
      for (const view of ["configuration", "channels"]) message(`Configuration unavailable: ${error.message}`, true, view);
      return false;
    }
  }
  async function save(file, document, success, view = "configuration", revision = state.files.find((item) => item.name === file).revision) {
    if (busy) return;
    busy = true; $(`${view}-workspace`).inert = true;
    try {
      const response = await window.adminFetch("/admin/api/configuration", {
        method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "configuration"},
        body: JSON.stringify({file, revision, document}), signal: AbortSignal.timeout(20000)
      });
      const result = await response.json();
      if (!response.ok) { popup(result.message || result.error, result.validation_errors || []); message(result.message || result.error, true, view); return; }
      if (view === "channels") channelsDirty = false;
      else dirty = false;
      if (await load()) {
        if (success) success(); else $("configuration-editor").replaceChildren(node("p", "Saved. Select an item to continue editing."));
        if (view === "configuration" && !channelsDirty) editChannels();
        message(result.restart_required ? "Configuration saved. Changes take full effect only after restarting the application." : "Configuration saved and applied. Existing RTSP sessions retain their previous settings; reconnect to use the updated profile or metadata.", false, view);
        if (result.restart_required) $("configuration-saved-dialog").showModal();
      }
    } catch (error) { popup(`Save could not be confirmed: ${error.message}. Reload to inspect the current file before retrying.`); }
    finally { busy = false; $(`${view}-workspace`).inert = false; }
  }
  function channelOwner(type, entry) { return ownerByType.get(type)?.get(entry.token) || ""; }
  function channelName(token) { return array(state.active_channels).find((entry) => entry.token === token)?.name; }
  function catalogTitle(type) {
    if (type === "VideoEncoder") return "Video encoder (Media2)";
    if (type === "VideoEncoderConfigurations") return "Video encoder (Media)";
    return label(type);
  }
  function catalog() {
    const target = $("configuration-catalog");
    const firstRender = !target.querySelector("details");
    const opened = new Set([...target.querySelectorAll("details[open]")].map((group) => group.dataset.group));
    function details(title, key, initiallyOpen = false) {
      const group = node("details");
      group.dataset.group = key; group.open = opened.has(key) || (firstRender && initiallyOpen);
      group.append(node("summary", title));
      return group;
    }
    target.replaceChildren();
    const channels = array(state.active_channels);
    const profiles = array(media.MediaProfiles);
    const catalogs = Object.entries(media).map(([type, entries]) => [type, array(entries).filter((entry) => entry.token)])
      .filter(([type, entries]) => type !== "MediaProfiles" && entries.length);
    const unassigned = {token: "", name: channels.length ? "Unassigned profiles" : "Media configuration"};
    for (const [index, channel] of [...channels, unassigned].entries()) {
      const groupProfiles = profiles.filter((profile) => (profile.channel || "") === channel.token);
      const groupCatalogs = catalogs.map(([type, entries]) => [type, entries.filter((entry) => channelOwner(type, entry) === channel.token)]);
      if (!channel.token && !groupProfiles.length && !groupCatalogs.some(([, entries]) => entries.length)) continue;
      const group = details(channel.name, `channel:${channel.token}`, index === 0);
      group.className = "channel-catalog";
      group.append(node("h3", "Media profiles"));
      for (const profile of groupProfiles) group.append(button(`${profile.Name}${truth(profile.fixed) ? " · fixed" : ""}`, () => { if (leave()) editProfile(profile); }));
      if (groupCatalogs.some(([, entries]) => entries.length)) group.append(node("h3", "ONVIF configurations"));
      for (const [type, entries] of groupCatalogs) {
        if (!entries.length) continue;
        const subGroup = details(catalogTitle(type), `${channel.token}:${type}`);
        for (const entry of entries) subGroup.append(button(entry.Name || entry.token, () => { if (leave()) editEntity(type, entry); }));
        group.append(subGroup);
      }
      target.append(group);
    }
    target.append(button("Create media profile", () => { if (leave()) editProfile(null); }));
    const files = details("Configuration files · advanced", "files");
    for (const entry of state.files.filter((item) => item.name !== "users.config")) files.append(button(entry.name, () => { if (leave()) editFile(entry); }));
    target.append(files);
  }
  function form(title, note, view = "configuration") {
    if (view === "configuration") $("configuration-selected").textContent = title;
    const editor = node("form", undefined, "detector-form configuration-form");
    editor.append(node("p", note, "table-note"));
    editor.addEventListener("invalid", (event) => { event.preventDefault(); popup(event.target.validationMessage); }, true);
    $(`${view}-editor`).replaceChildren(editor);
    return editor;
  }
  function input(parent, key, value, readOnly = false, changed = markDirty) {
    if (value && typeof value === "object" && !Array.isArray(value)) {
      const box = node("fieldset"); box.append(node("legend", label(key)));
      for (const [name, child] of Object.entries(value)) box.append(input(value, name, child, readOnly, changed));
      return box;
    }
    const wrapper = node("label", `${label(key)}${readOnly ? " (read-only)" : ""}`);
    let control;
    if (Array.isArray(value) || (value === "" && ["rules", "analyticsModules", "compatibleModuleAnalytics", "AnalyticsModules"].includes(key))) {
      control = node("textarea"); control.rows = 4; control.value = JSON.stringify(value === "" ? [] : value, null, 2);
      control.addEventListener("input", () => {
        try { const parsed = JSON.parse(control.value); if (!Array.isArray(parsed)) throw new Error(); parent[key] = parsed; control.setCustomValidity(""); }
        catch { control.setCustomValidity("Enter a valid JSON array"); }
        changed();
      });
    } else {
      control = node("input");
      control.type = (typeof value === "boolean" || ["true", "false"].includes(value)) ? "checkbox" : key === "password" ? "password" : "text";
      if (control.type === "checkbox") control.checked = truth(value); else control.value = value ?? "";
      const update = () => { parent[key] = control.type === "checkbox" ? control.checked : control.value; changed(); };
      control.addEventListener("input", update);
      control.addEventListener("change", update);
    }
    control.dataset.configurationKey = key;
    control.readOnly = readOnly; if (control.type === "checkbox") control.disabled = readOnly;
    wrapper.append(control); return wrapper;
  }
  function actions(editor, onSubmit, onCancel = () => {
    dirty = false; $("configuration-editor").replaceChildren(node("p", "Select a profile, configuration or file."));
  }) {
    const row = node("div", undefined, "dialog-actions");
    const submit = node("button", "Save configuration", "button secondary"); submit.type = "submit";
    row.append(submit, button("Cancel edits", onCancel));
    editor.append(row);
    editor.addEventListener("submit", (event) => { event.preventDefault(); onSubmit(); });
    return row;
  }
  function compatible(type, candidate, profile) {
    if (state.active_channels.length) {
      const attachedType = kinds.find((key) => profile[key]);
      const owner = profile.channel || (attachedType && channelOwner(attachedType, {token: profile[attachedType]}));
      if (owner && channelOwner(type, candidate) !== owner) return false;
    }
    const source = array(media.VideoSource).find((item) => item.token === profile.VideoSource);
    if (type === "Metadata" && !state.metadata_enabled) return false;
    if (type === "Analytics" && source) return array(source.CompatibleAnalyticsConfigurations).includes(candidate.token);
    if (type === "PTZ" && source) return array(source.CompatiblePtzNodes).includes(candidate.NodeToken);
    return true;
  }
  function newToken(prefix) {
    const bytes = crypto.getRandomValues(new Uint8Array(12));
    return prefix + "_" + Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
  }
  const channelCode = (index) => `CH${String(index + 1).padStart(2, "0")}`;
  function channelLabel(index, name) {
    return Array.from(`${channelCode(index)} - ${name.replace(/^CH\d+ - /, "")}`).slice(0, 64).join("");
  }
  function templateName(type, item, index) {
    const name = item.Name;
    if (type === "MediaProfiles" && /^MainProfile$/i.test(name)) return "Main stream";
    if (type === "MediaProfiles" && /^SecondProfile$/i.test(name)) return "Secondary stream";
    if (type === "VideoSource" && name === item.token) return "Video source";
    if (["VideoEncoder", "VideoEncoderConfigurations"].includes(type) && /^VideoEncConfig\d+$/.test(name))
      return `Video encoder ${index + 1}`;
    return name;
  }
  function copyChannel(template, index) {
    const channel = clone(template);
    const code = channelCode(index);
    const media = channel.media;
    const replacements = new Map();
    function assign(type, old, prefix) {
      if (!replacements.has(type)) replacements.set(type, new Map());
      const tokens = replacements.get(type);
      if (!tokens.has(old)) tokens.set(old, newToken(`${prefix}_${code}_${tokens.size + 1}`));
      return tokens.get(old);
    }
    const video = assign("VideoSources", channel.videoSource.token, "VideoSource");
    for (const [type, entries] of Object.entries(media)) {
      const primary = type === "VideoEncoderConfigurations" ? "VideoEncoder" : type === "PTZConfigurationOptions" ? "PTZ" : type;
      const field = type === "AudioSources" ? "Token" : "token";
      const prefix = type === "AudioSources" ? "AudioSource" : type === "MediaProfiles" ? "Profile" : `${primary}Config`;
      for (const [position, item] of array(entries).entries()) {
        if (item.Name) item.Name = channelLabel(index, templateName(type, item, position));
        if (item[field]) item[field] = assign(primary, item[field], prefix);
        if (type === "PTZ") item.NodeToken = assign("PTZNodes", item.NodeToken, "PTZNode");
      }
    }
    const token = (type, value) => replacements.get(type)?.get(value) || value;
    channel.token = newToken(`Channel_${code}`);
    channel.name = `Channel ${index + 1}`;
    channel.videoSource.token = video;
    for (const profile of media.MediaProfiles) {
      delete profile.channel;
      for (const type of kinds) if (profile[type]) profile[type] = token(type, profile[type]);
    }
    for (const source of media.VideoSource) {
      source.SourceToken = video;
      for (const [field, type] of [["CompatiblePtzNodes", "PTZNodes"], ["CompatibleAnalyticsConfigurations", "Analytics"]])
        if (Array.isArray(source[field])) source[field] = source[field].map((item) => token(type, item));
    }
    for (const source of array(media.AudioSource)) source.SourceToken = token("AudioSources", source.SourceToken);
    return channel;
  }
  function editChannels() {
    channelsDirty = false;
    const changed = () => markDirty("channels");
    const entry = state.files.find((item) => item.name === "channels.config");
    const document = JSON.parse(entry.document);
    const editor = form("Saved topology", "You can use an existing channel as a template when increasing the channel count.", "channels");
    const active = array(state.active_channels);
    editor.append(node("p", `Active channels (${active.length}): ${active.map((channel) => channel.name).join(", ")}`, "table-note"));
    const controls = node("fieldset"); controls.append(node("legend", "Saved topology"));
    const resizeLabel = node("label", "Channel count"); const count = node("input");
    count.type = "number"; count.required = true; count.min = 1; count.max = 32; count.step = 1; count.value = document.channels.length;
    resizeLabel.append(count);
    const templateLabel = node("label", "Template for new channels"), templates = node("select");
    templateLabel.append(templates); controls.append(resizeLabel, templateLabel);
    const list = node("div", undefined, "channel-editor-list");
    const disabledComponents = new Map();
    function render() {
      const selectedTemplate = templates.value;
      templates.replaceChildren(...document.channels.map((channel, index) => {
        const choice = node("option", channel.name); choice.value = index; return choice;
      }));
      if (selectedTemplate && Number(selectedTemplate) < document.channels.length) templates.value = selectedTemplate;
      const opened = new Set([...list.querySelectorAll("details[open]")].map((item) => item.dataset.channel));
      list.replaceChildren();
      for (const [index, channel] of document.channels.entries()) {
        const section = node("details");
        section.dataset.channel = channel.token; section.open = opened.has(channel.token);
        const title = node("summary", channel.name);
        const nameInput = input(channel, "name", channel.name, false, () => {
          title.textContent = channel.name;
          templates.options[index].textContent = channel.name;
          changed();
        });
        nameInput.querySelector("input").required = true;
        section.append(title, nameInput);
        const catalog = channel.media;
        const components = [
          ["Audio", ["AudioSources", "AudioSource", "AudioEncoder", "AudioEncoderConfigurationOptions"], ["AudioSource", "AudioEncoder"]],
          ["PTZ", ["PTZ", "PTZConfigurationOptions"], ["PTZ"]],
          ["Analytics", ["Analytics"], ["Analytics"]],
          ["Metadata", ["Metadata"], ["Metadata"]]
        ];
        for (const [label, catalogs, fields] of components) {
          const component = node("label", label); const enabled = node("input");
          enabled.type = "checkbox"; enabled.checked = array(catalog[catalogs[0]]).length > 0;
          enabled.disabled = label === "Metadata" && !state.metadata_enabled;
          const allow = label === "PTZ" ? "CompatiblePtzNodes" : label === "Analytics" ? "CompatibleAnalyticsConfigurations" : "";
          enabled.addEventListener("change", () => {
            const key = `${channel.token}:${label}`;
            if (!enabled.checked)
              disabledComponents.set(key, Object.fromEntries(catalogs.map((type) => [type, catalog[type]])));
            const saved = disabledComponents.get(key) || {};
            const needsTemplate = enabled.checked && !array(saved[catalogs[0]]).length;
            const defaults = needsTemplate ? copyChannel(JSON.parse(entry.document).channels[0], index).media : {};
            for (const type of catalogs) catalog[type] = enabled.checked ? saved[type] || defaults[type] || [] : [];
            if (allow) {
              const tokens = array(catalog[label]).map((item) => label === "PTZ" ? item.NodeToken : item.token);
              for (const source of catalog.VideoSource) source[allow] = [...new Set(tokens)];
            }
            if (!enabled.checked) for (const profile of catalog.MediaProfiles) for (const field of fields) delete profile[field];
            if (enabled.checked && !catalog[catalogs[0]].length) { enabled.checked = false; popup(`Choose a template that defines ${label}.`); }
            changed(); render();
          });
          component.prepend(enabled); section.append(component);
        }
        for (const profile of catalog.MediaProfiles) {
          const stream = node("fieldset"); stream.append(node("legend", profile.Name), input(profile, "Name", profile.Name, false, changed));
          const encoder = catalog.VideoEncoder.find((item) => item.token === profile.VideoEncoder);
          const legacy = catalog.VideoEncoderConfigurations.find((item) => item.token === profile.VideoEncoder);
          if (encoder && legacy) {
            for (const [label, name] of [["Width", "Width"], ["Height", "Height"], ["Frame rate", "FrameRateLimit"], ["Bitrate (kbit/s)", "BitrateLimit"]]) {
              const a = name.endsWith("Limit") ? encoder.RateControl : encoder.Resolution;
              const b = name.endsWith("Limit") ? legacy.RateControl : legacy.Resolution;
              const wrap = node("label", label), number = node("input");
              number.type = "number"; number.min = 1; number.step = 1; number.required = true; number.value = a[name];
              number.addEventListener("input", () => { a[name] = b[name] = number.value; changed(); });
              wrap.append(number); stream.append(wrap);
            }
          }
          for (const type of ["AudioSource", "AudioEncoder", "Metadata", "Analytics", "PTZ"]) {
            if (!array(catalog[type]).length) continue;
            const wrap = node("label", label(type)), select = node("select");
            for (const item of [{token: "", Name: "Not attached"}, ...catalog[type]]) {
              const choice = node("option", item.Name || item.token); choice.value = item.token; select.append(choice);
            }
            select.value = profile[type] || "";
            select.addEventListener("change", () => { profile[type] = select.value; changed(); });
            wrap.append(select); stream.append(wrap);
          }
          if (catalog.MediaProfiles.length > 1) stream.append(button("Remove stream", () => {
            catalog.MediaProfiles = catalog.MediaProfiles.filter((item) => item !== profile); changed(); render();
          }, "danger"));
          section.append(stream);
        }
        section.append(button("Add stream", () => {
          const base = catalog.MediaProfiles.find((profile) => profile.VideoEncoder && profile.VideoSource);
          if (!base) { popup("Attach a video source and encoder to a template stream."); return; }
          const code = channelCode(index);
          const token = newToken(`VideoEncoderConfig_${code}`);
          let number = catalog.MediaProfiles.length + 1;
          while (catalog.MediaProfiles.some((item) => item.Name === channelLabel(index, `Stream ${number}`)) ||
                 catalog.VideoEncoder.some((item) => item.Name === channelLabel(index, `Video encoder ${number}`))) ++number;
          for (const type of ["VideoEncoder", "VideoEncoderConfigurations"]) {
            catalog[type].push({...clone(catalog[type].find((item) => item.token === base.VideoEncoder)), token, Name: channelLabel(index, `Video encoder ${number}`)});
          }
          catalog.MediaProfiles.push({...clone(base), token: newToken(`Profile_${code}`), VideoEncoder: token, Name: channelLabel(index, `Stream ${number}`)});
          changed(); render();
        }));
        list.append(section);
      }
    }
    controls.append(button("Apply channel count", () => {
      if (!count.reportValidity()) return;
      const size = Number(count.value);
      const removed = document.channels.slice(size);
      if (removed.length) {
        const profiles = removed.flatMap((channel) => channel.media.MediaProfiles.map((profile) => profile.Name));
        if (!window.confirm(`Remove channels and their profiles after restart? ${profiles.join(", ")}`)) return;
      }
      const template = document.channels[Number(templates.value)];
      document.channels.length = Math.min(size, document.channels.length);
      while (document.channels.length < size) document.channels.push(copyChannel(template, document.channels.length));
      changed(); render();
    }));
    render(); editor.append(controls, list);
    actions(editor, () => save("channels.config", JSON.stringify(document), editChannels, "channels", entry.revision),
      () => { editChannels(); message("", false, "channels"); });
  }
  function editProfile(original) {
    dirty = !original;
    const profile = original ? clone(original) : {token: `WebProfile_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`, Name: "New profile", fixed: false};
    const editor = form(original ? `Media profile · ${profile.Name}` : "Create media profile", "Select a configuration to attach it. Choose Not attached to remove an association. Profiles and metadata apply to new connections without an application restart.");
    if (profile.channel) editor.append(node("p", channelName(profile.channel) || profile.channel, "table-note"));
    editor.append(input(profile, "Name", profile.Name), node("p", `Token: ${profile.token}`, "mono table-note"));
    const bindings = node("div", undefined, "configuration-bindings");
    function renderBindings() {
      bindings.replaceChildren();
      for (const type of kinds) {
        const candidates = array(media[type]); if (!candidates.length && !profile[type]) continue;
        const wrapper = node("label", label(type)); const select = node("select"); select.dataset.configurationType = type;
        const empty = node("option", "Not attached"); empty.value = ""; select.append(empty);
        const groups = new Map();
        for (const candidate of candidates) {
          const option = node("option", `${candidate.Name || candidate.token} · ${candidate.token}`);
          option.value = candidate.token; option.disabled = !compatible(type, candidate, profile);
          const owner = channelOwner(type, candidate);
          if (!owner) { select.append(option); continue; }
          if (!groups.has(owner)) {
            const group = node("optgroup");
            group.label = channelName(owner) || owner;
            groups.set(owner, group); select.append(group);
          }
          groups.get(owner).append(option);
        }
        select.value = profile[type] || "";
        select.addEventListener("change", () => {
          profile[type] = select.value; markDirty();
          if (state.active_channels.length || type === "VideoSource") {
            renderBindings();
            bindings.querySelector(`[data-configuration-type="${type}"]`)?.focus();
          }
        });
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
    dirty = false;
    const value = clone(original);
    const editor = form(`${catalogTitle(type)} · ${original.Name || original.token}`, type === "Metadata" ? "Metadata changes apply to new RTSP connections. Filters and namespace bindings are validated before saving." : "Saved settings require an application restart to take full effect. Some emulator stream parameters are fixed and only change the advertised configuration.");
    const owner = channelOwner(type, original);
    if (owner) editor.append(node("p", channelName(owner) || owner, "table-note"));
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
      if (owner && value.Name !== original.Name && ["VideoEncoder", "VideoEncoderConfigurations"].includes(type)) {
        const pairedType = type === "VideoEncoder" ? "VideoEncoderConfigurations" : "VideoEncoder";
        const paired = next[pairedType].find((item) => item.token === value.token);
        if (paired) paired.Name = value.Name;
      }
      save("media_profiles.config", JSON.stringify(next), () => editEntity(type, media[type].find((item) => item.token === original.token)));
    });
  }
  function editFile(entry) {
    dirty = false;
    const editor = form(entry.name, "Advanced JSON editor. The whole file is validated before saving. Application settings require a restart. Keep __UNCHANGED_SECRET__ to preserve an existing password; enter a new password to replace it.");
    const text = node("textarea", undefined, "configuration-json"); text.rows = 24; text.spellcheck = false;
    text.value = JSON.stringify(JSON.parse(entry.document), null, 2); text.setAttribute("aria-label", `${entry.name} JSON`);
    text.addEventListener("input", () => markDirty()); editor.append(text);
    actions(editor, () => {
      try { JSON.parse(text.value); } catch { popup("Enter a valid JSON document"); return; }
      save(entry.name, text.value, () => editFile(state.files.find((item) => item.name === entry.name)), "configuration", entry.revision);
    });
  }
  $("configuration-refresh").addEventListener("click", async () => {
    if (!leave() || busy) return;
    dirty = false;
    if (await load()) { $("configuration-editor").replaceChildren(node("p", "Select a profile, configuration or file.")); message(""); }
  });
  $("channels-refresh").addEventListener("click", async () => {
    if (busy || (channelsDirty && !window.confirm("Discard unsaved channel changes?"))) return;
    if (await load()) { editChannels(); message("", false, "channels"); }
  });
  window.addEventListener("beforeunload", (event) => { if (dirty || channelsDirty) { event.preventDefault(); event.returnValue = ""; } });
  window.addEventListener("admin-configuration-saved", async () => {
    if (!dirty && !busy && await load() && !channelsDirty) editChannels();
  });
  const networkForms = [...document.querySelectorAll("[data-network-file]")];
  const networkDirty = new Set();
  let networkFiles = [], networkBusy = false;
  const networkDefaults = {
    "http.enabled": true, "https.enabled": true, "addresses.https_port": 8443,
    "https.certificate": "", "https.private_key": "", "advertised.ipv4": "127.0.0.1",
    "advertised.web_scheme": "auto", "advertised.http_port": 0, "advertised.https_port": 0,
    "advertised.rtsp_port": 0, "advertised.pullpoint_port": 0, "rtp.port_min": 50000,
    "rtp.port_max": 50199, "enabled": true, "interface_ipv4": ""
  };
  function networkMessage(text, failure = false) {
    $("network-message").textContent = text;
    $("network-message").className = `notice ${failure ? "error" : "success"}`;
    $("network-message").hidden = !text;
  }
  function networkLock(value) {
    networkBusy = value; $("network-workspace").inert = value; $("network-refresh").disabled = value;
  }
  async function loadNetwork() {
    const response = await window.adminFetch("/admin/api/configuration", {cache: "no-store", signal: AbortSignal.timeout(10000)});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const result = await response.json();
    for (const form of networkForms) {
      const file = form.dataset.networkFile;
      if (networkDirty.has(file)) continue;
      const entry = result.files.find((item) => item.name === file);
      networkFiles = networkFiles.filter((item) => item.name !== file).concat(entry);
      const document = JSON.parse(entry.document);
      for (const control of form.querySelectorAll("[data-network-path]")) {
        const path = control.dataset.networkPath;
        const value = path.split(".").reduce((value, key) => value?.[key], document) ?? networkDefaults[path] ?? "";
        if (control.type === "checkbox") control.checked = truth(value); else control.value = value;
      }
    }
    $("network-restart").hidden = !result.restart_required;
    $("network-restart").textContent = "Saved settings require an application restart. Reconnect to the new address if the port or allowed connections change.";
  }
  for (const form of networkForms) {
    form.addEventListener("input", () => { networkDirty.add(form.dataset.networkFile); networkMessage("Unsaved network settings."); });
    form.addEventListener("submit", async (event) => {
      event.preventDefault(); if (networkBusy) return;
      const file = form.dataset.networkFile;
      const entry = networkFiles.find((item) => item.name === file);
      if (!entry) { networkMessage("Reload network settings before saving.", true); return; }
      const document = JSON.parse(entry.document);
      for (const control of form.querySelectorAll("[data-network-path]")) {
        const path = control.dataset.networkPath.split(".");
        let parent = document;
        for (const key of path.slice(0, -1)) parent = parent[key] ??= {};
        parent[path.at(-1)] = control.type === "checkbox" ? control.checked : control.dataset.networkType === "number" ? Number(control.value) : control.value;
      }
      networkLock(true);
      try {
        const response = await window.adminFetch("/admin/api/configuration", {
          method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "configuration"},
          body: JSON.stringify({file, revision: entry.revision, document: JSON.stringify(document)}), signal: AbortSignal.timeout(20000)
        });
        const result = await response.json();
        if (!response.ok) { popup(result.message || result.error, result.validation_errors || []); networkMessage(result.message || result.error, true); return; }
        networkDirty.delete(file); await loadNetwork();
        networkMessage("Network settings saved. Restart the application to apply them; reconnect to the new address if needed.");
        window.dispatchEvent(new Event("admin-configuration-saved"));
        if (result.restart_required) $("configuration-saved-dialog").showModal();
      } catch (error) { networkMessage(`Save could not be confirmed: ${error.message}. Reload to inspect saved settings.`, true); }
      finally { networkLock(false); }
    });
  }
  $("network-refresh").addEventListener("click", async () => {
    if (networkBusy || (networkDirty.size && !window.confirm("Discard unsaved network settings?"))) return;
    networkLock(true);
    try {
      networkDirty.clear(); await loadNetwork(); networkMessage("");
    } catch (error) { networkMessage(`Network settings unavailable: ${error.message}`, true); }
    finally { networkLock(false); }
  });
  window.addEventListener("beforeunload", (event) => { if (networkDirty.size) { event.preventDefault(); event.returnValue = ""; } });
  window.addEventListener("admin-configuration-saved", () => {
    if (!networkBusy) loadNetwork().catch((error) => networkMessage(`Network settings unavailable: ${error.message}`, true));
  });
  networkLock(true);
  loadNetwork().then(() => networkLock(false)).catch((error) => { networkLock(false); $("network-workspace").inert = true; networkMessage(`Network settings unavailable: ${error.message}`, true); });
  function recordingsMessage(text) {
    $("recordings-message").textContent = text;
    $("recordings-message").hidden = !text;
  }
  async function loadRecordings() {
    try {
      const response = await window.adminFetch("/admin/api/recordings", {cache: "no-store", signal: AbortSignal.timeout(10000)});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const result = await response.json();
      const channels = array(result.channels);
      const jobs = new Map(array(result.jobs).map((job) => [job.channel, job]));
      const target = $("recordings-jobs"); target.replaceChildren();
      for (const channel of channels) {
        const job = jobs.get(channel.token); jobs.delete(channel.token);
        const row = node("tr");
        const select = node("select"); select.setAttribute("aria-label", `Stream for ${channel.name}`);
        for (const stream of array(channel.profiles)) {
          const option = node("option", stream.name); option.value = stream.token; select.append(option);
        }
        if (job) {
          if (![...select.options].some((item) => item.value === job.profile)) {
            const option = node("option", `${job.profile} (unavailable)`); option.value = job.profile; select.append(option);
          }
          select.value = job.profile; select.disabled = true;
        }
        const selected = node("td"); selected.append(select);
        const active = job?.mode === "Active";
        const action = node("td");
        const submit = button(active ? "Stop" : "Start", async () => {
          submit.disabled = true;
          try {
            const response = await window.adminFetch("/admin/api/recordings", {
              method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "recordings"},
              body: JSON.stringify({action: active ? "stop" : "start", channel: channel.token, profile: select.value}),
              signal: AbortSignal.timeout(10000)
            });
            const result = await response.json();
            if (!response.ok) throw new Error(result.error);
            recordingsMessage(""); await loadRecordings();
          } catch (error) { recordingsMessage(error.message); submit.disabled = false; }
        });
        submit.disabled = !active && !array(channel.profiles).some((stream) => !job || stream.token === job.profile);
        action.append(submit);
        row.append(node("th", channel.name), selected, node("td", job?.mode || "Idle"), action);
        target.append(row);
      }
      for (const job of jobs.values()) {
        const row = node("tr");
        row.append(node("th", job.name), node("td", job.profile), node("td", job.mode), node("td", "Archived"));
        target.append(row);
      }
    } catch (error) { recordingsMessage(error.message); }
  }
  $("recordings-refresh").addEventListener("click", loadRecordings);
  window.addEventListener("hashchange", () => { if (location.hash === "#recordings") loadRecordings(); });
  if (location.hash === "#recordings") loadRecordings();
  load().then((loaded) => { if (loaded) editChannels(); });
})();
