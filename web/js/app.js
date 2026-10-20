import {
  clearToken,
  getStoredResumeVersionId,
  getToken,
  setStoredResumeVersionId,
} from "./api.js";
import { loadMe, logout, renderAuthView } from "./auth.js";
import { renderApplicationsView } from "./applications.js";
import { renderDetailView, renderJobsView } from "./jobs.js";
import { clear, el } from "./dom.js";

const header = document.getElementById("app-header");
const view = document.getElementById("view");
const toast = document.getElementById("toast");
let toastTimer = null;

function defaultFilters() {
  return {
    keyword: "",
    location: "",
    salary_min: "",
    salary_max: "",
    education_max: "0",
    job_nature: "0",
    open_only: false,
    sort: "created_desc",
  };
}

const state = {
  user: null,
  matchByJobId: new Map(),
  resumeVersionId: getStoredResumeVersionId(),
  views: {
    search: { filters: defaultFilters(), page: 1, total: 0 },
    recommended: { filters: defaultFilters(), page: 1, total: 0 },
  },
};

function setFlash(message) {
  toast.textContent = message || "";
  toast.classList.toggle("show", Boolean(message));
  if (toastTimer) {
    window.clearTimeout(toastTimer);
  }
  if (message) {
    toastTimer = window.setTimeout(() => {
      toast.classList.remove("show");
    }, 4200);
  }
}

function navigate(hash) {
  if (window.location.hash === hash) {
    route();
    return;
  }
  window.location.hash = hash;
}

function renderHeader() {
  clear(header);

  const brand = el("a", { class: "brand", href: "#/jobs" },
    "Job", el("span", {}, "Pilot"), "AI");

  const nav = el("nav", { class: "nav", "aria-label": "主导航" });
  const current = window.location.hash || "#/jobs";
  const items = state.user
    ? [
        ["#/jobs", "岗位"],
        ["#/recommended", "推荐"],
        ["#/applications", "申请状态板"],
      ]
    : [
        ["#/jobs", "岗位"],
        ["#/login", "登录"],
      ];
  for (const [href, label] of items) {
    const active = current === href
      || (href === "#/jobs" && current.startsWith("#/jobs/"));
    nav.appendChild(el("a", {
      href,
      class: active ? "active" : "",
    }, label));
  }

  header.append(brand, nav, el("div", { class: "header-spacer" }));

  if (state.user) {
    header.append(
      el("span", { class: "who" }, state.user.username || state.user.email || "已登录"),
      el("button", {
        type: "button",
        class: "ghost",
        onclick: () => {
          logout(ctx);
          state.resumeVersionId = null;
          setStoredResumeVersionId(null);
        },
      }, "退出"),
    );
  }
}

function route() {
  renderHeader();
  const hash = window.location.hash || "#/jobs";
  const loggedIn = Boolean(getToken() && state.user);
  const publicRoute = hash === "#/jobs" || hash.startsWith("#/jobs/");

  if (!loggedIn && !publicRoute && hash !== "#/login") {
    navigate("#/login");
    return;
  }

  if (loggedIn && hash === "#/login") {
    navigate("#/jobs");
    return;
  }

  if (hash === "#/login" || hash === "") {
    clear(view);
    renderAuthView(view, ctx);
    return;
  }

  if (hash === "#/jobs") {
    renderJobsView(view, ctx, "search");
    return;
  }

  if (hash === "#/recommended") {
    renderJobsView(view, ctx, "recommended");
    return;
  }

  if (hash.startsWith("#/jobs/")) {
    const id = hash.slice("#/jobs/".length);
    if (id) {
      renderDetailView(view, ctx, id);
      return;
    }
  }

  if (hash === "#/applications") {
    renderApplicationsView(view, ctx);
    return;
  }

  navigate("#/jobs");
}

const ctx = {
  state,
  navigate,
  renderHeader,
  setFlash,
};

async function boot() {
  window.addEventListener("hashchange", route);
  window.addEventListener("jobpilot:auth-expired", () => {
    state.user = null;
    state.matchByJobId.clear();
    state.resumeVersionId = null;
    setStoredResumeVersionId(null);
    renderHeader();
    setFlash("登录已过期，请重新登录");
    navigate("#/login");
  });

  if (getToken()) {
    try {
      state.user = await loadMe();
    } catch {
      clearToken();
      state.user = null;
    }
  }

  renderHeader();
  route();
}

boot();
