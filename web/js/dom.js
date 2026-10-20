export function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);

  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === null || value === undefined || value === false) {
      continue;
    }
    if (key === "class") {
      node.className = String(value);
    } else if (key === "text") {
      node.textContent = String(value);
    } else if (key === "dataset") {
      for (const [dataKey, dataValue] of Object.entries(value)) {
        node.dataset[dataKey] = String(dataValue);
      }
    } else if (key.startsWith("on") && typeof value === "function") {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (value === true) {
      node.setAttribute(key, "");
    } else {
      node.setAttribute(key, String(value));
    }
  }

  appendChildren(node, children);
  return node;
}

function appendChildren(node, children) {
  for (const child of children) {
    if (child === null || child === undefined || child === false) {
      continue;
    }
    if (Array.isArray(child)) {
      appendChildren(node, child);
    } else if (child instanceof Node) {
      node.appendChild(child);
    } else {
      node.appendChild(document.createTextNode(String(child)));
    }
  }
}

export function clear(node) {
  while (node.firstChild) {
    node.removeChild(node.firstChild);
  }
}

export function text(value) {
  return value === null || value === undefined ? "" : String(value);
}

export function safeHttpUrl(value) {
  if (typeof value !== "string" || value.trim() === "") {
    return null;
  }
  const raw = value.trim();
  if (!/^https?:\/\//i.test(raw)) {
    return null;
  }
  try {
    const parsed = new URL(raw);
    if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
      return null;
    }
    return parsed.href;
  } catch {
    return null;
  }
}

export function externalLink(label, value, className = "button secondary") {
  const url = safeHttpUrl(value);
  if (url === null) {
    return el("span", { class: "muted small" }, "未提供链接");
  }
  return el("a", {
    class: className,
    href: url,
    target: "_blank",
    rel: "noopener noreferrer",
  }, label);
}

export function statusLabel(status) {
  switch (Number(status)) {
    case 0: return "收藏";
    case 1: return "待投递";
    case 2: return "已投递";
    case 3: return "笔试";
    case 4: return "一面";
    case 5: return "二面";
    case 6: return "Offer";
    case 7: return "拒绝";
    default: return "未知状态";
  }
}

export function statusTone(status) {
  const value = Number(status);
  if (value === 7) return "badge danger";
  if (value === 6) return "badge score";
  if (value === 0) return "badge muted";
  return "badge";
}

export function educationLabel(value) {
  switch (Number(value)) {
    case 1: return "学历不限";
    case 2: return "大专";
    case 3: return "本科";
    case 4: return "硕士";
    case 5: return "博士";
    default: return "";
  }
}

export function jobNatureLabel(value) {
  switch (Number(value)) {
    case 1: return "校招";
    case 2: return "社招";
    case 3: return "实习";
    default: return "";
  }
}

export function freshnessLabel(value) {
  switch (value) {
    case "fresh": return "新鲜";
    case "aging": return "渐旧";
    case "stale": return "过期";
    case "closed": return "已关闭";
    default: return "";
  }
}

export function urgencyLabel(value) {
  switch (value) {
    case "expired": return "已截止";
    case "urgent": return "7 天内截止";
    case "soon": return "30 天内截止";
    case "normal": return "截止正常";
    default: return "";
  }
}

export function formatSalary(job) {
  const min = job.salary_min;
  const max = job.salary_max;
  const currency = job.salary_currency || "CNY";
  if (min === null || min === undefined) {
    return max === null || max === undefined ? "薪资未提供" : `${max} 以下`;
  }
  if (max === null || max === undefined) {
    return `${min} 以上`;
  }
  return `${min}–${max} ${currency}`;
}

export function formatDeadline(job) {
  if (!job.deadline) {
    return "";
  }
  return job.deadline_date_only ? `截止 ${job.deadline}` : `截止 ${job.deadline}`;
}

export function formatDate(value) {
  return value ? String(value) : "";
}

export function chip(label, tone = "badge") {
  return el("span", { class: tone }, label);
}

export function reasonItem(reason) {
  const label = reason && reason.text ? reason.text : "匹配说明";
  return el("li", {}, label);
}
