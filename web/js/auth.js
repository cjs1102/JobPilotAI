import { apiFetch, clearToken, setToken } from "./api.js";
import { clear, el } from "./dom.js";

export async function loadMe() {
  return apiFetch("/users/me");
}

export async function login(email, password) {
  const data = await apiFetch("/users/login", {
    method: "POST",
    body: { email, password },
    auth: false,
  });
  setToken(data.token);
  return data;
}

export async function register(username, email, password) {
  await apiFetch("/users/register", {
    method: "POST",
    body: { username, email, password },
    auth: false,
  });
  return login(email, password);
}

export function logout(ctx) {
  clearToken();
  ctx.state.user = null;
  ctx.state.matchByJobId.clear();
  ctx.navigate("#/login");
  ctx.renderHeader();
  ctx.setFlash("已退出登录");
}

export function renderAuthView(root, ctx) {
  let mode = "login";
  let busy = false;

  const panel = el("section", { class: "panel auth-shell" });

  function render() {
    clear(panel);

    const loginTab = el("button", {
      type: "button",
      class: mode === "login" ? "active" : "",
      "aria-selected": mode === "login" ? "true" : "false",
      onclick: () => {
        if (!busy) {
          mode = "login";
          render();
        }
      },
    }, "登录");
    const registerTab = el("button", {
      type: "button",
      class: mode === "register" ? "active" : "",
      "aria-selected": mode === "register" ? "true" : "false",
      onclick: () => {
        if (!busy) {
          mode = "register";
          render();
        }
      },
    }, "注册");

    const error = el("div", { class: "form-error", role: "alert", hidden: true });
    const submit = el("button", { type: "submit" }, mode === "login" ? "登录" : "注册并登录");

    const usernameField = el("div", { class: "field" },
      el("label", { for: "auth-username" }, "用户名"),
      el("input", {
        id: "auth-username",
        name: "username",
        autocomplete: "username",
        required: true,
        minlength: "2",
        maxlength: "64",
      }),
    );

    const emailField = el("div", { class: "field" },
      el("label", { for: "auth-email" }, "邮箱"),
      el("input", {
        id: "auth-email",
        name: "email",
        type: "email",
        autocomplete: "email",
        required: true,
      }),
    );

    const passwordField = el("div", { class: "field" },
      el("label", { for: "auth-password" }, "密码"),
      el("input", {
        id: "auth-password",
        name: "password",
        type: "password",
        autocomplete: mode === "login" ? "current-password" : "new-password",
        required: true,
        minlength: "6",
      }),
    );

    const form = el("form", { novalidate: true },
      mode === "register" ? usernameField : null,
      emailField,
      passwordField,
      error,
      el("div", { class: "row end" }, submit),
    );

    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      if (busy) {
        return;
      }

      const formData = new FormData(form);
      const email = String(formData.get("email") || "").trim();
      const password = String(formData.get("password") || "");
      const username = String(formData.get("username") || "").trim();

      error.hidden = true;
      if (mode === "register" && username.length < 2) {
        error.textContent = "用户名至少 2 个字符";
        error.hidden = false;
        return;
      }
      if (!email || !password) {
        error.textContent = "请填写邮箱和密码";
        error.hidden = false;
        return;
      }

      busy = true;
      submit.disabled = true;
      try {
        if (mode === "login") {
          await login(email, password);
        } else {
          await register(username, email, password);
        }
        ctx.state.user = await loadMe();
        ctx.renderHeader();
        ctx.navigate("#/jobs");
        ctx.setFlash(mode === "login" ? "登录成功" : "注册成功，已登录");
      } catch (err) {
        error.textContent = err && err.message ? err.message : "请求失败";
        error.hidden = false;
      } finally {
        busy = false;
        submit.disabled = false;
      }
    });

    panel.append(
      el("h1", {}, "进入 JobPilotAI"),
      el("p", { class: "muted" }, "本地 demo：用一个账号管理岗位、匹配解释与投递状态。"),
      el("div", { class: "tabs", role: "tablist" }, loginTab, registerTab),
      form,
      el("p", { class: "muted small" },
        "登录状态保存在本机浏览器；服务端会话有效期 24 小时，没有 refresh token。"),
    );

    root.appendChild(panel);
  }

  render();
}
