const TOKEN_KEY = "jobpilot.token";
const RESUME_VERSION_KEY = "jobpilot.resumeVersionId";

export class ApiError extends Error {
  constructor(code, message, httpStatus) {
    super(message || "请求失败");
    this.name = "ApiError";
    this.code = code;
    this.httpStatus = httpStatus;
  }
}

export function getToken() {
  try {
    return window.localStorage.getItem(TOKEN_KEY);
  } catch {
    return null;
  }
}

export function setToken(token) {
  window.localStorage.setItem(TOKEN_KEY, token);
}

export function clearToken() {
  window.localStorage.removeItem(TOKEN_KEY);
}

export function getStoredResumeVersionId() {
  try {
    const raw = window.localStorage.getItem(RESUME_VERSION_KEY);
    return raw === null ? null : Number(raw);
  } catch {
    return null;
  }
}

export function setStoredResumeVersionId(value) {
  if (value === null || value === undefined) {
    window.localStorage.removeItem(RESUME_VERSION_KEY);
    return;
  }
  window.localStorage.setItem(RESUME_VERSION_KEY, String(value));
}

function isExpired(code) {
  return code === 20001 || code === 20002;
}

function notifyExpired() {
  clearToken();
  window.dispatchEvent(new CustomEvent("jobpilot:auth-expired"));
}

export async function apiFetch(path, options = {}) {
  const {
    method = "GET",
    body,
    auth = true,
    multipart = false,
  } = options;

  const headers = {};
  const token = getToken();

  if (auth && token) {
    headers.Authorization = `Bearer ${token}`;
  }

  let payload;
  if (body !== undefined) {
    if (multipart) {
      payload = body;
    } else {
      headers["Content-Type"] = "application/json";
      payload = JSON.stringify(body);
    }
  }

  let response;
  try {
    response = await fetch(`/api/v1${path}`, {
      method,
      headers,
      body: payload,
      credentials: "same-origin",
    });
  } catch {
    throw new ApiError(-1, "网络请求失败，请检查后端是否运行", 0);
  }

  let envelope;
  try {
    envelope = await response.json();
  } catch {
    throw new ApiError(-1, `服务器返回了非 JSON 响应（HTTP ${response.status}）`, response.status);
  }

  const code = typeof envelope.code === "number" ? envelope.code : -1;

  if (response.status === 401 || isExpired(code)) {
    notifyExpired();
    throw new ApiError(code, envelope.message || "登录已过期，请重新登录", response.status);
  }

  if (!response.ok || code !== 0) {
    throw new ApiError(code, envelope.message || `请求失败（HTTP ${response.status}）`, response.status);
  }

  return envelope.data;
}
