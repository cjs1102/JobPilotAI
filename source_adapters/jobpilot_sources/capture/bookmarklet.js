/*
 * JobPilotAI manual-capture bookmarklet (ExecPlan 0014 Step 6).
 *
 * WHAT IT DOES
 *   Reads ONLY the page the user is already looking at (title, meta tags,
 *   JSON-LD JobPosting, a bounded excerpt of the visible text) and puts a JSON
 *   capture on the clipboard. The user then publishes it locally with
 *   `python -m jobpilot_sources publish-capture --file capture.json`.
 *
 * WHAT IT MUST NEVER DO (enforced by tests in tests/test_capture_bookmarklet.py)
 *   - send any network request (no fetch / XMLHttpRequest / sendBeacon /
 *     WebSocket / image beacon / form submission),
 *   - contain, request or store an API credential (the internal API secret
 *     never exists in the browser; the local publisher reads it from its own
 *     environment),
 *   - touch cookies, localStorage, sessionStorage or any other page state,
 *   - navigate, log in, solve a CAPTCHA, or bypass any access control.
 *
 * It is deliberately credential-free and network-free, so running it inside a
 * logged-in page cannot leak anything to a third party: the only output is text
 * the user already sees.
 */
(function (root, factory) {
  "use strict";

  var api = factory();

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }

  if (root) {
    root.jobpilotCapture = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  var CAPTURE_VERSION = 1;
  var DEFAULT_MAX_DESCRIPTION = 4000;
  var JOB_POSTING_TYPE = "jobposting";

  var ENTITIES = {
    "&amp;": "&",
    "&lt;": "<",
    "&gt;": ">",
    "&quot;": '"',
    "&#39;": "'",
    "&apos;": "'",
    "&nbsp;": " ",
    "&mdash;": "\u2014",
    "&ndash;": "\u2013",
    "&hellip;": "\u2026"
  };

  function clean(value) {
    if (typeof value !== "string") {
      return "";
    }

    return value.replace(/\s+/g, " ").trim();
  }

  function decodeEntities(value) {
    var text = String(value);

    Object.keys(ENTITIES).forEach(function (entity) {
      text = text.split(entity).join(ENTITIES[entity]);
    });

    return text.replace(/&#(\d+);/g, function (match, code) {
      var parsed = parseInt(code, 10);

      if (isNaN(parsed)) {
        return match;
      }

      return String.fromCharCode(parsed);
    });
  }

  function plainText(value) {
    if (typeof value !== "string" || !value) {
      return "";
    }

    var text = value.replace(/<(script|style)[^>]*>[\s\S]*?<\/\1>/gi, " ");
    text = text.replace(/<br\s*\/?>/gi, "\n");
    text = text.replace(/<\/(p|div|li|ul|ol|h[1-6]|tr|td)>/gi, "\n");
    text = text.replace(/<[^>]+>/g, " ");
    text = decodeEntities(text);

    var lines = text.split("\n").map(function (line) {
      return line.replace(/\s+/g, " ").trim();
    });

    return lines.filter(function (line) {
      return line.length > 0;
    }).join("\n");
  }

  function attributeOf(node, name) {
    if (!node || typeof node.getAttribute !== "function") {
      return "";
    }

    return node.getAttribute(name) || "";
  }

  function all(doc, selector) {
    if (!doc || typeof doc.querySelectorAll !== "function") {
      return [];
    }

    var found = doc.querySelectorAll(selector);

    if (!found) {
      return [];
    }

    if (typeof found.length === "number") {
      return Array.prototype.slice.call(found);
    }

    return [];
  }

  function firstOf(doc, selectors) {
    for (var index = 0; index < selectors.length; index += 1) {
      var nodes = all(doc, selectors[index]);

      if (nodes.length > 0) {
        return nodes[0];
      }
    }

    return null;
  }

  function metaContent(doc, keys) {
    var metas = all(doc, "meta");

    for (var index = 0; index < metas.length; index += 1) {
      var node = metas[index];
      var name = clean(
        attributeOf(node, "property") ||
          attributeOf(node, "name") ||
          attributeOf(node, "itemprop")
      ).toLowerCase();

      if (keys.indexOf(name) === -1) {
        continue;
      }

      var content = clean(attributeOf(node, "content"));

      if (content) {
        return content;
      }
    }

    return "";
  }

  function typeMatches(value, wanted) {
    if (typeof value === "string") {
      return value.toLowerCase() === wanted;
    }

    if (Object.prototype.toString.call(value) === "[object Array]") {
      return value.some(function (entry) {
        return typeMatches(entry, wanted);
      });
    }

    return false;
  }

  function collectPostings(document_, found) {
    if (!document_) {
      return;
    }

    if (Object.prototype.toString.call(document_) === "[object Array]") {
      document_.forEach(function (entry) {
        collectPostings(entry, found);
      });
      return;
    }

    if (typeof document_ !== "object") {
      return;
    }

    if (document_["@graph"]) {
      collectPostings(document_["@graph"], found);
    }

    if (typeMatches(document_["@type"], JOB_POSTING_TYPE)) {
      found.push(document_);
    }
  }

  function findJobPosting(doc) {
    var scripts = all(doc, 'script[type="application/ld+json"]');
    var found = [];

    scripts.forEach(function (node) {
      var body = node.textContent || "";

      if (!body) {
        return;
      }

      try {
        var parsed = JSON.parse(body);
        collectPostings(parsed, found);
      } catch (error) {
        /* A malformed JSON-LD block is skipped: never guessed at. */
      }
    });

    return found.length > 0 ? found[0] : null;
  }

  function firstString(value) {
    if (typeof value === "string") {
      return value;
    }

    if (Object.prototype.toString.call(value) === "[object Array]") {
      for (var index = 0; index < value.length; index += 1) {
        var candidate = firstString(value[index]);

        if (candidate) {
          return candidate;
        }
      }
    }

    return "";
  }

  function addressOf(posting) {
    var location = posting ? posting.jobLocation : null;

    if (Object.prototype.toString.call(location) === "[object Array]") {
      location = location.length > 0 ? location[0] : null;
    }

    if (!location) {
      return "";
    }

    if (typeof location === "string") {
      return clean(location);
    }

    var address = location.address || location;

    if (typeof address === "string") {
      return clean(address);
    }

    var locality = clean(address.addressLocality);
    var region = clean(address.addressRegion);

    if (locality && region && locality !== region) {
      return locality + "-" + region;
    }

    return locality || region;
  }

  function organizationOf(posting) {
    var organization = posting ? posting.hiringOrganization : null;

    if (!organization) {
      return "";
    }

    if (typeof organization === "string") {
      return clean(organization);
    }

    return clean(organization.name);
  }

  function salaryOf(posting, warnings) {
    var salary = posting ? posting.baseSalary : null;

    if (!salary) {
      return "";
    }

    if (typeof salary === "string") {
      return clean(salary);
    }

    warnings.push(
      "salary found on the page is not a plain string and was skipped; " +
        "paste the salary text by hand if it matters"
    );

    return "";
  }

  function educationOf(posting) {
    var value = posting ? posting.educationRequirements : null;
    var text = firstString(value);

    if (text) {
      return clean(text);
    }

    if (value && typeof value === "object" && !text) {
      return clean(value.credentialCategory);
    }

    return "";
  }

  function natureOf(posting) {
    var employment = firstString(posting ? posting.employmentType : null);
    var upper = employment.toUpperCase();

    if (upper.indexOf("INTERN") !== -1) {
      return "\u5b9e\u4e60";
    }

    /* Anything else is left for the publisher to map from page text. */
    return "";
  }

  function sourceUrlOf(doc, body) {
    var candidate = doc && doc.location && doc.location.href
      ? String(doc.location.href)
      : "";

    if (!candidate) {
      candidate = body ? String(body) : "";
    }

    var hash = candidate.indexOf("#");

    if (hash !== -1) {
      candidate = candidate.slice(0, hash);
    }

    return clean(candidate);
  }

  function buildJob(doc, posting, warnings) {
    var job = {};
    var title = clean(posting && posting.title);

    if (!title) {
      title = metaContent(doc, ["og:title", "twitter:title"]);
    }

    if (!title) {
      title = clean(doc ? doc.title : "");
    }

    var company = organizationOf(posting);

    if (!company) {
      company = metaContent(doc, ["og:site_name", "twitter:site"]);
    }

    if (!company) {
      warnings.push(
        "company name was not present on the page; fill it in before publishing"
      );
    }

    var description = plainText(posting && posting.description);
    var body = doc && doc.body && typeof doc.body.innerText === "string"
      ? doc.body.innerText
      : "";

    if (!description) {
      description = plainText(body);
      warnings.push(
        "description is an excerpt of the visible page text; review it before publishing"
      );
    }

    if (title) {
      job.title = title;
    }

    if (company) {
      job.company = company;
    }

    var location = addressOf(posting);

    if (location) {
      job.location = location;
    }

    var salary = salaryOf(posting, warnings);

    if (salary) {
      job.salary = salary;
    }

    if (description) {
      job.description = description;
    }

    var education = educationOf(posting);

    if (education) {
      job.education = education;
    }

    var nature = natureOf(posting);

    if (nature) {
      job.job_nature = nature;
    }

    var published = clean(posting && posting.datePosted);

    if (published) {
      job.published_at = published;
    }

    var deadline = clean(posting && posting.validThrough);

    if (deadline) {
      job.deadline = deadline;
    }

    return job;
  }

  function extract(doc, options) {
    var opts = options || {};
    var maxDescription = opts.maxDescription || DEFAULT_MAX_DESCRIPTION;
    var warnings = [];
    var posting = findJobPosting(doc);

    if (!posting) {
      warnings.push(
        "no JSON-LD JobPosting found on the page; only the visible page was used"
      );
    }

    var job = buildJob(doc, posting, warnings);
    var sourceUrl = sourceUrlOf(doc, opts.pageUrl);

    if (sourceUrl) {
      job.source_url = sourceUrl;
    } else {
      warnings.push("the page URL could not be read; source_url is missing");
    }

    if (job.description && job.description.length > maxDescription) {
      job.description = job.description.slice(0, maxDescription);
      warnings.push(
        "description was truncated to " + maxDescription + " characters"
      );
    }

    var artifact = {
      capture_version: CAPTURE_VERSION,
      captured_at: opts.capturedAt || new Date().toISOString(),
      page_url: sourceUrl,
      page_title: clean(doc ? doc.title : ""),
      job: job,
      warnings: warnings
    };

    var host = "";

    if (doc && doc.location && doc.location.host) {
      host = String(doc.location.host);
    }

    if (host) {
      artifact.page_host = host;
    }

    return artifact;
  }

  function serialize(artifact) {
    return JSON.stringify(artifact, null, 2);
  }

  function run(doc, options) {
    var artifact = extract(doc, options || {});
    var payload = serialize(artifact);
    var notify = function (message) {
      if (typeof alert === "function") {
        alert(message);
      }
    };

    if (typeof console !== "undefined" && console.log) {
      console.log(payload);
    }

    var clipboard =
      typeof navigator !== "undefined" && navigator.clipboard
        ? navigator.clipboard
        : null;

    if (clipboard && typeof clipboard.writeText === "function") {
      clipboard.writeText(payload).then(
        function () {
          notify(
            "JobPilotAI capture copied to the clipboard. " +
              "Run the local publisher to review and import it."
          );
        },
        function () {
          fallback(payload);
        }
      );
      return artifact;
    }

    fallback(payload);
    return artifact;
  }

  function fallback(payload) {
    if (typeof prompt === "function") {
      prompt(
        "Copy this JSON (Ctrl+C) into a local file, then run the local publisher:",
        payload
      );
      return;
    }

    if (typeof console !== "undefined" && console.warn) {
      console.warn(
        "Clipboard unavailable and no prompt; the capture is logged above."
      );
    }
  }

  return {
    VERSION: CAPTURE_VERSION,
    extract: extract,
    serialize: serialize,
    run: run,
    plainText: plainText
  };
});
