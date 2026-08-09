#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

import pymupdf


pdf_path = Path(sys.argv[1])
output_dir = Path(sys.argv[2])
output_dir.mkdir(parents=True, exist_ok=True)
document = pymupdf.open(pdf_path)
for index, page in enumerate(document):
    pixmap = page.get_pixmap(matrix=pymupdf.Matrix(2, 2), alpha=False)
    pixmap.save(output_dir / f"page-{index + 1}.png")
print(f"rendered_pages={len(document)}")
