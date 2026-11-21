"use strict";
(() => {
  const form = document.getElementById("login-form");
  const button = document.getElementById("login-submit");
  const error = document.getElementById("login-error");
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    button.disabled = true;
    button.textContent = "Signing in…";
    error.hidden = true;
    const password = document.getElementById("login-password");
    try {
      const response = await fetch("/admin/api/login", {
        method: "POST", credentials: "same-origin", cache: "no-store",
        headers: {"Content-Type": "application/json", "X-ONVIF-Admin": "login"},
        body: JSON.stringify({login: document.getElementById("login-username").value, password: password.value}),
        signal: AbortSignal.timeout(10000)
      });
      password.value = "";
      if (response.ok) { window.location.replace("/admin/"); return; }
      error.textContent = response.status === 401 ? "Invalid username or password, or this account cannot administer the emulator." :
        "Sign-in is unavailable. Please try again.";
    } catch {
      password.value = "";
      error.textContent = "Could not reach the emulator. Please try again.";
    } finally {
      button.disabled = false;
      button.textContent = "Sign in";
    }
    error.hidden = false;
    password.focus();
  });
})();
