"use strict";
(() => {
  const el = (id) => document.getElementById(id);
  const canvas = el("detector-scene");
  const ctx = canvas.getContext("2d");
  const clone = (value) => structuredClone(value);
  const truth = (value) => value === true || value === "true";
  const label = (key) => key.replace(/^tt:/, "").replace(/([a-z])([A-Z])/g, "$1 $2");
  let data = null, selected = null, draft = null, previous = null;
  let configuration = null, editing = false, saving = false, loading = false;
  let handles = [], dragged = null;
  let loadDone = Promise.resolve();

  function node(tag, text, className) {
    const item = document.createElement(tag);
    if (text !== undefined) item.textContent = String(text);
    if (className) item.className = className;
    return item;
  }
  function message(text, error = false) {
    el("detector-message").textContent = text;
    el("detector-message").className = `notice ${error ? "error" : ""}`;
    el("detector-message").hidden = !text;
  }
  const errorDialog = el("config-error-dialog");
  let errorReturnFocus = null;
  function validationPopup(summary, issues = []) {
    if (!errorDialog.open) errorReturnFocus = document.activeElement;
    el("config-error-summary").textContent = summary;
    const details = el("config-error-details");
    details.replaceChildren();
    for (const issue of issues) {
      const row = node("li");
      row.append(node("strong", `${issue.file || "Settings"} · ${issue.path || "/"}`, "mono"),
        node("p", issue.message || "Invalid value"));
      details.append(row);
    }
    details.hidden = !issues.length;
    if (!errorDialog.open) errorDialog.showModal();
  }
  errorDialog.addEventListener("close", () => {
    if (errorReturnFocus?.isConnected) errorReturnFocus.focus();
  });

  function button(text, callback, kind = "secondary") {
    const item = node("button", text, `button ${kind}`);
    item.type = "button";
    item.addEventListener("click", callback);
    return item;
  }
  async function change(command) {
    if (saving) return;
    saving = true;
    const sourceFocus = document.activeElement;
    el("detector-settings").inert = true;
    el("detector-catalog").inert = true;
    try {
      const response = await window.adminFetch("/admin/api/detectors", {
        method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "detectors"},
        body: JSON.stringify(command), signal: AbortSignal.timeout(10000)
      });
      const result = await response.json();
      if (!response.ok) {
        const errors = {InvalidRule: "Invalid rule settings: check geometry, coordinate ranges and parameter values", DetectorDisabled: "Enable the detector, its rule and the rule engine before running it", DetectorUnavailable: "The detector is unavailable", ConfigFileChanged: "event.config changed externally. Restart the application to reload it before saving detector settings", SettingsChanged: "Settings changed in another client. Refresh before editing", TooManyRules: "The limit of four rules has been reached", RuleAlreadyExistent: "A rule with this name already exists", RuleNotExistent: "The rule no longer exists", ConfigurationConflict: "The rule could not be saved; check configuration access and application logs"};
        const failure = new Error(result.message || errors[result.error] || result.error || `HTTP ${response.status}`);
        failure.issues = Array.isArray(result.validation_errors) ? result.validation_errors : [];
        throw failure;
      }
      editing = false;
      if (command.action === "delete") selected = null;
      else if (command.action === "create" || command.action === "modify") selected = {configuration: command.configuration, name: command.name, creating: false};
      draft = null;
      el("detector-settings").replaceChildren(node("p", "Reloading current settings…"));
      draw();
      if (await load(true)) message(command.action === "manual" ? "Detector ran once. Events depend on the current rule conditions and state." : "Detector settings updated.");
    } catch (error) {
      message(`${error.message}. Changes were not confirmed. Refresh and inspect current settings before retrying.`, true);
      validationPopup(error.message, error.issues);
      errorReturnFocus = sourceFocus;
    } finally {
      saving = false;
      el("detector-settings").inert = false;
      el("detector-catalog").inert = false;
    }
  }
  function toggle(text, checked, command) {
    const wrapper = node("label", undefined, "detector-toggle");
    const input = node("input");
    input.type = "checkbox";
    input.checked = truth(checked);
    input.addEventListener("change", () => {
      const next = input.checked;
      input.checked = !next;
      if (!mayLeave()) return;
      change({...command, enabled: next});
    });
    wrapper.append(input, node("span", text));
    return wrapper;
  }
  function mayLeave() {
    return !editing || window.confirm("Discard unsaved detector changes?");
  }
  function detectorButton(text, enabled, callback) {
    const item = button(text, callback, truth(enabled) ? "detector-enabled" : "detector-disabled");
    item.title = `${text}: ${truth(enabled) ? "enabled" : "disabled"}`;
    item.setAttribute("aria-label", item.title);
    return item;
  }
  function manualButton(id, enabled, available, scope = {}) {
    const item = button("Run detector once", () => change({action: "manual", id, ...scope}));
    item.classList.add("manual-generate");
    item.disabled = !enabled || !truth(available) || editing;
    item.dataset.allowed = String(enabled && truth(available));
    item.title = "Runs one simulation step using the same method as the automatic timer. Rule conditions determine whether events are emitted. Save pending changes first.";
    return item;
  }
  function invalidPopup(event) {
    event.preventDefault();
    if (!errorDialog.open) {
      validationPopup("Check the setting before saving.", [{file: "Settings", path: (draft ? "/rule/" : "/") + (event.target.dataset.validationPath || ""), message: event.target.validationMessage || "Invalid value"}]);
      errorReturnFocus = event.target;
    }
  }
  function showSettings(item, family = false) {
    selected = family ? {family: item.id} : {simple: item.id};
    draft = null; previous = null; editing = false;
    el("detector-selected-title").textContent = label(item.id);
    const form = node("form", undefined, "detector-form");
    form.addEventListener("invalid", invalidPopup, true);
    const values = {};
    for (const key of ["Enabled", "AutoGenerateEvents", "EventGenerationTimeout"]) {
      const wrapper = node("label", key === "EventGenerationTimeout" ? "Event generation timeout (seconds)" : key === "AutoGenerateEvents" ? "Auto generate events" : "Enabled");
      const input = node("input");
      input.dataset.validationPath = `${item.id}/${key}`;
      if (key === "EventGenerationTimeout") {
        input.type = "number"; input.min = "1"; input.max = "2147483647"; input.step = "1"; input.required = true;
        input.value = item.settings[key]; values[key] = Number(input.value);
      } else {
        input.type = "checkbox"; input.checked = truth(item.settings[key]); values[key] = input.checked;
      }
      input.addEventListener("input", () => {
        values[key] = input.type === "checkbox" ? input.checked : Number(input.value);
        dirty();
      });
      wrapper.append(input); form.append(wrapper);
    }
    const readonly = node("dl", undefined, "key-values");
    for (const [key, value] of Object.entries(item.settings)) {
      if (!(key in values)) readonly.append(node("dt", `${label(key)} (read-only)`), node("dd", value));
    }
    if (readonly.children.length) form.append(readonly);
    if (family) form.append(node("p", "These settings apply to all rules of this detector type. The rule engine must also be enabled.", "table-note"));
    const actions = node("div", undefined, "dialog-actions");
    const save = node("button", "Save changes", "button secondary"); save.type = "submit";
    actions.append(save, manualButton(item.id, truth(item.enabled) && (!family || truth(data.rules_enabled)), item.manual_available));
    form.append(actions);
    form.addEventListener("submit", (event) => {
      event.preventDefault(); change({action: "settings", id: item.id, settings: values});
    });
    el("detector-settings").replaceChildren(form);
    draw();
  }
  function showSimple(item) { showSettings(item); }
  function showParent() {
    selected = {parent: true}; draft = null; editing = false;
    el("detector-selected-title").textContent = "Rule engine";
    el("detector-settings").replaceChildren(
      toggle("Enabled", data.rules_enabled, {action: "detector", id: "rules"}),
      node("p", "Enables or disables all rule-based detectors. Select a detector type to configure its timer, or a rule to edit its parameters.", "table-note"));
    draw();
  }
  function catalog() {
    const target = el("detector-catalog");
    const firstRender = !target.children.length;
    const opened = new Set([...target.querySelectorAll("details[open]")].map((item) => item.dataset.key));
    target.replaceChildren(node("h3", "Built-in detectors"));
    for (const item of data.simple) {
      const row = node("div", undefined, "detector-row");
      row.append(detectorButton(label(item.id), item.enabled, () => {
        if (!mayLeave()) return;
        showSimple(item);
      }));
      target.append(row);
    }
    target.append(node("h3", "Rule-based analytics"), detectorButton("Rule engine", data.rules_enabled, () => { if (mayLeave()) showParent(); }));
    for (const cfg of data.configurations) {
      const group = node("div", undefined, "detector-rule-group");
      group.append(node("h4", cfg.name), node("p", cfg.token, "muted mono"));
      for (const family of data.families) {
        const rules = cfg.rules.filter((rule) => rule.Type === family.id);
        const details = node("details");
        details.dataset.key = `${cfg.token}|${family.id}`;
        details.open = opened.has(details.dataset.key) || (firstRender && rules.length > 0);
        details.append(node("summary", `${label(family.id)} (${rules.length})`));
        details.append(node("p", `Synthetic interval: ${family.interval_seconds} s`, "table-note"));
        details.append(detectorButton("Detector settings", truth(family.enabled) && truth(data.rules_enabled), () => { if (mayLeave()) showSettings(family, true); }));
        for (const rule of rules) {
          const row = node("div", undefined, "detector-row");
          row.append(detectorButton(rule.Name, truth(rule.enabled) && truth(family.enabled) && truth(data.rules_enabled), () => { if (mayLeave()) openRule(cfg, rule, false); }));
          details.append(row);
        }
        details.append(button("Create rule", () => {
          const template = cfg.templates.find((rule) => rule.Type === family.id);
          if (!template) { message("This configuration has no compatible video source.", true); return; }
          if (mayLeave()) openRule(cfg, template, true);
        }));
        group.append(details);
      }
      target.append(group);
    }
    target.append(node("p", `Maximum ${data.max_rules} rules across all configurations.`, "table-note"));
  }
  function openRule(cfg, rule, creating) {
    configuration = cfg;
    selected = {configuration: cfg.token, name: rule.Name, creating, enabled: truth(rule.enabled)};
    previous = clone(rule);
    delete previous.enabled;
    draft = clone(previous);
    editing = creating;
    renderEditor();
  }
  function dirty() { editing = true; document.querySelectorAll(".manual-generate").forEach((item) => { item.disabled = true; }); message("Unsaved changes. Save to apply detector settings."); draw(); }
  function geometryPaths(value, path = []) {
    const result = [];
    if (Array.isArray(value) && value.length && value.every((point) => point && "x" in point && "y" in point)) {
      result.push({path, points: value, open: path.includes("Segments") || path.includes("SnapLine")});
    } else if (value && typeof value === "object") {
      for (const [key, child] of Object.entries(value)) result.push(...geometryPaths(child, [...path, key]));
    }
    return result;
  }
  function screen(point) {
    const bounds = configuration.bounds;
    if (draft.Type === "tt:MotionRegionDetector") {
      return [(Number(point.x) - Number(bounds.x)) / Number(bounds.width) * canvas.width,
        (Number(point.y) - Number(bounds.y)) / Number(bounds.height) * canvas.height];
    }
    return [(Number(point.x) + 1) / 2 * canvas.width, (1 - Number(point.y)) / 2 * canvas.height];
  }
  function draw() {
    ctx.fillStyle = "#243c4b";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#bdcbd1";
    ctx.font = "18px system-ui";
    ctx.fillText("SIMULATED FRAME", 22, 34);
    handles = [];
    if (!draft) {
      el("detector-coordinate-note").textContent = "No geometric settings for this detector. The scene is schematic, not live video.";
      return;
    }
    const geometries = geometryPaths(draft);
    geometries.forEach((shape, index) => {
      const color = ["#64e2ca", "#ffd48b", "#8cc8ff"][index % 3];
      const points = shape.points.map(screen);
      ctx.beginPath();
      points.forEach(([x, y], i) => { if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y); });
      if (!shape.open) ctx.closePath();
      ctx.strokeStyle = color; ctx.lineWidth = 3; ctx.stroke();
      if (!shape.open) { ctx.fillStyle = `${color}30`; ctx.fill(); }
      points.forEach(([x, y], i) => {
        ctx.beginPath(); ctx.arc(x, y, 7, 0, Math.PI * 2); ctx.fillStyle = color; ctx.fill();
        ctx.fillText(String(i + 1), x + 11, Math.max(20, y - 11));
        handles.push({x, y, point: shape.points[i]});
      });
      if (shape.open && points.length > 1) {
        const [a, b] = points;
        const angle = Math.atan2(b[1] - a[1], b[0] - a[0]);
        const x = (a[0] + b[0]) / 2, y = (a[1] + b[1]) / 2;
        ctx.save(); ctx.translate(x, y); ctx.rotate(angle);
        ctx.beginPath(); ctx.moveTo(-8, -8); ctx.lineTo(8, 0); ctx.lineTo(-8, 8);
        ctx.strokeStyle = color; ctx.stroke(); ctx.restore();
      }
    });
    el("detector-coordinate-note").textContent = draft.Type === "tt:MotionRegionDetector"
      ? `Pixel coordinates within source bounds (${configuration.bounds.width} × ${configuration.bounds.height}). Drag vertices or edit coordinates below.`
      : geometries.length ? "Normalized coordinates: x −1…1 left to right; y −1…1 bottom to top. Drag vertices or edit coordinates below. Arrows show vertex order; Direction selects the crossing side."
        : "This rule has no geometric parameters. The scene is schematic, not live video.";
  }
  function field(parent, key, value, path) {
    if (Array.isArray(value)) {
      if (value.length && value.every((p) => p && "x" in p && "y" in p)) {
        const box = node("fieldset", undefined, "detector-points");
        box.append(node("legend", path.join(" / ")));
        value.forEach((point, i) => {
          const row = node("div", undefined, "detector-point");
          row.append(node("span", `Vertex ${i + 1}`));
          for (const axis of ["x", "y"]) {
            const wrapper = node("label", axis);
            const input = node("input"); input.type = "number"; input.step = "any"; input.value = point[axis];
            input.dataset.validationPath = [...path, String(i), axis].join("/");
            input.addEventListener("input", () => { point[axis] = input.value; dirty(); });
            wrapper.append(input); row.append(wrapper);
          }
          row.append(button("Remove", () => {
            const minimum = path.includes("Segments") || path.includes("SnapLine") ? 2 : 3;
            if (value.length <= minimum) { message(`At least ${minimum} vertices are required.`, true); return; }
            value.splice(i, 1); dirty(); renderEditor();
          }));
          box.append(row);
        });
        box.append(button("Add vertex", () => {
          if (value.length >= 8) { message("A polygon or line supports at most eight vertices.", true); return; }
          const a = value[value.length - 1], b = value.length > 1 ? value[0] : a;
          value.push({x: String((Number(a.x) + Number(b.x)) / 2), y: String((Number(a.y) + Number(b.y)) / 2)});
          dirty(); renderEditor();
        }));
        return box;
      }
      if (value.some(Array.isArray)) {
        const box = node("fieldset"); box.append(node("legend", label(key)));
        value.forEach((line, i) => { box.append(field(value, String(i), line, [...path, String(i)]), button(`Remove line ${i + 1}`, () => {
          if (value.length <= 1) { message("At least one polyline is required.", true); return; }
          value.splice(i, 1); dirty(); renderEditor();
        })); });
        box.append(button("Add line", () => {
          if (value.length >= 4) { message("At most four polylines are supported.", true); return; }
          value.push([{x: "-0.5", y: "-1"}, {x: "-0.5", y: "1"}]); dirty(); renderEditor(); }));
        return box;
      }
      const wrapper = node("label", `${label(key)} (JSON array)`);
      const input = node("textarea"); input.value = JSON.stringify(value); input.rows = 2;
      input.dataset.validationPath = path.join("/");
      input.addEventListener("input", dirty);
      input.addEventListener("change", () => {
        try { const parsed = JSON.parse(input.value); if (!Array.isArray(parsed)) throw new Error(); parent[key] = parsed; input.setCustomValidity(""); dirty(); }
        catch { input.setCustomValidity("Enter a valid JSON array."); }
      });
      wrapper.append(input); return wrapper;
    }
    if (value && typeof value === "object") {
      const box = node("fieldset"); box.append(node("legend", label(key)));
      for (const [child, content] of Object.entries(value)) box.append(field(value, child, content, [...path, child]));
      return box;
    }
    const wrapper = node("label", label(key));
    let input;
    if (key === "Direction") {
      input = node("select"); for (const option of ["Any", "Left", "Right"]) input.append(node("option", option));
      input.value = value;
    } else {
      input = node("input");
      input.type = ["Armed", "PassAllPolylines"].includes(key) ? "checkbox" : "text";
      if (input.type === "checkbox") input.checked = truth(value); else input.value = value;
    }
    input.dataset.validationPath = path.join("/");
    if (key === "Name") { input.required = true; input.maxLength = 256; input.readOnly = !selected.creating; }
    input.addEventListener("input", () => { parent[key] = input.type === "checkbox" ? String(input.checked) : input.value; dirty(); });
    wrapper.append(input); return wrapper;
  }
  function renderEditor() {
    el("detector-selected-title").textContent = selected.creating ? "Create analytics rule" : draft.Name;
    const form = node("form", undefined, "detector-form");
    form.addEventListener("invalid", invalidPopup, true);
    if (!selected.creating) {
      const family = data.families.find((item) => item.id === draft.Type);
      form.append(toggle("Enabled", selected.enabled, {action: "toggle", configuration: configuration.token, name: draft.Name}),
        node("p", `Auto generate events: ${truth(family.settings.AutoGenerateEvents) ? "on" : "off"} · Event generation timeout: ${family.interval_seconds} s (shared by this detector type).`, "table-note"),
        button("Edit detector settings", () => { if (mayLeave()) showSettings(family, true); }),
        manualButton(draft.Type, selected.enabled && truth(family.enabled) && truth(data.rules_enabled), family.manual_available, {configuration: configuration.token, name: draft.Name}));
    }
    const typeLabel = node("label", "Rule type"); const select = node("select");
    for (const item of configuration.templates) { const option = node("option", label(item.Type)); option.value = item.Type; select.append(option); }
    select.value = draft.Type;
    select.addEventListener("change", () => {
      const name = draft.Name; draft = clone(configuration.templates.find((t) => t.Type === select.value)); draft.Name = name; dirty(); renderEditor();
    });
    typeLabel.append(select); form.append(typeLabel);
    for (const [key, value] of Object.entries(draft)) {
      if (key === "Type") continue;
      if (key === "ResetTime" && value === "") draft[key] = [];
      form.append(field(draft, key, draft[key], [key]));
    }
    if (draft.Type === "tt:LicensePlateRecognition") {
      for (const key of ["Region", "SnapLine"]) {
        if (!(key in draft)) form.append(button(`Add ${label(key)}`, () => {
          draft[key] = key === "Region" ? [{x: "-0.8", y: "-0.8"}, {x: "0.8", y: "-0.8"}, {x: "0.8", y: "0.8"}, {x: "-0.8", y: "0.8"}] : [{x: "0", y: "-1"}, {x: "0", y: "1"}]; dirty(); renderEditor();
        }));
        else form.append(button(`Remove ${label(key)}`, () => { delete draft[key]; dirty(); renderEditor(); }));
      }
    }
    const actions = node("div", undefined, "dialog-actions");
    const save = node("button", selected.creating ? "Create rule" : "Save changes", "button secondary"); save.type = "submit";
    actions.append(save, button("Cancel edits", () => { editing = false; selected = null; draft = null; load(true); }));
    if (!selected.creating) actions.append(button("Delete rule", () => {
      if (window.confirm(`Delete rule “${previous.Name}”?`)) change({action: "delete", configuration: configuration.token, name: previous.Name, previous});
    }, "danger"));
    form.append(actions);
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      change({action: selected.creating ? "create" : "modify", configuration: configuration.token, name: draft.Name, rule: draft, previous});
    });
    el("detector-settings").replaceChildren(form);
    draw();
  }
  function position(event) {
    const rect = canvas.getBoundingClientRect();
    return [(event.clientX - rect.left) / rect.width * canvas.width, (event.clientY - rect.top) / rect.height * canvas.height];
  }
  canvas.addEventListener("pointerdown", (event) => {
    if (!draft || saving) return;
    const [x, y] = position(event);
    dragged = handles.find((handle) => Math.hypot(handle.x - x, handle.y - y) < 20)?.point;
    if (dragged) { canvas.setPointerCapture(event.pointerId); event.preventDefault(); }
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!dragged) return;
    const [x, y] = position(event);
    const nx = Math.max(0, Math.min(1, x / canvas.width)), ny = Math.max(0, Math.min(1, y / canvas.height));
    const pixel = draft.Type === "tt:MotionRegionDetector", bounds = configuration.bounds;
    dragged.x = String(Number((pixel ? Number(bounds.x) + nx * Number(bounds.width) : nx * 2 - 1).toFixed(4)));
    dragged.y = String(Number((pixel ? Number(bounds.y) + ny * Number(bounds.height) : 1 - ny * 2).toFixed(4)));
    dirty();
  });
  function endDrag() { if (dragged) { dragged = null; renderEditor(); } }
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);
  async function load(force = false) {
    if (loading) { if (force) { await loadDone; return load(true); } return; }
    if (!force && (editing || saving || document.hidden)) return;
    loading = true;
    let finish;
    loadDone = new Promise((resolve) => { finish = resolve; });
    try {
      const response = await window.adminFetch("/admin/api/detectors", {cache: "no-store", signal: AbortSignal.timeout(5000)});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      data = await response.json(); catalog();
      if (selected?.simple) showSimple(data.simple.find((item) => item.id === selected.simple));
      if (selected?.family) showSettings(data.families.find((item) => item.id === selected.family), true);
      if (selected?.parent) showParent();
      if (selected?.configuration && !editing) {
        const cfg = data.configurations.find((item) => item.token === selected.configuration);
        const rule = cfg?.rules.find((item) => item.Name === selected.name);
        if (rule) openRule(cfg, rule, false); else { selected = null; draft = null; }
      }
      if (!selected) { el("detector-selected-title").textContent = "Select a detector or rule"; el("detector-settings").replaceChildren(node("p", "Select a built-in detector or an analytics rule. Create a rule to configure its geometry.")); draw(); }
      if (!editing) message("");
      return true;
    } catch (error) { message(`Detector settings unavailable: ${error.message}. Displayed values may be stale.`, true); return false; }
    finally { loading = false; finish(); }
  }
  el("detector-refresh").addEventListener("click", () => { if (mayLeave()) { editing = false; load(true); } });
  draw(); load();
  setInterval(() => load(), 5000);
})();
