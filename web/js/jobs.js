import { apiFetch } from "./api.js";
import { favouriteJob } from "./applications.js";
import {
  chip,
  clear,
  educationLabel,
  el,
  externalLink,
  formatDeadline,
  formatSalary,
  freshnessLabel,
  jobNatureLabel,
  reasonItem,
  urgencyLabel,
} from "./dom.js";

const PAGE_SIZE = 20;

function numberOrNull(value) {
  const text = String(value || "").trim();
  if (text === "") {
    return null;
  }
  const number = Number(text);
  return Number.isFinite(number) ? number : null;
}

function queryFor(mode, filters, page) {
  const params = new URLSearchParams();
  params.set("page", String(page));
  params.set("page_size", String(PAGE_SIZE));

  for (const key of ["keyword", "location"]) {
    const value = String(filters[key] || "").trim();
    if (value) {
      params.set(key, value);
    }
  }

  for (const key of ["salary_min", "salary_max"]) {
    const value = numberOrNull(filters[key]);
    if (value !== null && value >= 0) {
      params.set(key, String(value));
    }
  }

  for (const key of ["education_max", "job_nature"]) {
    const value = numberOrNull(filters[key]);
    if (value !== null && value > 0) {
      params.set(key, String(value));
    }
  }

  if (filters.open_only) {
    params.set("open_only", "true");
  }

  if (mode === "search" && filters.sort) {
    params.set("sort", filters.sort);
  }

  return params;
}

function selectField(id, label, options, value) {
  const select = el("select", { id, name: id });
  for (const option of options) {
    const node = el("option", { value: String(option.value) }, option.label);
    if (String(option.value) === String(value)) {
      node.setAttribute("selected", "");
    }
    select.appendChild(node);
  }
  return el("div", { class: "field" }, el("label", { for: id }, label), select);
}

function textField(id, label, value, attrs = {}) {
  return el("div", { class: "field" },
    el("label", { for: id }, label),
    el("input", { id, name: id, value: value || "", ...attrs }),
  );
}

function matchesBlock(job) {
  const hasScore = typeof job.score === "number";
  const reasons = Array.isArray(job.reasons) ? job.reasons : [];
  const breakers = Array.isArray(job.deal_breakers) ? job.deal_breakers : [];

  if (!hasScore && reasons.length === 0 && breakers.length === 0) {
    return null;
  }

  const block = el("div", { class: "match" });
  const badges = el("div", { class: "badges" });
  if (hasScore) {
    const numericScore = Number(job.score);
    const scoreText = Number.isInteger(numericScore)
      ? String(numericScore)
      : numericScore.toFixed(1);
    badges.appendChild(chip(`匹配 ${scoreText}`, "badge score"));
    if (job.score_version) {
      badges.appendChild(chip(job.score_version, "badge muted"));
    }
  }
  if (job.deadline_urgency && urgencyLabel(job.deadline_urgency)) {
    badges.appendChild(chip(urgencyLabel(job.deadline_urgency), "badge warn"));
  }
  if (badges.childNodes.length > 0) {
    block.appendChild(badges);
  }

  if (reasons.length > 0) {
    const list = el("ul", { class: "match-reasons" });
    for (const reason of reasons) {
      list.appendChild(reasonItem(reason));
    }
    block.appendChild(list);
  }

  if (breakers.length > 0) {
    const row = el("div", { class: "deal-breakers" });
    row.appendChild(el("span", { class: "muted small" }, "硬性冲突："));
    for (const breaker of breakers) {
      row.appendChild(chip(breaker.text || breaker.code || "冲突", "badge danger"));
    }
    block.appendChild(row);
  }

  return block;
}

function jobMeta(job) {
  const parts = [
    formatSalary(job),
    job.location || "地点未提供",
    freshnessLabel(job.freshness),
    formatDeadline(job),
  ].filter(Boolean);
  return el("div", { class: "job-meta" }, parts.join(" · "));
}

function jobBadges(job) {
  const badges = el("div", { class: "badges" });
  if (job.source) {
    badges.appendChild(chip(job.source, "badge muted"));
  }
  if (job.education_required) {
    badges.appendChild(chip(educationLabel(job.education_required), "badge"));
  }
  if (job.job_nature) {
    badges.appendChild(chip(jobNatureLabel(job.job_nature), "badge"));
  }
  if (job.deadline_urgency && urgencyLabel(job.deadline_urgency)) {
    badges.appendChild(chip(urgencyLabel(job.deadline_urgency), "badge warn"));
  }
  return badges;
}

function rememberMatch(ctx, job) {
  if (job && typeof job.id !== "undefined") {
    ctx.state.matchByJobId.set(Number(job.id), job);
  }
}

function jobRow(ctx, job) {
  const title = el("a", {
    class: "job-title",
    href: `#/jobs/${job.id}`,
    onclick: () => rememberMatch(ctx, job),
  }, job.title || `岗位 #${job.id}`);

  const actions = el("div", { class: "row" },
    el("a", {
      class: "button secondary",
      href: `#/jobs/${job.id}`,
      onclick: () => rememberMatch(ctx, job),
    }, "查看详情"),
  );

  if (ctx.state.user) {
    actions.appendChild(el("button", {
      type: "button",
      onclick: () => favouriteJob(ctx, job),
    }, "收藏"));
  } else {
    actions.appendChild(el("a", { class: "button ghost", href: "#/login" }, "登录后收藏"));
  }

  return el("article", { class: "job-card" },
    el("div", { class: "job-head" },
      el("div", {},
        el("h2", { class: "job-title" }, title),
        el("div", { class: "job-company" }, job.company || "公司未提供"),
        jobMeta(job),
        jobBadges(job),
      ),
      actions,
    ),
    matchesBlock(job),
  );
}

function renderPager(container, state, load) {
  clear(container);
  const totalPages = Math.max(1, Math.ceil((state.total || 0) / PAGE_SIZE));
  if (totalPages <= 1) {
    return;
  }
  const previous = el("button", {
    type: "button",
    class: "secondary",
    disabled: state.page <= 1,
    onclick: () => load(state.page - 1),
  }, "上一页");
  const next = el("button", {
    type: "button",
    class: "secondary",
    disabled: state.page >= totalPages,
    onclick: () => load(state.page + 1),
  }, "下一页");
  container.append(
    previous,
    el("span", { class: "muted small" }, `第 ${state.page} / ${totalPages} 页 · 共 ${state.total} 条`),
    next,
  );
}

export function renderJobsView(root, ctx, mode = "search") {
  clear(root);
  const state = ctx.state.views[mode];
  const title = mode === "recommended" ? "为你推荐" : "岗位列表";
  const subtitle = mode === "recommended"
    ? "基于 rule_v1 的确定性匹配；需要登录。"
    : "公开岗位搜索；登录后显示可解释匹配。";

  const filters = state.filters;
  const form = el("form", { class: "filters", novalidate: true });

  form.append(
    textField("keyword", "关键词", filters.keyword, { placeholder: "C++ / 后端" }),
    textField("location", "地点", filters.location, { placeholder: "深圳" }),
    textField("salary_min", "薪资下限（千）", filters.salary_min, { type: "number", min: "0" }),
    textField("salary_max", "薪资上限（千）", filters.salary_max, { type: "number", min: "0" }),
    selectField("education_max", "最高学历要求", [
      { value: 0, label: "不限" },
      { value: 1, label: "不限学历" },
      { value: 2, label: "大专" },
      { value: 3, label: "本科" },
      { value: 4, label: "硕士" },
      { value: 5, label: "博士" },
    ], filters.education_max),
    selectField("job_nature", "岗位性质", [
      { value: 0, label: "不限" },
      { value: 1, label: "校招" },
      { value: 2, label: "社招" },
      { value: 3, label: "实习" },
    ], filters.job_nature),
  );

  if (mode === "search") {
    form.appendChild(selectField("sort", "排序", [
      { value: "created_desc", label: "最新优先" },
      { value: "deadline_asc", label: "截止时间" },
      { value: "education_asc", label: "学历要求" },
    ], filters.sort || "created_desc"));
  }

  const openOnly = el("input", {
    type: "checkbox",
    id: "open_only",
    name: "open_only",
  });
  if (filters.open_only) {
    openOnly.setAttribute("checked", "");
  }

  form.appendChild(el("div", { class: "field" },
    el("label", { for: "open_only" }, "只看未截止"),
    el("label", { class: "small" }, openOnly, " 隐藏已过截止时间"),
  ));

  const submit = el("button", { type: "submit" }, "搜索");
  const reset = el("button", {
    type: "button",
    class: "secondary",
    onclick: () => {
      state.filters = {
        keyword: "",
        location: "",
        salary_min: "",
        salary_max: "",
        education_max: "0",
        job_nature: "0",
        open_only: false,
        sort: "created_desc",
      };
      state.page = 1;
      renderJobsView(root, ctx, mode);
    },
  }, "重置");
  form.appendChild(el("div", { class: "filter-actions" }, submit, reset));

  const results = el("div", { class: "job-list" });
  const pager = el("div", { class: "pager" });

  function readFilters() {
    const data = new FormData(form);
    return {
      keyword: String(data.get("keyword") || "").trim(),
      location: String(data.get("location") || "").trim(),
      salary_min: String(data.get("salary_min") || "").trim(),
      salary_max: String(data.get("salary_max") || "").trim(),
      education_max: String(data.get("education_max") || "0"),
      job_nature: String(data.get("job_nature") || "0"),
      open_only: data.get("open_only") === "on",
      sort: String(data.get("sort") || "created_desc"),
    };
  }

  async function load(page) {
    state.page = page;
    clear(results);
    results.appendChild(el("div", { class: "loading" }, "加载岗位…"));
    clear(pager);

    try {
      const params = queryFor(mode, state.filters, state.page);
      if (mode === "search" && ctx.state.user) {
        params.set("with_match", "true");
      }
      const path = mode === "recommended"
        ? "/jobs/recommended"
        : "/jobs";
      const data = await apiFetch(`${path}?${params.toString()}`);
      const rows = Array.isArray(data && data.list) ? data.list : [];
      state.total = Number(data && data.total) || 0;
      clear(results);
      if (rows.length === 0) {
        results.appendChild(el("div", { class: "empty" },
          ctx.state.user ? "没有找到匹配的岗位。" : "没有找到岗位；登录后可按匹配解释筛选。"));
      } else {
        for (const job of rows) {
          rememberMatch(ctx, job);
          results.appendChild(jobRow(ctx, job));
        }
      }
      renderPager(pager, state, load);
    } catch (err) {
      clear(results);
      results.appendChild(el("div", { class: "form-error", role: "alert" },
        err.message || "加载岗位失败"));
    }
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    state.filters = readFilters();
    state.page = 1;
    load(1);
  });

  root.append(
    el("div", { class: "row" },
      el("h1", {}, title),
      el("span", { class: "badge muted" }, mode === "recommended" ? "需要登录" : "公开"),
    ),
    el("p", { class: "muted" }, subtitle),
    form,
    results,
    pager,
  );

  load(state.page || 1);
}

function detailMatch(ctx, jobId) {
  const job = ctx.state.matchByJobId.get(Number(jobId));
  if (!job) {
    return el("p", { class: "muted small" },
      "匹配解释来自列表或推荐视图；直接打开详情时没有可复用的 match 对象。返回列表即可查看。");
  }
  return matchesBlock(job) || el("p", { class: "muted small" }, "该岗位没有匹配解释。");
}

export async function renderDetailView(root, ctx, jobId) {
  clear(root);
  root.appendChild(el("div", { class: "loading" }, "加载岗位详情…"));

  let job;
  try {
    job = await apiFetch(`/jobs/${encodeURIComponent(jobId)}`);
  } catch (err) {
    clear(root);
    root.append(
      el("div", { class: "form-error", role: "alert" }, err.message || "加载岗位失败"),
      el("a", { href: "#/jobs", class: "button secondary" }, "返回列表"),
    );
    return;
  }

  clear(root);
  const actions = el("div", { class: "row" },
    el("a", { href: "#/jobs", class: "button secondary" }, "返回列表"),
  );
  if (ctx.state.user) {
    actions.appendChild(el("button", {
      type: "button",
      onclick: () => favouriteJob(ctx, job),
    }, "收藏"));
  } else {
    actions.appendChild(el("a", { href: "#/login", class: "button secondary" }, "登录后收藏"));
  }

  const requirements = Array.isArray(job.requirements) ? job.requirements : [];
  const main = el("article", { class: "panel" },
    el("div", { class: "row" },
      el("h1", {}, job.title || `岗位 #${job.id}`),
      job.freshness ? chip(freshnessLabel(job.freshness) || job.freshness, "badge muted") : null,
    ),
    el("p", { class: "job-company" }, job.company || "公司未提供"),
    jobMeta(job),
    jobBadges(job),
    el("h2", { class: "job-section" }, "岗位描述"),
    el("p", { class: "detail-description" }, job.description || "暂无描述"),
    el("h2", { class: "job-section" }, "技能要求"),
    requirements.length > 0
      ? el("ul", { class: "req-list" },
        ...requirements.map((item) => el("li", {},
          `${item.skill}${item.level ? ` · L${item.level}` : ""}`)))
      : el("p", { class: "muted" }, "来源没有提供技能要求。"),
  );

  const side = el("aside", { class: "panel side-card" },
    el("h2", {}, "匹配解释"),
    detailMatch(ctx, job.id),
    el("h2", { class: "job-section" }, "打开原始页面"),
    el("div", { class: "stack" },
      externalLink("打开申请链接", job.apply_url),
      externalLink("打开原始公告", job.source_url),
      el("p", { class: "muted small" },
        job.apply_url
          ? "申请链接由来源显式提供；不会自动提交。"
          : "来源没有显式申请链接；原始公告仍可打开，申请需在浏览器中手动完成。"),
    ),
    el("h2", { class: "job-section" }, "操作"),
    actions,
  );

  root.append(
    el("div", { class: "detail-grid" }, main, side),
  );
}
