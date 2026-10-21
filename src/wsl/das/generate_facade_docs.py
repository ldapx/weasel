#!/usr/bin/env python3
"""Generate the gameplay facade API reference from the daslang source.

The authoritative surface is `src/wsl/das/modules/weasel.das` (gameplay,
`require weasel`) plus `src/wsl/das/modules/weasel_editor.das` (editor-only,
`require weasel_editor`). Hand-maintaining a separate reference invariably
drifts, so this script parses those files and emits Sphinx `.rst` using the
das: domain directives.

Only the public surface is emitted: top-level `struct`/`class`/`enum`/`def`
(and struct methods/fields) whose names do not start with `_`, excluding
`private` classes and the query-macro machinery (covered by a curated
Queries section instead). Preceding `//` comment blocks become the entry
description.

Usage:
    python3 generate_facade_docs.py --source <weasel.das> --rst <weasel.rst>
                                     [--module-name weasel]
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Member:
    kind: str  # 'field' | 'method' | 'operator'
    signature: str  # clean signature without `def`
    name: str
    doc: str = ""


@dataclass
class Entry:
    kind: str  # 'struct' | 'class' | 'enum' | 'function' | 'operator' | 'alias'
    name: str
    signature: str  # clean signature without `def`
    doc: str = ""
    members: list = field(default_factory=list)
    base: str = ""


def _strip_line_comment(line):
    """Remove a trailing // comment, respecting string literals."""
    in_str = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\" and i + 1 < len(line):
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
        else:
            if c == '"':
                in_str = True
                i += 1
            elif c == "/" and i + 1 < len(line) and line[i + 1] == "/":
                return line[:i]
            else:
                i += 1
    return line


def _count_braces(line):
    """Net { minus } count, ignoring strings and // comments."""
    code = _strip_line_comment(line)
    code = re.sub(r'"(?:[^"\\]|\\.)*"', '""', code)
    return code.count("{") - code.count("}")


TOP_RE = re.compile(
    r"^(?:public\s+|private\s+)?(struct|class|enum|typedef|def)\b\s*(.*)$"
)
FIELD_RE = re.compile(r"^([A-Za-z_][\w]*)\s*:\s*(.+?)\s*(?:=\s*(.+))?$")
ENUM_VARIANT_RE = re.compile(r"^([A-Za-z_][\w]*)\s*,?\s*$")


def _is_private(rest, full_line):
    s = full_line.lstrip()
    if s.startswith("private "):
        return True
    if rest.startswith("private "):
        return True
    m = re.match(r"^([A-Za-z_][\w]*)", rest)
    if m and m.group(1).startswith("_"):
        return True
    if m and m.group(1) in ("QueryMacro",):
        return True
    return False


def _clean_def_signature(sig):
    """Turn `def [override] name(args) : ret` into `name(args) : ret`."""
    body = sig.strip()
    assert body.startswith("def "), sig
    body = body[len("def ") :].strip()
    if body.startswith("override "):
        body = body[len("override ") :].strip()
    return body


def _def_name(clean_sig):
    """Extract the short name from a cleaned def signature."""
    if clean_sig.startswith("operator"):
        m = re.match(r"(operator\s*\S+)", clean_sig)
        return m.group(1).strip() if m else clean_sig
    m = re.match(r"([A-Za-z_][\w]*)", clean_sig)
    return m.group(1) if m else clean_sig


def _skip_balanced(lines, i, depth):
    """Advance past lines until `depth` returns to zero. Returns new (i, depth)."""
    n = len(lines)
    while i < n and depth > 0:
        depth += _count_braces(lines[i])
        i += 1
    return i, depth


def parse_facade(path):
    """Parse a .das facade module into public entries."""
    lines = Path(path).read_text(encoding="utf-8").split("\n")
    entries = []
    pending_doc = []
    i = 0
    n = len(lines)

    def take_doc():
        doc = "\n".join(pending_doc).strip()
        pending_doc.clear()
        return doc

    while i < n:
        line = lines[i]
        s = line.strip()

        if s.startswith("//"):
            pending_doc.append(s[2:].strip())
            i += 1
            continue
        if s == "":
            pending_doc.clear()
            i += 1
            continue
        if (
            s.startswith("[")
            or s.startswith("options ")
            or s.startswith("module ")
            or s.startswith("require ")
        ):
            pending_doc.clear()
            i += 1
            continue

        m = TOP_RE.match(s)
        if m is None:
            pending_doc.clear()
            i += 1
            continue

        kind_word, rest = m.group(1), m.group(2).strip()
        if _is_private(rest, s):
            take_doc()
            if kind_word == "typedef":
                i += 1
            else:
                # skip signature lines until the opening brace, then the body
                depth = _count_braces(line)
                i += 1
                while depth == 0 and i < n and "{" not in lines[i]:
                    depth += _count_braces(lines[i])
                    i += 1
                i, _depth = _skip_balanced(lines, i, depth)
            continue

        doc = take_doc()

        if kind_word == "typedef":
            am = re.match(r"(?:public\s+)?([A-Za-z_][\w]*)\s*=\s*(.+)$", rest)
            if am:
                entries.append(
                    Entry(kind="alias", name=am.group(1), signature=s.rstrip(), doc=doc)
                )
            i += 1
            continue

        if kind_word == "def":
            sig_lines = [s]
            depth = _count_braces(line)
            j = i + 1
            # A single-line `def f() : void { }` is already complete.
            while depth == 0 and j < n and "{" not in sig_lines[-1]:
                sig_lines.append(lines[j].strip())
                depth += _count_braces(lines[j])
                j += 1
                if "{" in lines[j - 1]:
                    break
            full = " ".join(sig_lines)
            raw_sig = full[: full.find("{")].strip()
            clean = _clean_def_signature(raw_sig)
            name = _def_name(clean)
            is_op = clean.startswith("operator")
            entries.append(
                Entry(
                    kind="operator" if is_op else "function",
                    name=name,
                    signature=clean,
                    doc=doc,
                )
            )
            i, _d = _skip_balanced(lines, j, depth)
            continue

        # struct / class / enum: accumulate header until '{', then members
        sig_lines = [s]
        depth = _count_braces(line)
        j = i + 1
        while depth == 0 and j < n and "{" not in lines[j - 1]:
            sig_lines.append(lines[j].strip())
            depth += _count_braces(lines[j])
            j += 1
        full = " ".join(sig_lines)
        header = full[: full.find("{")].strip()
        hm = re.match(
            r"(?:struct|class|enum)\s+([A-Za-z_][\w]*)\s*(?::\s*(.+))?$", header
        )
        tname = hm.group(1) if hm else rest
        base = (hm.group(2) or "").strip() if hm else ""
        entry = Entry(kind=kind_word, name=tname, signature=header, doc=doc, base=base)
        entries.append(entry)

        i = j
        member_doc = []
        while i < n and depth > 0:
            mline = lines[i]
            ms = mline.strip()
            if ms.startswith("//"):
                member_doc.append(ms[2:].strip())
                i += 1
                continue
            if ms == "":
                member_doc.clear()
                i += 1
                continue
            if (
                ms.startswith("[")
                or ms.startswith("options ")
                or ms.startswith("module ")
                or ms.startswith("require ")
            ):
                member_doc.clear()
                i += 1
                continue
            if ms.startswith("def ") or ms.startswith("private def "):
                if ms.startswith("private def "):
                    member_doc.clear()
                    mdepth = _count_braces(mline)
                    i += 1
                    while mdepth == 0 and i < n and "{" not in mline:
                        mdepth += _count_braces(lines[i])
                        i += 1
                        if "{" in lines[i - 1]:
                            break
                    i, _d = _skip_balanced(lines, i, mdepth)
                    continue
                mdoc = "\n".join(member_doc).strip()
                member_doc.clear()
                m_sig_lines = [ms]
                mdepth = _count_braces(mline)
                depth += mdepth
                i += 1
                while mdepth == 0 and i < n and "{" not in m_sig_lines[-1]:
                    m_sig_lines.append(lines[i].strip())
                    d = _count_braces(lines[i])
                    mdepth += d
                    depth += d
                    i += 1
                    if "{" in lines[i - 1]:
                        break
                full_m = " ".join(m_sig_lines)
                raw_m = full_m[: full_m.find("{")].strip()
                clean_m = _clean_def_signature(raw_m)
                mname = _def_name(clean_m)
                if not mname.startswith("_"):
                    entry.members.append(
                        Member(
                            kind="operator"
                            if clean_m.startswith("operator")
                            else "method",
                            signature=clean_m,
                            name=mname,
                            doc=mdoc,
                        )
                    )
                # Skip the method body, tracking the method-relative depth
                # and the enclosing struct depth in parallel.
                while i < n and mdepth > 0:
                    d = _count_braces(lines[i])
                    mdepth += d
                    depth += d
                    i += 1
                continue
            fm = FIELD_RE.match(ms)
            if fm:
                mdoc = "\n".join(member_doc).strip()
                member_doc.clear()
                fname = fm.group(1)
                if not fname.startswith("_"):
                    fsig = ms.rstrip()
                    if fsig.endswith("}"):
                        fsig = fsig[:-1].rstrip()
                    entry.members.append(
                        Member(kind="field", signature=fsig, name=fname, doc=mdoc)
                    )
                depth += _count_braces(mline)
                i += 1
                continue
            # enum variant or macro internals
            if kind_word == "enum":
                vm = ENUM_VARIANT_RE.match(ms.rstrip("}").strip())
                if vm and not vm.group(1).startswith("_"):
                    entry.members.append(
                        Member(
                            kind="field",
                            signature=vm.group(1),
                            name=vm.group(1),
                            doc="\n".join(member_doc).strip(),
                        )
                    )
                member_doc.clear()
            else:
                member_doc.clear()
            depth += _count_braces(mline)
            i += 1
        continue

    return entries


# -- Section grouping (curated) -----------------------------------------------


def _matches(name, parent, pat):
    if pat.startswith("re:"):
        return re.search(pat[3:], name) is not None
    if pat.startswith("*") and pat.endswith("*") and len(pat) > 2:
        return pat[1:-1] in name
    if pat.endswith("*"):
        return name.startswith(pat[:-1])
    if pat.startswith("*"):
        return name.endswith(pat[1:])
    if "/" in pat:
        p, q = pat.split("/", 1)
        return (p == "*" or p == parent) and (q == "*" or q == name)
    return name == pat


# (title, patterns). Entries match by free-function name or `Parent/member`.
# First match wins. Order here is the document order.
SECTIONS = [
    (
        "Lifecycle",
        [
            "GameSystem",
            "EcsSystem",
            "SystemContext",
            "ScriptContext",
            "current_world",
        ],
    ),
    (
        "Core handles",
        [
            "Time",
            "Entity",
            "World",
            "CommandBuffer",
            "Subscription",
            "SensorOverlap",
            "DestroyMode",
            "Camera",
            "re:^operator",
            "is_null",
            "entity_from_id",
        ],
    ),
    (
        "Time",
        [
            "time_paused",
            "set_time_paused",
            "get_time_scale",
            "set_time_scale",
        ],
    ),
    (
        "Entities",
        [
            "create_entity",
            "destroy_entity",
            "destroy",
            "destroy_entity_only",
            "clone_entity",
            "is_alive",
            "find_entity",
            "find_all_by_name",
            "set_name",
            "entity_count",
        ],
    ),
    (
        "Components",
        [
            "has",
            "add",
            "emplace",
            "remove",
            "get",
            "get_or_default",
            "try_get",
            "copy_component",
            "*singleton*",
        ],
    ),
    ("Hierarchy", ["set_parent", "parent_of", "children_of"]),
    ("Commands", ["defer_*"]),
    (
        "Events",
        [
            "subscribe_event",
            "unsubscribe_event",
            "connect_area_entered",
            "connect_area_exited",
            "subscribe_area_entered",
            "subscribe_area_exited",
            "sensor_area",
            "sensor_other",
        ],
    ),
    ("Messages", ["message_at"]),
    (
        "Systems",
        [
            "enable_system",
            "disable_system",
            "system_enabled",
            "set_stage",
            "depends_on",
            "conflicts_with",
        ],
    ),
    ("Transform", ["move", "set_position", "move_2d", "set_position_2d"]),
    ("Physics", ["apply_force", "apply_impulse", "move_character"]),
    ("Animation", ["*animation*"]),
    ("Audio", ["*audio*"]),
    (
        "Camera and picking",
        [
            "active_camera",
            "set_active_camera",
            "camera_*",
            "set_camera_*",
            "pick_ray",
            "ray_plane_hit",
            "window_size",
        ],
    ),
    (
        "Models, prefabs, scenes",
        [
            "set_model*",
            "instantiate_prefab",
            "change_scene",
        ],
    ),
    ("Services", ["Input", "Logger", "LogFields", "UiService", "format_log_message"]),
]

QUERIES_TEXT = """Queries
-------

Bulk work uses the ``query`` macro. The block parameters declare the
required components (each must be a ``&`` reference); any arguments
between the World and the block are typed exclusion tags. The macro
drives the smallest component pool and binds live references; structural
changes inside the block are rejected, so queue them through
``ctx.commands``.

.. code-block:: das

   // every entity with a Transform, mutating it in place
   query(ctx.world) $(t : Transform&) {
       t.position.y += 1.0 * dt
   }

   // required components only, no explicit world
   query() $(t : Transform&; v : Velocity&) {
       t.position += v.linear * dt
   }

   // every Transform holder that has no Sprite2D and no Audio
   query(ctx.world, Sprite2D(), Audio()) $(t : Transform&) {
       t.position.y += 1.0 * dt
   }

``query`` requires every listed component parameter to be a ``&``
reference. The exclusion tags are typed, so no raw component id reaches
gameplay code; omit them (or pass no world) to disable filtering.
Component type ids are resolved once per query call, never per entity.
There are no cached query plans yet; each call resolves the ids and
builds the view.
"""


def section_for(entry, parent=""):
    for title, patterns in SECTIONS:
        for pat in patterns:
            if _matches(entry.name, parent, pat):
                return title
    return "Miscellaneous"


def _write_doc(out, doc, indent):
    if not doc:
        return
    for ln in doc.split("\n"):
        out.write(f"{indent}{ln}\n" if ln else "\n")
    out.write("\n")


def emit_rst(entries, out, module_name):
    out.write(f"{module_name}\n")
    out.write("=" * len(module_name) + "\n\n")
    out.write(f".. das:module:: {module_name}\n\n")
    out.write("Gameplay scripts use one import (``require weasel``). Native\n")
    out.write("bindings and legacy proxy helpers are implementation details\n")
    out.write("of this module.\n\n")
    out.write("This reference is generated from the facade source\n")
    out.write(f"(``src/wsl/das/modules/{module_name}.das``). Nothing here is\n")
    out.write("hand-written; edit the ``//`` doc comments in the facade\n")
    out.write("and regenerate.\n\n")

    buckets = {title: [] for title, _ in SECTIONS}
    buckets["Miscellaneous"] = []
    buckets["Queries"] = []
    for e in entries:
        if e.kind in ("struct", "class", "enum", "alias"):
            buckets[section_for(e)].append(e)
        else:
            buckets[section_for(e)].append(e)

    for title, _ in SECTIONS + [("Miscellaneous", []), ("Queries", [])]:
        items = buckets[title]
        if title == "Queries":
            out.write(QUERIES_TEXT + "\n")
            continue
        if not items:
            continue
        out.write(f"{title}\n")
        out.write("-" * len(title) + "\n\n")
        types = [e for e in items if e.kind in ("struct", "class", "enum", "alias")]
        funcs = [e for e in items if e.kind in ("function", "operator")]
        for e in types:
            directive = "data" if e.kind == "alias" else "class"
            out.write(f".. das:{directive}:: {e.name}\n\n")
            if e.base:
                out.write(f"   Base: ``{e.base}``\n\n")
            _write_doc(out, e.doc, "   ")
            for m in e.members:
                if m.kind == "field":
                    out.write(f"   .. das:attribute:: {m.signature}\n\n")
                elif m.kind == "operator":
                    out.write(f"   .. das:operator:: {m.signature}\n\n")
                else:
                    out.write(f"   .. das:method:: {m.signature}\n\n")
                _write_doc(out, m.doc, "      ")
        for e in funcs:
            directive = "operator" if e.kind == "operator" else "function"
            out.write(f".. das:{directive}:: {e.signature}\n\n")
            _write_doc(out, e.doc, "   ")

    out.write("Availability notes\n")
    out.write("------------------\n\n")
    out.write(
        "- Animation functions return ``false`` (or ``-1`` for time\n"
        "  queries) when the entity lacks an ``Animator``.\n"
        "- Audio functions return ``false`` when the entity lacks an\n"
        "  ``Audio`` component.\n"
        "- ``message_at`` returns ``default<T>`` for unknown types or\n"
        "  out-of-range indices.\n"
        "- Raw ``event_emit`` / ``event_post`` / ``event_connect`` /\n"
        "  ``message_post`` / ``message_count`` / ``for_each_message``\n"
        "  natives live in ``weasel_api`` (see :doc:`weasel_api`) and\n"
        "  are used directly; the facade adds typed wrappers where a\n"
        "  copy or handle is needed.\n"
    )


EDITOR_NOTES = """Editor-only helpers
---------------------

These read editor UI state through the editor context and are no-ops
outside an editor session (headless or standalone runs report zeros).
Gameplay scripts must never require this module.
"""


def emit_editor_rst(entries, out, module_name="weasel_editor"):
    out.write(f"{module_name}\n")
    out.write("=" * len(module_name) + "\n\n")
    out.write(f".. das:module:: {module_name}\n\n")
    out.write(EDITOR_NOTES + "\n")
    for e in entries:
        if e.kind in ("struct", "class", "enum", "alias"):
            directive = "data" if e.kind == "alias" else "class"
            out.write(f".. das:{directive}:: {e.name}\n\n")
            _write_doc(out, e.doc, "   ")
            for m in e.members:
                if m.kind == "field":
                    out.write(f"   .. das:attribute:: {m.signature}\n\n")
                elif m.kind == "operator":
                    out.write(f"   .. das:operator:: {m.signature}\n\n")
                else:
                    out.write(f"   .. das:method:: {m.signature}\n\n")
                _write_doc(out, m.doc, "      ")
        else:
            directive = "operator" if e.kind == "operator" else "function"
            out.write(f".. das:{directive}:: {e.signature}\n\n")
            _write_doc(out, e.doc, "   ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, help="path to a facade .das module")
    ap.add_argument("--rst", required=True, help="output .rst path")
    ap.add_argument("--module-name", default=None, help="override the das:module name")
    args = ap.parse_args()

    entries = parse_facade(args.source)
    if not entries:
        sys.stderr.write("error: no public entries parsed\n")
        sys.exit(1)

    mod = args.module_name
    if mod is None:
        mod = Path(args.source).stem

    with open(args.rst, "w", encoding="utf-8") as fh:
        if mod == "weasel_editor":
            emit_editor_rst(entries, fh, mod)
        else:
            emit_rst(entries, fh, mod)

    print(f"wrote {len(entries)} facade entries to {args.rst}")


if __name__ == "__main__":
    main()
