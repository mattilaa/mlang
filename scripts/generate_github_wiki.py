#!/usr/bin/env python3
"""Generate GitHub Wiki markdown pages from the repository documentation."""

from __future__ import annotations

import argparse
import os
import re
import shutil
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = REPO_ROOT / "docs" / "wiki"
DEFAULT_REPO_URL = "https://github.com/mattilaa/mlang"


@dataclass(frozen=True)
class Page:
    source: Path
    title: str
    wiki_name: str
    group: str


PAGES: list[Page] = [
    Page(Path("docs/README.md"), "MLang Documentation", "Home", "Start Here"),
    Page(Path("README.md"), "Project README", "Project-README", "Start Here"),
    Page(Path("docs/quick_guide.md"), "Quick Guide", "Quick-Guide", "Language"),
    Page(Path("docs/language_syntax.md"), "Language Syntax", "Language-Syntax", "Language"),
    Page(Path("docs/language_attributes.md"), "Language Attributes", "Language-Attributes", "Language"),
    Page(Path("docs/compiler_diagnostics.md"), "Compiler Diagnostics", "Compiler-Diagnostics", "Language"),
    Page(Path("docs/package_manager.md"), "Package Manager", "Package-Manager", "Tooling"),
    Page(Path("docs/stdlib_mlang_api.md"), "Stdlib Module API", "Stdlib-Module-API", "Standard Library"),
    Page(Path("docs/stdlib_misc_modules.md"), "Stdlib Misc Modules", "Stdlib-Misc-Modules", "Standard Library"),
    Page(Path("docs/stdlib_changes_2026-03-13.md"), "Stdlib Changes 2026-03-13", "Stdlib-Changes-2026-03-13", "Standard Library"),
    Page(Path("stdlib/README.md"), "Stdlib README", "Stdlib-README", "Standard Library"),
    Page(Path("docs/examples.md"), "Examples", "Examples", "Examples"),
    Page(Path("docs/uml_ui_generator.md"), "UML UI Generator", "UML-UI-Generator", "Examples"),
    Page(Path("examples/uml_ui_generator/README.md"), "UML UI Generator Example", "UML-UI-Generator-Example", "Examples"),
    Page(Path("bootstrap/README.md"), "Bootstrap", "Bootstrap", "Tooling"),
    Page(Path("tools/mlangpkg/README.md"), "mlangpkg", "Mlangpkg", "Tooling"),
    Page(Path("tests/README.md"), "Tests", "Tests", "Tooling"),
]

MANPAGES: list[tuple[Path, str]] = [
    (Path("docs/man/mlang.1"), "mlang"),
    (Path("docs/man/mlang-frontend.1"), "mlang-frontend"),
    (Path("docs/man/mlang-frontend-mla.1"), "mlang-frontend-mla"),
    (Path("docs/man/mlang-format.1"), "mlang-format"),
    (Path("docs/man/mlang-pkg.1"), "mlang-pkg"),
    (Path("docs/man/mlangd.1"), "mlangd"),
    (Path("docs/man/mlangd-mla.1"), "mlangd-mla"),
]


def slugify_wiki_name(name: str) -> str:
    stem = Path(name).stem
    stem = stem.replace("_", "-").replace(" ", "-")
    stem = re.sub(r"[^A-Za-z0-9.-]+", "-", stem)
    stem = re.sub(r"-+", "-", stem).strip("-")
    return stem or "Page"


def build_source_map(pages: list[Page]) -> dict[Path, str]:
    mapping: dict[Path, str] = {}
    for page in pages:
        mapping[page.source] = page.wiki_name
        mapping[page.source.resolve()] = page.wiki_name
    return mapping


def wiki_link_for(target: str, current_source: Path, source_map: dict[Path, str], repo_url: str) -> str:
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
        return target
    if target.startswith("#"):
        return target

    path_part, hash_part = (target.split("#", 1) + [""])[:2] if "#" in target else (target, "")
    if not path_part:
        return target

    candidate = (current_source.parent / path_part).resolve()
    wiki_name = source_map.get(candidate)
    if wiki_name:
        return f"{wiki_name}{'#' + hash_part if hash_part else ''}"

    repo_relative = os.path.normpath(str((current_source.parent / path_part).as_posix()))
    if repo_relative.startswith("../"):
        repo_relative = repo_relative.lstrip("./")
        while repo_relative.startswith("../"):
            repo_relative = repo_relative[3:]
    return f"{repo_url}/blob/main/{repo_relative}{'#' + hash_part if hash_part else ''}"


def rewrite_markdown_links(text: str, current_source: Path, source_map: dict[Path, str], repo_url: str) -> str:
    def replace_inline(match: re.Match[str]) -> str:
        label = match.group(1)
        target = match.group(2).strip()
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1]
        return f"[{label}]({wiki_link_for(target, current_source, source_map, repo_url)})"

    text = re.sub(r"(?<!!)\[([^\]]+)\]\(([^)]+)\)", replace_inline, text)

    def replace_reference(match: re.Match[str]) -> str:
        label = match.group(1)
        target = match.group(2).strip()
        return f"[{label}]: {wiki_link_for(target, current_source, source_map, repo_url)}"

    return re.sub(r"^\[([^\]]+)\]:\s*(\S+)\s*$", replace_reference, text, flags=re.MULTILINE)


def ensure_title(text: str, title: str) -> str:
    if re.match(r"^#\s+", text):
        return text
    return f"# {title}\n\n{text.lstrip()}"


def roff_to_markdown(source: Path, command_name: str) -> str:
    text = source.read_text(encoding="utf-8", errors="replace")
    lines = [f"# `{command_name}` manual", "", "```roff"]
    lines.extend(text.rstrip().splitlines())
    lines.extend(["```", ""])
    return "\n".join(lines)


def write_page(page: Page, out_dir: Path, source_map: dict[Path, str], repo_url: str) -> None:
    source = REPO_ROOT / page.source
    if not source.exists():
        raise FileNotFoundError(source)
    text = source.read_text(encoding="utf-8")
    text = ensure_title(text, page.title)
    text = rewrite_markdown_links(text, page.source, source_map, repo_url)
    text = text.rstrip() + "\n"
    (out_dir / f"{page.wiki_name}.md").write_text(text, encoding="utf-8")


def write_sidebar(out_dir: Path, pages: list[Page], manpage_names: list[str]) -> None:
    by_group: dict[str, list[Page]] = {}
    for page in pages:
        by_group.setdefault(page.group, []).append(page)

    lines = ["# MLang Wiki", ""]
    group_order = ["Start Here", "Language", "Standard Library", "Tooling", "Examples"]
    for group in group_order:
        group_pages = by_group.get(group, [])
        if not group_pages:
            continue
        lines.extend([f"## {group}"])
        for page in group_pages:
            lines.append(f"- [{page.title}]({page.wiki_name})")
        lines.append("")

    if manpage_names:
        lines.extend(["## Man Pages"])
        for command_name in manpage_names:
            wiki_name = f"Man-{slugify_wiki_name(command_name)}"
            lines.append(f"- [`{command_name}`]({wiki_name})")
        lines.append("")

    (out_dir / "_Sidebar.md").write_text("\n".join(lines), encoding="utf-8")


def generated_filenames(pages: list[Page]) -> set[str]:
    names = {f"{page.wiki_name}.md" for page in pages}
    names.add("_Sidebar.md")
    for _source, command_name in MANPAGES:
        names.add(f"Man-{slugify_wiki_name(command_name)}.md")
    return names


def clean_output_dir(out_dir: Path, pages: list[Page]) -> None:
    if not out_dir.exists():
        return
    if (out_dir / ".git").exists():
        for filename in generated_filenames(pages):
            path = out_dir / filename
            if path.exists() and path.is_file():
                path.unlink()
        return
    shutil.rmtree(out_dir)


def generate(out_dir: Path, repo_url: str, clean: bool) -> None:
    pages = [page for page in PAGES if (REPO_ROOT / page.source).exists()]
    source_map = build_source_map(pages)

    if clean and out_dir.exists():
        clean_output_dir(out_dir, pages)
    out_dir.mkdir(parents=True, exist_ok=True)

    for page in pages:
        write_page(page, out_dir, source_map, repo_url.rstrip("/"))

    manpage_names: list[str] = []
    for source, command_name in MANPAGES:
        full_source = REPO_ROOT / source
        if not full_source.exists():
            continue
        wiki_name = f"Man-{slugify_wiki_name(command_name)}"
        (out_dir / f"{wiki_name}.md").write_text(
            roff_to_markdown(full_source, command_name), encoding="utf-8"
        )
        manpage_names.append(command_name)

    write_sidebar(out_dir, pages, manpage_names)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate GitHub Wiki markdown pages from MLang documentation."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"output directory, default: {DEFAULT_OUTPUT}",
    )
    parser.add_argument(
        "--repo-url",
        default=DEFAULT_REPO_URL,
        help=f"repository URL used for links to source files, default: {DEFAULT_REPO_URL}",
    )
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="do not delete the output directory before generation",
    )
    args = parser.parse_args()

    out_dir = args.output if args.output.is_absolute() else REPO_ROOT / args.output
    generate(out_dir, args.repo_url, clean=not args.no_clean)
    print(f"Generated GitHub Wiki pages in: {out_dir}")
    print("Commit this directory to keep the wiki source in the repository.")
    print("To publish to GitHub Wiki, rerun with --output pointing at a mlang.wiki clone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
