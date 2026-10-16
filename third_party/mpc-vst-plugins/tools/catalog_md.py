"""A small Markdown to HTML converter for the catalog site's guide pages (catalog/pages/*.md). Standard library only.

Supports: `#`..`###` headings (with ids), paragraphs, `-`/`*` and `1.` lists (one level, wrapped lines allowed),
fenced code blocks, `>` callouts, pipe tables, collapsible sections (a line `::: details Title`, the content, then a line
`:::`; no nesting), and inline `code`, **bold**, *italic*, [links](url). Everything is
HTML-escaped first, so page text can't inject markup; link targets must be http(s), relative, or #anchors.
A page starts with front matter between `---` lines: title, nav (menu label), order (menu position), summary.
"""
import re
from html import escape

_LINK = re.compile(r"\[([^\]]+)\]\(([^)\s]+)\)")


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", re.sub(r"<[^>]+>", "", text).lower()).strip("-") or "section"


def inline(text):
    codes = []

    def keep(m):
        codes.append("<code>%s</code>" % escape(m.group(1), quote=False))
        return "\x00%d\x00" % (len(codes) - 1)
    text = re.sub(r"`([^`]+)`", keep, text)
    text = escape(text, quote=False)

    def link(m):
        url = m.group(2).replace("&amp;", "&")
        if not re.match(r"(https?://|#|[\w./-]+(#[\w-]+)?$)", url):
            return m.group(0)
        return '<a href="%s">%s</a>' % (escape(url), m.group(1))
    text = _LINK.sub(link, text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


def front_matter(src):
    meta = {}
    if src.startswith("---\n"):
        head, _, src = src[4:].partition("\n---\n")
        for line in head.splitlines():
            k, _, v = line.partition(":")
            meta[k.strip()] = v.strip()
    return meta, src


def _cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def render(src):
    lines = src.splitlines()
    out, i, para = [], 0, []

    def flush():
        if para:
            out.append("<p>%s</p>" % inline(" ".join(para)))
            para.clear()
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("::: details "):
            flush()
            title = ln[len("::: details "):].strip()
            i += 1
            inner = []
            while i < len(lines) and lines[i].strip() != ":::":
                inner.append(lines[i])
                i += 1
            i += 1
            out.append('<details class="fold" id="%s"><summary>%s</summary>%s</details>' % (slug(title), inline(title), render("\n".join(inner))))
        elif ln.startswith("```"):
            flush()
            i += 1
            code = []
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            out.append("<pre><code>%s</code></pre>" % escape("\n".join(code), quote=False))
            i += 1
        elif re.match(r"#{1,3} ", ln):
            flush()
            n = len(ln) - len(ln.lstrip("#"))
            text = ln[n + 1:].strip()
            out.append("<h%d id=\"%s\">%s</h%d>" % (n, slug(text), inline(text), n))
            i += 1
        elif ln.startswith(">"):
            flush()
            q = []
            while i < len(lines) and lines[i].startswith(">"):
                q.append(lines[i][1:].strip())
                i += 1
            out.append("<blockquote><p>%s</p></blockquote>" % inline(" ".join(q)))
        elif "|" in ln and i + 1 < len(lines) and re.match(r"^\s*\|?\s*:?-{2,}", lines[i + 1]):
            flush()
            head = _cells(ln)
            i += 2
            rows = []
            while i < len(lines) and "|" in lines[i]:
                rows.append(_cells(lines[i]))
                i += 1
            out.append('<div class="tablewrap"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (
                "".join("<th>%s</th>" % inline(c) for c in head),
                "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r) for r in rows)))
        elif re.match(r"(\s*[-*] |\d+\. )", ln):
            flush()
            ordered = bool(re.match(r"\d+\. ", ln))
            items = []
            while i < len(lines) and (re.match(r"([-*] |\d+\. )", lines[i]) or (lines[i].startswith("  ") and items and lines[i].strip())):
                if re.match(r"([-*] |\d+\. )", lines[i]):
                    items.append([re.sub(r"^([-*]|\d+\.) ", "", lines[i])])
                else:
                    items[-1].append(lines[i].strip())
                i += 1
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(" ".join(x)) for x in items), tag))
        elif not ln.strip():
            flush()
            i += 1
        else:
            para.append(ln.strip())
            i += 1
    flush()
    return "\n".join(out)
