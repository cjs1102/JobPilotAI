import {
  apiFetch,
  getStoredResumeVersionId,
  setStoredResumeVersionId,
} from "./api.js";
import {
  clear,
  el,
  formatDate,
  statusLabel,
  statusTone,
} from "./dom.js";

async function latestVersionId(resumeId) {
  const data = await apiFetch(`/resumes/${resumeId}/versions`);
  const versions = Array.isArray(data && data.list) ? data.list : [];
  if (versions.length === 0) {
    return null;
  }
  // The API returns versions in ascending version order; the last entry is the
  // newest one.
  return Number(versions[versions.length - 1].id);
}

export async function ensureResumeVersion(ctx) {
  if (ctx.state.resumeVersionId) {
    return ctx.state.resumeVersionId;
  }

  const stored = getStoredResumeVersionId();
  if (stored !== null && Number.isFinite(stored)) {
    ctx.state.resumeVersionId = stored;
    return stored;
  }

  const data = await apiFetch("/resumes?page=1&page_size=20");
  const resumes = Array.isArray(data && data.list) ? data.list : [];
  for (const resume of resumes) {
    const versionId = await latestVersionId(resume.id);
    if (versionId !== null) {
      ctx.state.resumeVersionId = versionId;
      setStoredResumeVersionId(versionId);
      return versionId;
    }
  }
  return null;
}

async function uploadResume(ctx, file, title) {
  const form = new FormData();
  form.append("title", title);
  form.append("file", file);
  const uploaded = await apiFetch("/resumes/upload", {
    method: "POST",
    body: form,
    multipart: true,
  });
  const versionId = await latestVersionId(uploaded.resume_id);
  if (versionId === null) {
    throw new Error("简历已上传，但没有可用的版本");
  }
  ctx.state.resumeVersionId = versionId;
  setStoredResumeVersionId(versionId);
  return { resumeId: uploaded.resume_id, versionId };
}

export async function favouriteJob(ctx, job) {
  if (!ctx.state.user) {
    ctx.navigate("#/login");
    return;
  }

  let versionId;
  try {
    versionId = await ensureResumeVersion(ctx);
  } catch (err) {
    ctx.setFlash(err.message || "读取简历失败");
    return;
  }

  if (!versionId) {
    ctx.setFlash("请先在“申请状态板”上传一份简历，再收藏岗位");
    ctx.navigate("#/applications");
    return;
  }

  try {
    await apiFetch("/applications", {
      method: "POST",
      body: {
        job_id: job.id,
        resume_version_id: versionId,
      },
    });
    ctx.setFlash("已收藏，可在“申请状态板”推进状态");
  } catch (err) {
    if (err && err.code === 40004) {
      ctx.setFlash("这个岗位已在投递列表中");
    } else {
      ctx.setFlash(err.message || "收藏失败");
    }
  }
}

function renderResumePanel(ctx, reload) {
  const panel = el("section", { class: "panel resume-panel" });
  const status = el("p", { class: "status-line" });
  const error = el("div", { class: "form-error", role: "alert", hidden: true });
  const ok = el("div", { class: "form-ok", hidden: true });
  const fileInput = el("input", {
    type: "file",
    id: "resume-file",
    accept: ".pdf,.txt,.md,text/plain,application/pdf",
    required: true,
  });
  const titleInput = el("input", {
    type: "text",
    id: "resume-title",
    maxlength: "128",
    placeholder: "如：C++ 后端版",
    required: true,
  });
  const submit = el("button", { type: "submit" }, "上传并设为当前版本");

  const form = el("form", { novalidate: true },
    el("div", { class: "filters" },
      el("div", { class: "field" },
        el("label", { for: "resume-title" }, "简历名称"),
        titleInput,
      ),
      el("div", { class: "field" },
        el("label", { for: "resume-file" }, "文件（PDF/TXT/MD）"),
        fileInput,
      ),
      el("div", { class: "filter-actions" }, submit),
    ),
    error,
    ok,
  );

  function refreshStatus() {
    status.textContent = ctx.state.resumeVersionId
      ? `当前简历版本 id：${ctx.state.resumeVersionId}`
      : "尚未上传简历；收藏岗位前需要先上传一份简历。";
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    error.hidden = true;
    ok.hidden = true;

    const file = fileInput.files && fileInput.files[0];
    const title = titleInput.value.trim();
    if (!file) {
      error.textContent = "请选择 PDF/TXT/MD 文件";
      error.hidden = false;
      return;
    }
    if (!title) {
      error.textContent = "请填写简历名称";
      error.hidden = false;
      return;
    }

    submit.disabled = true;
    try {
      const result = await uploadResume(ctx, file, title);
      refreshStatus();
      ok.textContent = `已上传：resume_id=${result.resumeId}，version_id=${result.versionId}`;
      ok.hidden = false;
      form.reset();
      ctx.setFlash("简历已上传，现在可以收藏岗位");
      if (reload) {
        reload();
      }
    } catch (err) {
      error.textContent = err.message || "上传失败";
      error.hidden = false;
    } finally {
      submit.disabled = false;
    }
  });

  refreshStatus();
  panel.append(
    el("div", { class: "row" },
      el("h2", {}, "简历设置"),
      el("span", { class: "badge muted" }, "收藏的前置条件"),
    ),
    el("p", { class: "muted small" },
      "后端要求每次收藏都带 resume_version_id。上传后本页会自动取最新版本；没有简历版本时收藏会被阻止。"),
    form,
    status,
  );
  return panel;
}

function renderApplicationRow(ctx, application, reload) {
  const status = Number(application.status);
  const actions = el("div", { class: "actions" });

  if (status >= 0 && status <= 5) {
    actions.appendChild(el("button", {
      type: "button",
      class: "secondary",
      onclick: async (event) => {
        const button = event.currentTarget;
        button.disabled = true;
        try {
          await apiFetch(`/applications/${application.id}/status`, {
            method: "PUT",
            body: { status: status + 1 },
          });
          ctx.setFlash(`已推进到「${statusLabel(status + 1)}」`);
          reload();
        } catch (err) {
          ctx.setFlash(err.message || "状态更新失败");
          button.disabled = false;
        }
      },
    }, `推进到 ${statusLabel(status + 1)}`));
  }

  if (status >= 0 && status <= 6) {
    actions.appendChild(el("button", {
      type: "button",
      class: "ghost danger",
      onclick: async (event) => {
        const button = event.currentTarget;
        button.disabled = true;
        try {
          await apiFetch(`/applications/${application.id}/status`, {
            method: "PUT",
            body: { status: 7 },
          });
          ctx.setFlash("已标记为拒绝");
          reload();
        } catch (err) {
          ctx.setFlash(err.message || "状态更新失败");
          button.disabled = false;
        }
      },
    }, "拒绝"));
  }

  return el("div", { class: "application-row" },
    el("div", {},
      el("a", { href: `#/jobs/${application.job_id}`, class: "job-title" },
        application.job_title || `岗位 #${application.job_id}`),
      el("div", { class: "muted small" },
        [application.company || "公司未提供",
         formatDate(application.apply_time) ? `更新于 ${application.apply_time}` : ""]
          .filter(Boolean).join(" · ")),
    ),
    el("div", {},
      el("span", { class: statusTone(status) }, statusLabel(status)),
      application.remark ? el("div", { class: "muted small" }, application.remark) : null,
    ),
    actions,
  );
}

async function renderApplicationList(ctx) {
  const list = el("div", { class: "application-list" });
  list.appendChild(el("div", { class: "loading" }, "加载投递列表…"));

  try {
    const data = await apiFetch("/applications?page=1&page_size=50");
    const rows = Array.isArray(data && data.list) ? data.list : [];
    clear(list);
    if (rows.length === 0) {
      list.appendChild(el("div", { class: "empty" },
        "还没有投递记录。先去岗位列表收藏一个岗位。"));
      return list;
    }
    for (const application of rows) {
      list.appendChild(renderApplicationRow(ctx, application, () => {
        clear(list);
        list.appendChild(el("div", { class: "loading" }, "刷新中…"));
        renderApplicationList(ctx).then((next) => {
          list.replaceWith(next);
        });
      }));
    }
  } catch (err) {
    clear(list);
    list.appendChild(el("div", { class: "form-error", role: "alert" },
      err.message || "加载投递列表失败"));
  }
  return list;
}

export function renderApplicationsView(root, ctx) {
  clear(root);
  const header = el("div", { class: "row" },
    el("h1", {}, "申请状态板"),
    el("span", { class: "badge muted" }, "本地 demo"),
  );
  const intro = el("p", { class: "muted" },
    "收藏、推进和拒绝只记录状态；真正的申请仍在浏览器中手动完成。");

  const resumePanel = renderResumePanel(ctx, () => {
    // Re-render the list only; the resume status updates in place.
  });
  const listPanel = el("section", { class: "panel" },
    el("div", { class: "row" },
      el("h2", {}, "我的投递"),
      el("span", { class: "muted small" }, "状态机由后端校验"),
    ),
    el("div", { class: "application-list-slot" }),
  );
  const slot = listPanel.querySelector(".application-list-slot");

  root.append(header, intro, resumePanel, listPanel);

  const load = () => {
    renderApplicationList(ctx).then((list) => {
      clear(slot);
      slot.appendChild(list);
    });
  };

  // Ensure an existing resume is discovered on first visit.
  ensureResumeVersion(ctx).catch(() => {
    // A missing resume is a normal state; the panel explains the prerequisite.
  }).finally(load);
}
