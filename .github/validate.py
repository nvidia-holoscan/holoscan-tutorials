# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Check tutorial sources and local documentation without executing examples."""

import json
from collections import Counter
from pathlib import Path
import subprocess
from urllib.parse import unquote, urlsplit

from markdown_it import MarkdownIt
import nbformat


root = Path(__file__).resolve().parents[1]
counts = Counter()
markdown = MarkdownIt("commonmark")
paths = [root / "README.md", *sorted((root / "tutorials").rglob("*"))]
for path in paths:
    if not path.is_file():
        continue
    if path.suffix == ".py":
        compile(path.read_bytes(), str(path), "exec")
        counts["Python files"] += 1
    elif path.suffix == ".json":
        # These editor configurations intentionally permit comments/trailing commas.
        if ".vscode" in path.parts or path.name == "devcontainer.json":
            continue
        with path.open() as source:
            json.load(source)
        counts["JSON files"] += 1
    elif path.suffix == ".ipynb":
        notebook = nbformat.read(path, as_version=nbformat.NO_CONVERT)
        nbformat.validate(notebook)
        counts["notebooks"] += 1
    elif path.suffix == ".sh" or path.name in {"holohubCLI_wrapper", "nvcc-cross-sbsa"}:
        shell = "sh" if path.name == "nvcc-cross-sbsa" else "bash"
        subprocess.run([shell, "-n", str(path)], check=True)
        counts["shell scripts"] += 1
    elif path.suffix == ".md":
        tokens = markdown.parse(path.read_text())
        while tokens:
            token = tokens.pop()
            tokens.extend(token.children or [])
            attr = {"link_open": "href", "image": "src"}.get(token.type)
            if not attr:
                continue
            link = token.attrGet(attr)
            url = urlsplit(link)
            if url.scheme or url.netloc or not url.path:
                continue
            target = unquote(url.path)
            target = root / target.lstrip("/") if target.startswith("/") else path.parent / target
            if not target.exists():
                raise SystemExit(f"{path.relative_to(root)}: missing local link {link}")
            counts["local links"] += 1
        counts["Markdown files"] += 1

print("Validated " + ", ".join(f"{count} {kind}" for kind, count in counts.items()))
