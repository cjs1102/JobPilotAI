# 只读岗位匹配报告（0015 阶段 C，决策 2 = C1）

离线、自包含的**单文件**报告生成器。它只渲染公开接口**已经返回**的内容：不重新打分、
不重新排序、不补数据、不写数据库、不新增服务端接口。

## 用法

```bash
# 离线模式（零网络 I/O、不需要会话）：渲染已抓取的响应体
python3 tools/job_report/render_report.py \
    --input captured_jobs.json --output job_report.html

# 联网模式：用既有 Bearer token 发一次只读 GET
python3 tools/job_report/render_report.py \
    --url 'http://127.0.0.1:8080/api/v1/jobs?with_match=true&page_size=50' \
    --token "$JOBPILOT_TOKEN" \
    --output job_report.html
```

`captured_jobs.json` 就是 `GET /api/v1/jobs?with_match=true[&strict=true]&...` 的响应体
（也可以直接保存浏览器/curl 的返回）。报告生成的 HTML 可直接用浏览器打开，无外部资源。

## 内容

- 每行：`rank`、`score` + `score_version`、公司/职位/地点/`freshness`、`matched_skills`、
  `missing_skills`、`reasons[]`（code/text/evidence）、`deal_breakers[]`、`deadline_urgency`、
  **原始岗位链接**（`source_url`）。
- **确定性排序**：行顺序 = 响应 `list` 的顺序（`sort=`/评分顺序），`rank` = 位置序号 1..N。
  `--sort-basis "<文本>"` 只在头部**记录**排序基准（默认 `API 返回顺序（未重排）`），工具不应用它。
- 没有 `reasons[]` 的行显示显式标记 `无匹配理由`（绝不空白）；没有 `deal_breakers[]` 显示 `无`；
  缺 `source_url` 显示 `—`。
- 顶部显示行数、`total`、`page/page_size`，以及 `strict` 模式下的 `truncated`。

## 只读保证

- `--input` 模式**不进行任何网络访问**；
- `--url` 模式**只发一次 `GET`**（附加既有 Bearer token），没有其他动词，不访问数据库；
- 只写一个文件：`--output`。

## 测试

```bash
python3 -m unittest discover -s tools/job_report/tests -v
```

11 个用例覆盖：行顺序与 `rank`、每行的原始链接、score/version/技能/理由/deal-breaker/紧迫度、
「无理由」显式标记、空列表、自包含（无外部资源/无脚本）、渲染确定性、`--input` 零网络 I/O、
`--url` 只发一次带 token 的 `GET`。仅使用 Python 标准库。
