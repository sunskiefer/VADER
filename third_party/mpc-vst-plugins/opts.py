from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    b=p.chromium.launch(); pg=b.new_page(); pg.goto("file:///w/site-fixture/index.html")
    for sel in ("#f-dist","#f-os"):
        print(sel, pg.evaluate("s => Array.from(document.querySelector(s).options).map(o => [o.value,o.text])", sel), "| value:", repr(pg.evaluate("s => document.querySelector(s).value", sel)))
    b.close()
