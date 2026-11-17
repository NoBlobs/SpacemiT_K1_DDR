# SPDX-License-Identifier: GPL-3.0-or-later
# Sphinx configuration. Build the HTML with `make` in this folder.
import subprocess
from pathlib import Path

from docutils import nodes

project = "SpacemiT K1 free DDR init firmware"
author = "Thalles Campagnani"
copyright = "2026, Thalles Campagnani"
language = "en"

# sphinx_rtd_dark_mode adds a light/dark toggle button to the RTD theme;
# the choice is remembered by the browser.
extensions = ["sphinx_rtd_theme", "sphinx_rtd_dark_mode"]
default_dark_mode = False
exclude_patterns = ["_build", ".venv", "README.md", "requirements.txt"]

# Read the Docs theme (same as docs.openmc.org)
html_theme = "sphinx_rtd_theme"
html_title = "SpacemiT K1 — free DDR init firmware"
html_show_sourcelink = False
html_theme_options = {
    "navigation_depth": 3,
    "collapse_navigation": False,
    "sticky_navigation": True,
    "prev_next_buttons_location": "both",
}
pygments_style = "sphinx"

# _static/tables.css: let table cells wrap (the theme keeps them on one line).
# _static/theme_persist.js: keep the light/dark choice across local files.
html_static_path = ["_static"]
html_css_files = ["tables.css"]
html_js_files = ["theme_persist.js"]

# ---------------------------------------------------------------------------
# Links to the clean-room specification submodule on GitHub.
#
#   :dc:`address-map`            docsClean/address-map.md
#   :dc:`mode-registers`         docsClean/mode-registers.md
#   :dc:`docsClean/ <README>`    explicit title, links to the submodule root
# ---------------------------------------------------------------------------
REPO_URL = "https://github.com/NoBlobs/SpacemiT-K1-U_Boot-DDR_init"
REPO_BRANCH = "main"
DOCSCLEAN_URL = "https://github.com/NoBlobs-docsClean/SpacemiT_K1_DDR"

ROOT = Path(__file__).resolve().parent.parent


def _docsclean_commit():
    """Commit of docsClean pinned by this repository (gitlink), so links show
    exactly the revision the documentation was written against."""
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-s", "docsClean"],
                             capture_output=True, text=True, check=True)
        fields = out.stdout.split()
        if len(fields) >= 2 and fields[0] == "160000":
            return fields[1]
    except (OSError, subprocess.CalledProcessError):
        pass
    return "main"


DOCSCLEAN_REV = _docsclean_commit()


def _split(text):
    """Accept "title <target>" and return (title, target)."""
    if text.endswith(">") and "<" in text:
        title, _, target = text[:-1].rpartition("<")
        return title.strip() or None, target.strip()
    return None, text.strip()


def dc_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    title, target = _split(text)
    if target in ("", "/", "README"):
        url = f"{DOCSCLEAN_URL}/tree/{DOCSCLEAN_REV}"
        node = nodes.reference(rawtext, title or "docsClean/", refuri=url)
        return [node], []
    fname = target if target.endswith(".md") else target + ".md"
    url = f"{DOCSCLEAN_URL}/blob/{DOCSCLEAN_REV}/{fname}"
    node = nodes.reference(rawtext, title or f"docsClean/{target}", refuri=url)
    return [node], []


def setup(app):
    app.add_role("dc", dc_role)
    return {"parallel_read_safe": True}
