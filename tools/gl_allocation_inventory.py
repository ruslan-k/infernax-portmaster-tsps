"""Fail-closed source census for GL storage, attachment, copy and surface calls.

This is static source evidence only; it does not establish execution, driver
allocation success, physical backing, residency, or allocator refunds.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

SOURCE_DIRS = ("native", "framework")
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hh", ".hpp"}
VALID_STATUSES = {
    "wrapped+measured",
    "wrapped+unmeasured",
    "not reached",
    "unknown",
}

API_RULES = (
    (re.compile(r"gl(?:TexImage|TexStorage|CompressedTexImage|CompressedTexStorage|TextureImage|TextureStorage)\w*"), "texture-storage"),
    (re.compile(r"gl(?:Named)?RenderbufferStorage\w*"), "renderbuffer-storage"),
    (re.compile(r"gl(?:Named)?Buffer(?:Data|Storage)\w*"), "buffer-storage"),
    (re.compile(r"glFramebuffer(?:Texture|Renderbuffer)\w*"), "framebuffer-attachment"),
    (re.compile(r"glCopyTex(?:Sub)?Image\w*"), "texture-copy"),
    (re.compile(r"glEGLImageTargetTexture\w*"), "dma-buf-import"),
    (re.compile(r"eglCreate(?:[A-Za-z0-9_]*Surface|Image)\w*"), "egl-surface"),
    (re.compile(r"gbm_(?:surface_create|bo_create|bo_import)\w*"), "gbm-surface-or-import"),
)
IDENTIFIER_CALL = re.compile(r"(?P<callee>[A-Za-z_]\w*)\s*\(")
FUNCTION_POINTER_CALL = re.compile(r"\(\s*\*\s*(?P<callee>[A-Za-z_]\w*)\s*\)\s*\(")


class InventoryError(ValueError):
    """The source callsites and the reviewed coverage policy do not reconcile."""


def _mask_comments_and_literals(source: str) -> str:
    """Replace comments and quoted literals with spaces, preserving newlines."""
    out = list(source)
    i = 0
    state = "code"
    quote = ""
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = "line"
                continue
            if ch == "/" and nxt == "*":
                out[i] = out[i + 1] = " "
                i += 2
                state = "block"
                continue
            if ch in ("\"", "'"):
                quote = ch
                out[i] = " "
                i += 1
                state = "literal"
                continue
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = "code"
                continue
            if ch != "\n":
                out[i] = " "
            i += 1
            continue
        if state == "literal":
            if ch == "\\":
                if ch != "\n":
                    out[i] = " "
                if i + 1 < len(source):
                    if source[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                else:
                    i += 1
                continue
            if ch == quote:
                out[i] = " "
                i += 1
                state = "code"
                continue
            if ch != "\n":
                out[i] = " "
            i += 1
    return "".join(out)


def _api_for_callee(callee: str) -> tuple[str, str, str] | None:
    for pattern, kind in API_RULES:
        if pattern.fullmatch(callee):
            return callee, kind, "direct"
    # Common function-pointer aliases and dispatch-table members are explicit
    # indirect callsites; they must receive their own inventory entries.
    suffixes = re.split(r"(?:->|\.|_)", callee)
    for suffix in reversed(suffixes):
        for pattern, kind in API_RULES:
            if pattern.fullmatch(suffix):
                return suffix, kind, "indirect"
    return None


def _looks_like_declaration(masked: str, match: re.Match) -> bool:
    """Exclude function prototypes/definitions while retaining dispatch calls."""
    line_start = masked.rfind("\n", 0, match.start()) + 1
    prefix = masked[line_start:match.start()]
    statement_start = max(prefix.rfind(";"), prefix.rfind("{"), prefix.rfind("}"))
    prefix = prefix[statement_start + 1:].strip()
    words = prefix.replace("*", " ").split()
    if any(word in {"extern", "static", "typedef", "inline"} for word in words):
        return True
    return len(words) == 1 and words[0] in {
        "void", "int", "unsigned", "signed", "GLenum", "GLint", "GLuint",
        "GLsizei", "GLboolean", "GLsizeiptr", "EGLBoolean", "EGLSurface", "EGLContext",
    }


def _safe_relative_path(path: str) -> str:
    normalized = PurePosixPath(path.replace("\\", "/"))
    if normalized.is_absolute() or ".." in normalized.parts:
        raise InventoryError(f"source path must be relative and contained: {path!r}")
    return normalized.as_posix()


def scan_text(path: str, source: str) -> list[dict]:
    """Return API calls in source order, ignoring comments and string literals."""
    relpath = _safe_relative_path(path)
    masked = _mask_comments_and_literals(source)
    matches = []
    for pattern in (IDENTIFIER_CALL, FUNCTION_POINTER_CALL):
        for match in pattern.finditer(masked):
            if _looks_like_declaration(masked, match):
                continue
            callee = match.group("callee")
            classified = _api_for_callee(callee)
            if classified is None:
                continue
            api, kind, dispatch = classified
            # Exact GL/EGL names called through a table member are indirect.
            if dispatch == "direct":
                prefix = masked[max(0, match.start() - 3):match.start()].rstrip()
                if prefix.endswith(("->", ".")):
                    dispatch = "indirect"
            matches.append((match.start(), callee, api, kind, dispatch))
    matches.sort(key=lambda row: row[0])
    ordinals: Counter[tuple[str, str]] = Counter()
    rows = []
    for offset, callee, api, kind, dispatch in matches:
        key = (api, dispatch)
        ordinals[key] += 1
        line = masked.count("\n", 0, offset) + 1
        callsite_id = f"{relpath}|{api}|{dispatch}|{callee}|{ordinals[key]}"
        rows.append({
            "id": callsite_id,
            "path": relpath,
            "line": line,
            "callee": callee,
            "api": api,
            "kind": kind,
            "dispatch": dispatch,
        })
    return rows


def scan_source_tree(source_root: str | Path) -> list[dict]:
    """Scan all C/C++ sources in both pinned source roots, including headers."""
    root = Path(source_root).resolve()
    missing = [name for name in SOURCE_DIRS if not (root / name).is_dir()]
    if missing:
        raise InventoryError(f"source root missing directories: {', '.join(missing)}")
    calls = []
    for dirname in SOURCE_DIRS:
        base = root / dirname
        files = sorted(
            p for p in base.rglob("*")
            if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES
            and not any(part in {".git", "__pycache__", "build", "build-output"} for part in p.parts)
        )
        for path in files:
            relative = path.relative_to(root).as_posix()
            calls.extend(scan_text(relative, path.read_text(encoding="utf-8", errors="replace")))
    return sorted(calls, key=lambda row: (row["path"], row["line"], row["api"], row["dispatch"]))


def source_tree_identity(source_root: str | Path) -> dict:
    """Hash every C/C++ input in native and framework using relative names."""
    root = Path(source_root).resolve()
    missing = [name for name in SOURCE_DIRS if not (root / name).is_dir()]
    if missing:
        raise InventoryError(f"source root missing directories: {', '.join(missing)}")
    digest = hashlib.sha256()
    files = []
    for dirname in SOURCE_DIRS:
        base = root / dirname
        files.extend(
            p for p in base.rglob("*")
            if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES
            and not any(part in {".git", "__pycache__", "build", "build-output"} for part in p.parts)
        )
    for path in sorted(files, key=lambda p: p.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return {"file_count": len(files), "sha256": digest.hexdigest()}


def validate_policy(calls: list[dict], policy: dict, source_identity: dict | None = None) -> None:
    """Require exact callsite coverage and an explicit, justified status per site."""
    if policy.get("schema_version") != 1:
        raise InventoryError("unsupported inventory policy schema_version")
    expected_source = policy.get("source_tree_sha256")
    if expected_source:
        if source_identity is None:
            raise InventoryError("source identity required by inventory policy")
        if source_identity.get("sha256") != expected_source:
            raise InventoryError("source tree SHA-256 mismatch")
    entries = policy.get("call_sites")
    if not isinstance(entries, list):
        raise InventoryError("policy call_sites must be a list")
    policy_by_id = {}
    for row in entries:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            raise InventoryError("policy callsite needs a string id")
        callsite_id = row["id"]
        if callsite_id in policy_by_id:
            raise InventoryError(f"duplicate inventory callsite: {callsite_id}")
        if row.get("status") not in VALID_STATUSES:
            raise InventoryError(f"invalid status for {callsite_id}")
        if not isinstance(row.get("rationale"), str) or not row["rationale"].strip():
            raise InventoryError(f"missing rationale for {callsite_id}")
        policy_by_id[callsite_id] = row
    actual_by_id = {row["id"]: row for row in calls}
    new = sorted(set(actual_by_id) - set(policy_by_id))
    stale = sorted(set(policy_by_id) - set(actual_by_id))
    if new:
        row = actual_by_id[new[0]]
        raise InventoryError(
            "unclassified callsite: "
            f"{row['path']}:{row['line']} {row['callee']} ({row['id']})"
        )
    if stale:
        raise InventoryError(f"stale inventory callsite: {stale[0]}")


def render_report(calls: list[dict], policy: dict, source_identity: dict | None = None) -> dict:
    validate_policy(calls, policy, source_identity)
    policy_by_id = {row["id"]: row for row in policy["call_sites"]}
    report_calls = []
    for row in calls:
        reviewed = policy_by_id[row["id"]]
        report_calls.append({**row, "status": reviewed["status"], "rationale": reviewed["rationale"]})
    return {
        "schema_version": 1,
        "scope": "static source census only; execution and physical backing remain unverified",
        "call_site_count": len(report_calls),
        "status_counts": dict(sorted(Counter(row["status"] for row in report_calls).items())),
        "kind_counts": dict(sorted(Counter(row["kind"] for row in report_calls).items())),
        "source_tree": source_identity,
        "call_sites": report_calls,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        calls = scan_source_tree(args.source_root)
        identity = source_tree_identity(args.source_root)
        policy = json.loads(args.policy.read_text(encoding="utf-8"))
        report = render_report(calls, policy, identity)
    except (OSError, json.JSONDecodeError, InventoryError) as exc:
        parser.error(str(exc))
        raise SystemExit(2)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(f"GL_STORAGE_INVENTORY call-sites={report['call_site_count']} "
          f"statuses={json.dumps(report['status_counts'], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
