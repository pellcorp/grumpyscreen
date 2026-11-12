#!/usr/bin/env python3
"""Require braced control statement bodies in src/ and Android Java sources."""

import argparse
from pathlib import Path
import sys

from tree_sitter import Language, Parser
import tree_sitter_c
import tree_sitter_cpp
import tree_sitter_java


LANGUAGES = {
    "c": Language(tree_sitter_c.language()),
    "cpp": Language(tree_sitter_cpp.language()),
    "java": Language(tree_sitter_java.language()),
}
CPP_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx"}
LOOPS = {
    "for_statement": "for",
    "for_range_loop": "for",
    "enhanced_for_statement": "for",
    "while_statement": "while",
    "do_statement": "do",
}


def source_files(root):
    """Keep vendor dependencies and Android build output outside the check."""
    files = {p for p in (root / "src").rglob("*") if p.suffix in CPP_SUFFIXES}
    for directory in (root / "android/app/src", root / "android/tests"):
        files.update(directory.rglob("*.java"))
    return sorted(files)


def unbraced_bodies(source, language):
    """Yield (keyword, keyword node, body node), including nested statements.

    Syntax trees distinguish comments, strings, preprocessor conditionals,
    constexpr ifs, range loops and do/while tails from statement bodies.
    C/C++ trees may contain recovery nodes for macros or conditional syntax;
    walk their children too, without requiring a configured compiler build.
    """
    tree = Parser(LANGUAGES[language]).parse(source)
    pending = [tree.root_node]
    while pending:
        node = pending.pop()
        pending.extend(reversed(node.named_children))
        bodies = []
        if node.type == "if_statement":
            bodies.append(("if", node, node.child_by_field_name("consequence")))
            alternative = node.child_by_field_name("alternative")
            if alternative is not None:
                if alternative.type == "else_clause":
                    body = next((c for c in alternative.named_children
                                 if c.type != "comment"), None)
                    bodies.append(("else", alternative, body))
                else:
                    keyword = next(c for c in node.children if c.type == "else")
                    bodies.append(("else", keyword, alternative))
        elif node.type in LOOPS:
            bodies.append((LOOPS[node.type], node, node.child_by_field_name("body")))

        for keyword, control, body in bodies:
            if body is None:
                continue
            if body.type == "attributed_statement":
                body = body.named_children[-1]
            if body.type in {"compound_statement", "block"}:
                continue
            # Keep conventional else-if chains; the nested if is checked itself.
            if keyword == "else" and body.type == "if_statement":
                continue
            yield keyword, control, body


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (defaults to this checkout)")
    args = parser.parse_args(argv)
    files = source_files(args.root)
    if not files:
        parser.error("no C/C++ or Android Java source files found")
    count = 0
    for path in files:
        language = "java" if path.suffix == ".java" else "c" if path.suffix == ".c" else "cpp"
        for keyword, node, _ in unbraced_bodies(path.read_bytes(), language):
            row, column = node.start_point
            print(f"{path.relative_to(args.root)}:{row + 1}:{column + 1}: "
                  f"error: {keyword} body must use braces")
            count += 1
    if count:
        print(f"Found {count} unbraced control statement bodies.", file=sys.stderr)
        return 1
    print(f"Braces check passed ({len(files)} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
