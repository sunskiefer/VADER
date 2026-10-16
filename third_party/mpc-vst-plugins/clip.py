from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    b=p.chromium.launch(); pg=b.new_page(viewport={"width":1280,"height":900}); pg.goto("file:///w/site-fixture/index.html")
    box=pg.locator(".tools").bounding_box(); clip={"x":box["x"],"y":box["y"]-10,"width":box["width"],"height":box["height"]+20}
    pg.screenshot(path="/w/clip1.png",clip=clip)
    print("selected text:", pg.evaluate("() => { const s=document.querySelector('#f-os'); return s.options[s.selectedIndex].text }"), "| computed:", pg.evaluate("() => { const s=getComputedStyle(document.querySelector('#f-os')); return [s.color,s.fontSize,s.width,s.paddingLeft] }"))
    print("dist  computed:", pg.evaluate("() => { const s=getComputedStyle(document.querySelector('#f-dist')); return [s.color,s.fontSize,s.width,s.paddingLeft] }"))
    pg.select_option("#f-os","2x"); pg.screenshot(path="/w/clip2.png",clip=clip)
    b.close()
