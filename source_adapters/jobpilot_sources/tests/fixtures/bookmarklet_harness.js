/* Node harness for the bookmarklet extraction tests (offline, no network).
 *
 * Builds two fake documents (a rich page with JSON-LD and a minimal page), runs
 * the packaged bookmarklet's pure `extract` function against them, and prints
 * the resulting artifacts as JSON for the Python test to assert on.
 *
 * Usage: node bookmarklet_harness.js <path-to-bookmarklet.js>
 */
"use strict";

const path = process.argv[2];

if (!path) {
  console.error("usage: node bookmarklet_harness.js <bookmarklet.js>");
  process.exit(2);
}

const capture = require(path);

function meta(name, content) {
  return {
    getAttribute(attribute) {
      if (attribute === "property" || attribute === "name") {
        return name;
      }

      if (attribute === "content") {
        return content;
      }

      return null;
    }
  };
}

function script(type, body) {
  return {
    getAttribute(attribute) {
      return attribute === "type" ? type : null;
    },
    textContent: body
  };
}

function fakeDocument(options) {
  const scripts = options.scripts || [];
  const metas = options.metas || [];

  return {
    title: options.title || "",
    location: {
      href: options.href || "",
      host: options.host || ""
    },
    body: { innerText: options.innerText || "" },
    querySelectorAll(selector) {
      if (selector.indexOf("application/ld+json") !== -1) {
        return scripts;
      }

      if (selector === "meta") {
        return metas;
      }

      return [];
    }
  };
}

const richJsonLd = JSON.stringify({
  "@context": "https://schema.org",
  "@graph": [
    {
      "@type": "Organization",
      name: "示例科技有限公司"
    },
    {
      "@type": "JobPosting",
      title: "C++ 后端开发工程师",
      url: "https://jobs.example.invalid/campus/1001",
      datePosted: "2026-09-20T08:00:00+08:00",
      validThrough: "2026-10-31",
      description: "<p>负责后端服务开发</p><ul><li>熟悉 Linux</li></ul>",
      educationRequirements: { credentialCategory: "本科" },
      employmentType: "INTERN",
      baseSalary: "15-25K",
      hiringOrganization: { name: "示例科技有限公司" },
      jobLocation: {
        address: { addressLocality: "深圳市", addressRegion: "南山区" }
      }
    }
  ]
});

const richDocument = fakeDocument({
  title: "C++ 后端开发工程师 - 示例科技",
  href: "https://jobs.example.invalid/campus/1001#apply",
  host: "jobs.example.invalid",
  innerText: "页面可见文本",
  scripts: [
    script("application/ld+json", richJsonLd),
    script("application/ld+json", "{ this is not valid json }")
  ],
  metas: [
    meta("og:site_name", "示例科技"),
    meta("og:title", "og 标题（JSON-LD 优先）")
  ]
});

const minimalDocument = fakeDocument({
  title: "某岗位 - 登录可见",
  href: "https://app.example-ats.invalid/social/2002",
  host: "app.example-ats.invalid",
  innerText:
    "某岗位\n薪资：面议\n公司：明示公司在可见文本里\n" + "长文本".repeat(5)
});

const longDocument = fakeDocument({
  title: "长描述岗位",
  href: "https://x.invalid/3",
  host: "x.invalid",
  innerText: "内容".repeat(2000)
});

const output = {
  rich: capture.extract(richDocument),
  minimal: capture.extract(minimalDocument),
  truncated: capture.extract(longDocument, { maxDescription: 50 }),
  plainText: capture.plainText("<p>A&amp;B</p><script>x()</script><p>C</p>"),
  version: capture.VERSION
};

process.stdout.write(JSON.stringify(output, null, 1));
