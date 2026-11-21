"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  let files = [], busy = false, dirty = false;
  const entry = (name) => files.find((file) => file.name === name);
  const documentFor = (name) => JSON.parse(entry(name).document);
  function node(tag, text) {
    const element = document.createElement(tag); element.textContent = text; return element;
  }
  function message(text, error = false) {
    $("users-message").textContent = text;
    $("users-message").className = `notice ${error ? "error" : "success"}`;
    $("users-message").hidden = !text;
  }
  function button(text, action) {
    const element = node("button", text); element.type = "button"; element.className = "button secondary";
    element.addEventListener("click", action); return element;
  }
  function render() {
    $("authentication-scheme").value = documentFor("common.config").authentication;
    const accounts = documentFor("users.config");
    $("users-body").replaceChildren();
    for (const [index, user] of accounts.users.entries()) {
      const row = document.createElement("tr"), actions = document.createElement("td");
      const enabled = user.enabled === true;
      const lastAdmin = enabled && user.type === "administrator" && accounts.users.filter((item) => item.enabled && item.type === "administrator").length === 1;
      const toggle = button(enabled ? "Disable" : "Enable", () => {
        const next = structuredClone(accounts); next.users[index].enabled = !enabled; save("users.config", next);
      });
      const remove = button("Delete", () => {
        if (!window.confirm(`Delete account “${user.login}”?`)) return;
        const next = structuredClone(accounts); next.users.splice(index, 1); save("users.config", next);
      });
      toggle.disabled = remove.disabled = lastAdmin;
      if (lastAdmin) toggle.title = remove.title = "Keep at least one enabled administrator account";
      actions.append(toggle, remove);
      row.append(node("td", user.login), node("td", user.type), node("td", enabled ? "Enabled" : "Disabled"), actions);
      $("users-body").append(row);
    }
  }
  async function load() {
    const response = await window.adminFetch("/admin/api/configuration", {cache: "no-store", signal: AbortSignal.timeout(10000)});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const result = await response.json(); files = result.files; render(); $("users-workspace").inert = false;
    if (result.restart_required) message("Saved configuration requires an application restart. Accounts and authentication still use the values loaded at startup.");
  }
  async function save(name, document) {
    if (busy) return;
    busy = true; $("users-workspace").inert = true; $("users-refresh").disabled = true;
    try {
      const response = await window.adminFetch("/admin/api/configuration", {
        method: "POST", headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "configuration"},
        body: JSON.stringify({file: name, revision: entry(name).revision, document: JSON.stringify(document)}),
        signal: AbortSignal.timeout(20000)
      });
      const result = await response.json();
      if (!response.ok) {
        message([result.message || result.error, ...(result.validation_errors || []).map((issue) => `${issue.path}: ${issue.message}`)].join(" "), true);
        return;
      }
      dirty = false;
      if (name === "users.config") $("user-create").reset();
      await load();
      message("Saved. Restart the application in System to apply account and authentication changes. Restart signs out all browser sessions.");
      window.dispatchEvent(new Event("admin-configuration-saved"));
    } catch (error) { message(`Save could not be confirmed: ${error.message}. Reload to inspect saved values before retrying.`, true); }
    finally { busy = false; $("users-workspace").inert = false; $("users-refresh").disabled = false; }
  }
  $("authentication-form").addEventListener("submit", (event) => {
    event.preventDefault(); const next = documentFor("common.config"); next.authentication = $("authentication-scheme").value; save("common.config", next);
  });
  $("user-create").addEventListener("submit", (event) => {
    event.preventDefault(); const next = documentFor("users.config"), login = $("user-login").value;
    if (next.users.some((user) => user.login === login)) { message("This login already exists.", true); return; }
    next.users.push({login, password: $("user-password").value, type: $("user-role").value, enabled: $("user-enabled").checked});
    save("users.config", next);
  });
  $("users-workspace").addEventListener("input", () => { dirty = true; });
  $("users-refresh").addEventListener("click", async () => {
    if (busy || (dirty && !window.confirm("Discard unsaved user and authentication changes?"))) return;
    try { await load(); dirty = false; $("user-create").reset(); } catch (error) { message(`Users unavailable: ${error.message}`, true); }
  });
  window.addEventListener("beforeunload", (event) => { if (dirty) { event.preventDefault(); event.returnValue = ""; } });
  $("users-workspace").inert = true;
  load().catch((error) => { message(`Users unavailable: ${error.message}`, true); $("users-workspace").inert = true; });
})();
