"use strict";
window.adminFetch = async (url, options = {}) => {
  const response = await fetch(url, {...options, credentials: "same-origin"});
  if (response.status === 401) {
    window.location.replace("/admin/login");
    throw new Error("Administrator sign-in required");
  }
  return response;
};
document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("sign-out").addEventListener("click", async (event) => {
    const button = event.currentTarget;
    button.disabled = true;
    try {
      const response = await window.adminFetch("/admin/api/logout", {
        method: "POST", headers: {"X-ONVIF-Admin": "logout"}
      });
      if (response.ok) window.location.replace("/admin/login");
      else throw new Error("Sign-out failed");
    } catch {
      button.disabled = false;
      document.getElementById("notice").hidden = false;
      document.getElementById("notice").textContent = "Could not sign out. Please try again.";
    }
  });
});
