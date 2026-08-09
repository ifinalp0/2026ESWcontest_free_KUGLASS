# DOCX template execution contract

## Reference

- Retained reference: `/Users/mooyoung/Documents/건국대학교 2-1/DECA/쿠글(KUGLASS)/쿠글_외/임소경 개발계획서/임소경 개발 계획서.docx`
- SHA-256: `b867eb9fe841c78151aa72047aa3b685e05637da181c847695cbbc6e187dc0f9`
- Render evidence: `.tmp/prelim/reference-render/reference.pdf` and `.tmp/prelim/reference-render/pages/page-1.png` through `page-10.png`
- Structural evidence: `.tmp/prelim/template-evidence/reference-inspection.json`, `.tmp/prelim/template-evidence/template-style-evidence.json`
- Page count: 10
- Section count: 1
- Package: 17 parts, no drawings or embedded images. The body uses 8 tables and 162 top-level blocks.

## Page system

- One A4 portrait section: 7,560,310 × 10,692,130 EMU (210 × 297 mm).
- Margins: left/right 720,090 EMU (about 20 mm), top 698,500 EMU (about 19.4 mm), bottom 539,750 EMU (about 15 mm).
- Header distance 158,750 EMU; footer distance 0. Header and footer have no visible content.
- First/odd/even page variants are not used.
- Major content groups may begin on a new page. The source has no separate cover page; its first page starts with a centered title block.

## Typography and recurring visual roles

- Primary Korean face: `나눔명조`.
- Body: 11 pt, black, left aligned, compact paragraph spacing. The source alternates between single-spaced list paragraphs and a 1.6 line-spacing Hangul-conversion style; the new document should use a consistent readable 1.15–1.25 line spacing while retaining the same size and face.
- Major section: literal square marker `□`, 15 pt, bold, left aligned, with clear space above.
- Subsection: literal circle marker `○`, 11 pt, regular or semibold, left aligned.
- Technical subhead: 10.5–11 pt, bold, left aligned.
- Title: centered NanumMyeongjo, approximately 22–24 pt for the main line and 13–14 pt for the bracketed field line.
- The opening title block is bounded by a dark green top rule and pale green lower rule.
- No running header or footer is visible in the reference. The final document may add a restrained page-number footer only if it does not disrupt the source-derived appearance.

## Lists and tables

- Source lists are visually dash-led or middle-dot-led, with wrapped lines hanging beneath the text. The new document must implement real Word bullet numbering with explicit hanging indents rather than typed bullet characters.
- Tables use full or near-full usable width, thin pale blue-gray borders, generous cell padding, and blue-gray header fills.
- The first-page summary table uses a narrow label column and wide content column.
- Comparison/risk tables use three columns with the first column compact and the two descriptive columns wider.
- Workflow and study tables use a narrow step/topic column and a wide explanation/application column.
- The source schedule table is a dense 19-column Gantt. Because this report describes current status rather than a historic calendar, the final uses a readable status-and-phase schedule while retaining the tabular role and blue-gray treatment.
- All tables must have explicit DXA widths, table indentation matching body text, consistent cell widths, repeatable header rows, no fixed row heights, and rows that do not split when this avoids orphaned fragments.

## Content flow and slot map

1. Opening title block — rewrite for the school creative-design preliminary report and current reference date.
2. `□ 개발 개요` — summary metadata table; background; objectives; current four-channel scope.
3. `□ 개발 방향 및 전략` — responsibility split; software/hardware methods; protocol and safety strategy; differentiation; current status; risks; expected use.
4. `□ 작품 상세 설명 및 지원 장비 사용 계획` — operation flow table; channel mapping; implemented software/hardware; demonstration and verification plan.
5. `□ 개발 일정` — replace the obsolete July–October eight-channel plan with a current-status and remaining-work schedule.
6. `□ 팀 구성 및 역량` — preserve the four-person roster from the retained document but rewrite duties to match the current KUGLASS_DEV architecture.
7. `□ 참고 문헌 및 기준 자료` — add repository-canonical documents and component references used by the current implementation.

## Editable and preserve-only package parts

- Editable: `word/document.xml`, `docProps/core.xml`, `docProps/app.xml`, and style/numbering definitions needed for the rewritten document.
- Preserve as the starting authority unless a formatting defect requires a controlled update: page section geometry, theme color basis, font table, relationships, settings, web settings, custom XML, footnotes, and endnotes.
- The source contains no drawings. No source media relationship needs to be recreated.
- The final build starts from a working copy of the retained DOCX, removes the obsolete body content while keeping its section properties, and builds the new current-state report in that copy.

## Fidelity and delivery gates

- The retained reference must remain byte-for-byte unchanged at the recorded SHA-256.
- The final must visibly retain the source-derived A4 geometry, green title rules, NanumMyeongjo hierarchy, `□`/`○` section grammar, and pale blue-gray table treatment.
- Improvements are intentional where the source has awkward table splits or large blank remainders: keep tables with their captions, repeat table headers, avoid clipped or pinned cell text, and use explicit page breaks between major report groups.
- Every final page must be rendered through Microsoft Word to PDF and then to page PNGs for visual inspection, because the packaged LibreOffice renderer is unavailable on this host.
- No obsolete eight-channel, Raspberry Pi, external weather, rear camera, VEML7700, or 72 V performance-verified claim may survive. Unmeasured hardware, optical, thermal, or calibration values must be labeled as pending rather than asserted.
