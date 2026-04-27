#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple


CPP_EXTS: Set[str] = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
}


DEFAULT_EXCLUDE_DIRS = {
    ".git",
    "build",
    "build_runfix_clang",
    "build-release",
    "build-release",
    "distfiles",
    "sdk",
    "3rdparty",
    "third_party",
    "skia",
    "gperftools",
    "appimagetool-squashfs-root",
    "linuxdeploy-squashfs-root",
    "linuxdeploy-plugin-qt-squashfs-root",
    ".cppcheck-cache",
    ".localdeps",
}


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

CLASS_STRUCT_RE = re.compile(
    r"""^\s*
        (?:(?:template\s*<[^;{>]*>\s*)+)?
        (class|struct)\s+
        ([A-Za-z_]\w*)
        (?:\s*:\s*[^;{]+)?\s*
        \{?
        """,
    re.VERBOSE,
)


FUNC_DEF_RE = re.compile(
    r"""
    (?P<prefix>^|\s)
    (?P<name>(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*))
    \s*\(
        (?P<args>[^;{}()]*(?:\([^)]*\)[^;{}()]*)*)
    \)\s*
    (?P<suffix>
        (?:const\s*)?
        (?:noexcept\s*)?
        (?:->\s*[^;{]+?\s*)?
    )
    \{
    """,
    re.VERBOSE,
)


def _iter_source_files(
    roots: List[Path], exclude_dirs: Set[str]
) -> Iterable[Path]:
    for base in roots:
        if not base.exists():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in exclude_dirs]
            for fn in filenames:
                p = Path(dirpath) / fn
                if p.suffix.lower() in CPP_EXTS:
                    yield p


def _strip_line_comments_preserve_strings(line: str) -> str:
    out: List[str] = []
    i = 0
    in_squote = False
    in_dquote = False
    escape = False
    while i < len(line):
        ch = line[i]
        nxt = line[i + 1] if i + 1 < len(line) else ""
        if escape:
            out.append(ch)
            escape = False
            i += 1
            continue
        if ch == "\\":
            out.append(ch)
            escape = True
            i += 1
            continue
        if not in_squote and ch == '"' and not in_dquote:
            in_dquote = True
            out.append(ch)
            i += 1
            continue
        if in_dquote and ch == '"':
            in_dquote = False
            out.append(ch)
            i += 1
            continue
        if not in_dquote and ch == "'" and not in_squote:
            in_squote = True
            out.append(ch)
            i += 1
            continue
        if in_squote and ch == "'":
            in_squote = False
            out.append(ch)
            i += 1
            continue
        if not in_squote and not in_dquote and ch == "/" and nxt == "/":
            break
        out.append(ch)
        i += 1
    return "".join(out)


def _remove_block_comments(text: str) -> str:
    out: List[str] = []
    i = 0
    in_block = False
    in_squote = False
    in_dquote = False
    escape = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if escape:
            out.append(ch)
            escape = False
            i += 1
            continue
        if ch == "\\":
            out.append(ch)
            escape = True
            i += 1
            continue
        if not in_block:
            if not in_squote and ch == '"' and not in_dquote:
                in_dquote = True
                out.append(ch)
                i += 1
                continue
            if in_dquote and ch == '"':
                in_dquote = False
                out.append(ch)
                i += 1
                continue
            if not in_dquote and ch == "'" and not in_squote:
                in_squote = True
                out.append(ch)
                i += 1
                continue
            if in_squote and ch == "'":
                in_squote = False
                out.append(ch)
                i += 1
                continue
            if not in_squote and not in_dquote and ch == "/" and nxt == "*":
                in_block = True
                i += 2
                continue
            out.append(ch)
            i += 1
            continue
        else:
            if ch == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            if ch == "\n":
                out.append("\n")
            i += 1
            continue
    return "".join(out)


def _clean_text_for_parse(text: str) -> str:
    text = _remove_block_comments(text)
    lines = text.splitlines(True)
    cleaned = [_strip_line_comments_preserve_strings(ln) for ln in lines]
    return "".join(cleaned)


@dataclass(frozen=True)
class Symbol:
    kind: str
    name: str
    qualified_name: str
    file: str
    line: int


def _extract_includes(lines: List[str]) -> List[Tuple[str, int]]:
    includes: List[Tuple[str, int]] = []
    for idx, ln in enumerate(lines, start=1):
        m = INCLUDE_RE.match(ln)
        if m:
            includes.append((m.group(1).strip(), idx))
    return includes


def _extract_class_struct(lines: List[str], file_rel: str) -> List[Symbol]:
    out: List[Symbol] = []
    for idx, ln in enumerate(lines, start=1):
        m = CLASS_STRUCT_RE.match(ln)
        if not m:
            continue
        kind = m.group(1)
        name = m.group(2)
        out.append(Symbol(kind=kind, name=name, qualified_name=name, file=file_rel, line=idx))
    return out


def _extract_function_defs(clean_text: str, file_rel: str) -> List[Symbol]:
    out: List[Symbol] = []
    for m in FUNC_DEF_RE.finditer(clean_text):
        qualified = m.group("name")
        if qualified in {"if", "for", "while", "switch", "catch"}:
            continue
        start = m.start()
        line = clean_text.count("\n", 0, start) + 1
        name = qualified.split("::")[-1]
        out.append(Symbol(kind="function", name=name, qualified_name=qualified, file=file_rel, line=line))
    return out


def _to_posix_rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def build_map(
    root: Path,
    scan_paths: List[Path],
    exclude_dirs: Set[str],
    include_external: bool,
) -> Dict:
    root = root.resolve()
    files: Dict[str, Dict] = {}
    all_project_files: Set[str] = set()
    scan_roots = [p.resolve() for p in scan_paths]

    for p in _iter_source_files(scan_roots, exclude_dirs):
        all_project_files.add(_to_posix_rel(p, root))

    include_roots = [
        root / "src",
        root / "src" / "core",
        root / "src" / "app",
        root / "src" / "ui",
    ]
    for sp in scan_paths:
        include_roots.append(sp)
    include_roots = [p.resolve() for p in dict.fromkeys(include_roots)]

    by_basename: Dict[str, List[str]] = {}
    for rel in all_project_files:
        by_basename.setdefault(Path(rel).name, []).append(rel)
    for k in by_basename:
        by_basename[k].sort()

    def resolve_include(inc_path: str) -> List[str]:
        inc_norm = inc_path.replace("\\", "/")
        inc_norm = inc_norm.lstrip("./")
        while inc_norm.startswith("../"):
            inc_norm = inc_norm[3:]
        resolved: List[str] = []

        for base in include_roots:
            cand = (base / inc_norm)
            if cand.exists() and cand.is_file():
                rel = _to_posix_rel(cand, root)
                if rel in all_project_files:
                    resolved.append(rel)
        if resolved:
            return sorted(set(resolved))

        bn = Path(inc_norm).name
        cands = by_basename.get(bn, [])
        if len(cands) == 1:
            return cands

        suffix_matches = [p for p in cands if p.endswith("/" + inc_norm) or p == inc_norm]
        if len(suffix_matches) == 1:
            return suffix_matches
        return []

    for p in sorted(_iter_source_files(scan_roots, exclude_dirs), key=lambda x: x.as_posix()):
        file_rel = _to_posix_rel(p, root)
        try:
            raw = p.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue
        cleaned = _clean_text_for_parse(raw)
        raw_lines = raw.splitlines()
        includes = _extract_includes(raw_lines)
        symbols = []
        symbols.extend(_extract_class_struct(raw_lines, file_rel))
        symbols.extend(_extract_function_defs(cleaned, file_rel))

        include_items: List[Dict] = []
        for inc, line in includes:
            inc_norm = inc.replace("\\", "/")
            resolved = resolve_include(inc_norm)
            is_project = bool(resolved)
            if include_external or is_project:
                include_items.append(
                    {
                        "path": inc,
                        "line": line,
                        "is_project": bool(is_project),
                        "resolved_to": resolved,
                    }
                )

        files[file_rel] = {
            "includes": include_items,
            "symbols": [
                {
                    "kind": s.kind,
                    "name": s.name,
                    "qualified_name": s.qualified_name,
                    "line": s.line,
                }
                for s in sorted(symbols, key=lambda x: (x.kind, x.line, x.qualified_name))
            ],
        }

    include_edges: List[Tuple[str, str]] = []
    for f, info in files.items():
        for inc in info["includes"]:
            if inc["is_project"]:
                for dst in inc.get("resolved_to", []):
                    include_edges.append((f, dst))

    include_reverse: Dict[str, List[str]] = {}
    for src, dst in include_edges:
        include_reverse.setdefault(dst, []).append(src)
    for k in include_reverse:
        include_reverse[k].sort()

    symbols_index: List[Dict] = []
    for f, info in files.items():
        for s in info["symbols"]:
            symbols_index.append(
                {
                    "kind": s["kind"],
                    "name": s["name"],
                    "qualified_name": s["qualified_name"],
                    "file": f,
                    "line": s["line"],
                }
            )

    return {
        "root": str(root),
        "files": files,
        "include_graph": {
            "edges": include_edges,
            "reverse_index": include_reverse,
        },
        "symbols": symbols_index,
    }


def write_dot(include_edges: List[Tuple[str, str]], out_path: Path) -> None:
    out_path.write_text(
        "digraph includes {\n"
        "  rankdir=LR;\n"
        "  node [shape=box, fontsize=10];\n"
        + "".join(f'  "{a}" -> "{b}";\n' for a, b in include_edges)
        + "}\n",
        encoding="utf-8",
    )


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description=(
            "Extract a lightweight code structure map from a C/C++ codebase: "
            "includes, class/struct declarations, and function definitions."
        )
    )
    ap.add_argument("--root", default=".", help="Project root directory (default: .)")
    ap.add_argument(
        "--path",
        action="append",
        default=[],
        help=(
            "Relative path to scan for symbols (can be repeated). "
            "Default is --path src"
        ),
    )
    ap.add_argument(
        "--out",
        default="code_map.json",
        help="Output JSON path (default: code_map.json)",
    )
    ap.add_argument(
        "--dot",
        default="include_graph.dot",
        help="Output Graphviz DOT for include graph (default: include_graph.dot)",
    )
    ap.add_argument(
        "--include-external-includes",
        action="store_true",
        help="Also keep system/3rd-party includes in JSON (default: only project-local).",
    )
    ap.add_argument(
        "--exclude-dir",
        action="append",
        default=[],
        help="Directory name to exclude (can be repeated).",
    )
    args = ap.parse_args(argv)

    root = Path(args.root)
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    exclude_dirs.update(args.exclude_dir)
    scan_paths = [root / p for p in (args.path if args.path else ["src"])]

    data = build_map(
        root=root,
        scan_paths=scan_paths,
        exclude_dirs=exclude_dirs,
        include_external=bool(args.include_external_includes),
    )

    out_path = Path(args.out)
    out_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    write_dot(data["include_graph"]["edges"], Path(args.dot))

    print(f"Wrote: {out_path}")
    print(f"Wrote: {args.dot}")
    print(f"Files: {len(data['files'])}")
    print(f"Symbols: {len(data['symbols'])}")
    print(f"Include edges (project-local): {len(data['include_graph']['edges'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
