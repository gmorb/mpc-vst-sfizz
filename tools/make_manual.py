#!/usr/bin/env python3
"""docs/manual/manual.html -> docs/MANUAL.pdf with headless Chromium (Playwright).   tools/make_manual.py"""
import os
from playwright.sync_api import sync_playwright
HERE = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(HERE, "..", "docs", "manual", "manual.html")
out = os.path.join(HERE, "..", "docs", "MANUAL.pdf")
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page()
    pg.goto("file://" + os.path.abspath(src))
    pg.wait_for_load_state("networkidle")
    pg.pdf(path=out, format="A4", print_background=True, prefer_css_page_size=True,
           display_header_footer=True, header_template="<span></span>",
           footer_template='<div style="width:100%;font-size:8px;color:#777;text-align:center;font-family:sans-serif">'
                           'sfizz for MPC OS · <span class="pageNumber"></span> / <span class="totalPages"></span></div>',
           margin={"top": "18mm", "bottom": "18mm", "left": "16mm", "right": "16mm"})
    b.close()
print("wrote", os.path.abspath(out))
