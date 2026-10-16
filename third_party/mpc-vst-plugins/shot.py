import sys
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    b=p.chromium.launch(); pg=b.new_page(viewport={"width":1280,"height":1100}); pg.goto("file:///w/site-fixture/index.html")
    names=lambda: [pg.locator("article.card h2").nth(i).text_content() for i in range(pg.locator("article.card").count())]
    print("all :",names())
    for v in ("2x","3x"):
        pg.select_option("#f-os",v); print(v,":",names(), "| hash:", pg.evaluate("location.hash"))
    pg.select_option("#f-os",""); pg.screenshot(path="/w/shot-os.png",full_page=True)
    for i in range(pg.locator("article.card").count()):
        c=pg.locator("article.card").nth(i); print(c.locator("h2").text_content(),"->",[t.text_content() for t in c.locator(".tag").all()][-2:], "| meta MPC OS:", (c.locator("dt:has-text('MPC OS') + dd").text_content() if c.locator("dt:has-text('MPC OS')").count() else "(none)"))
    b.close()
