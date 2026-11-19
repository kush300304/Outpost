#!/usr/bin/env python3
"""@file checkStyle.py
Checks C++ sources against the CLAUDE.md code-style rules that can be measured mechanically.

Rules:
  single-exit      at most one return statement per function or lambda body, and when present
                   it is the last statement of that body (not nested inside a control block)
  file-header      a `/// @file` comment within the first five lines
  raw-new-delete   no `new T` or `delete p` expressions (`= delete` declarations are fine)
  history-comment  comments describe the current implementation, not its history
  line-length      no line longer than the .clang-format ColumnLimit (100)

Usage: scripts/checkStyle.py [file ...]   (default: every project .h/.cpp file)
Exit status is 1 when any violation is found.
"""

import pathlib
import re
import sys

PROJECT_DIRS = ["common", "edge", "router", "storage", "write-api"]
CONTROL_WORDS = {"if", "for", "while", "switch", "catch"}
QUALIFIERS = {"const", "override", "noexcept", "mutable", "final"}
COLUMN_LIMIT = 100
HISTORY_PATTERN = re.compile(
    r"\b(previously|formerly|changed from|used to|no longer|was changed|old version|"
    r"bug ?fix|fixed (?:the|a|an|bug|issue|in))\b",
    re.IGNORECASE,
)


def stripSource(text):
    """Replace comments and string/char literals with spaces; return (code, comments)."""
    code = list(text)
    comments = []
    i = 0
    length = len(text)
    while i < length:
        start = i
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = length if end == -1 else end
            comments.append((text.count("\n", 0, i) + 1, text[i:end]))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = length if end == -1 else end + 2
            comments.append((text.count("\n", 0, i) + 1, text[i:end]))
            i = end
        elif text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            open_paren = text.find("(", i + 2)
            delimiter = text[i + 2:open_paren]
            end = text.find(")" + delimiter + '"', open_paren)
            i = end + len(delimiter) + 2
        elif text[i] in "\"'":
            quote = text[i]
            i += 1
            while i < length and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
        if i - start > 1 or (start < length and text[start] in "\"'"):
            for j in range(start, min(i, length)):
                if code[j] != "\n":
                    code[j] = " "
    return "".join(code), comments


def previousToken(code, end):
    """Return (token, startIndex) of the token ending before index `end`, skipping whitespace."""
    i = end - 1
    while i >= 0 and code[i].isspace():
        i -= 1
    token = ("", -1)
    if i >= 0:
        if code[i].isalnum() or code[i] == "_":
            j = i
            while j >= 0 and (code[j].isalnum() or code[j] == "_"):
                j -= 1
            token = (code[j + 1:i + 1], j + 1)
        else:
            token = (code[i], i)
    return token


def matchingOpenParen(code, closeIndex):
    """Index of the '(' matching the ')' at closeIndex, or -1."""
    depth = 0
    i = closeIndex
    result = -1
    while i >= 0 and result == -1:
        if code[i] == ")":
            depth += 1
        elif code[i] == "(":
            depth -= 1
            if depth == 0:
                result = i
        i -= 1
    return result


def isFunctionBody(code, braceIndex):
    """True when the '{' at braceIndex opens a function or lambda body."""
    token, start = previousToken(code, braceIndex)
    while token in QUALIFIERS:
        token, start = previousToken(code, start)
    result = False
    if token == ")":
        open_paren = matchingOpenParen(code, start)
        word, _ = previousToken(code, open_paren) if open_paren > 0 else ("", -1)
        result = word not in CONTROL_WORDS
    elif token == "]":
        result = True
    return result


def checkSingleExit(path, code):
    """Find functions with more than one return, or a return that is not the final statement."""
    violations = []
    stack = []  # entries: [isFunction, openIndex, returns(list of (index, depthOwner))]
    returns_pattern = re.compile(r"\breturn\b")
    events = [(m.start(), "{") for m in re.finditer(r"\{", code)]
    events += [(m.start(), "}") for m in re.finditer(r"\}", code)]
    events += [(m.start(), "return") for m in returns_pattern.finditer(code)]
    events.sort()
    for index, kind in events:
        if kind == "{":
            stack.append({"function": isFunctionBody(code, index), "open": index, "returns": []})
        elif kind == "return":
            owner = next((frame for frame in reversed(stack) if frame["function"]), None)
            if owner is not None:
                owner["returns"].append((index, stack[-1] is owner))
        elif stack:
            frame = stack.pop()
            if frame["function"]:
                line = code.count("\n", 0, frame["open"]) + 1
                returns = frame["returns"]
                if len(returns) > 1:
                    violations.append((path, line, "single-exit", f"{len(returns)} return statements"))
                elif len(returns) == 1:
                    return_index, at_body_level = returns[0]
                    tail = code[return_index:index]
                    semicolon = findStatementEnd(tail)
                    trailing = tail[semicolon + 1:].strip() if semicolon >= 0 else "?"
                    if not at_body_level or trailing:
                        violations.append((path, line, "single-exit", "return is not the last statement"))
    return violations


def findStatementEnd(fragment):
    """Index of the ';' that ends the statement starting at fragment[0], ignoring nested braces."""
    depth = 0
    result = -1
    i = 0
    while i < len(fragment) and result == -1:
        char = fragment[i]
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == ";" and depth == 0:
            result = i
        i += 1
    return result


def checkFile(path):
    """Return the list of violations for one file."""
    text = path.read_text(encoding="utf-8")
    code, comments = stripSource(text)
    violations = []
    head = "\n".join(text.splitlines()[:5])
    if "@file" not in head:
        violations.append((path, 1, "file-header", "missing /// @file header"))
    for match in re.finditer(r"\bnew\s+[A-Za-z_:(<]", code):
        before, _ = previousToken(code, match.start())
        if before != "operator":
            violations.append((path, code.count("\n", 0, match.start()) + 1, "raw-new-delete", "new expression"))
    for match in re.finditer(r"\bdelete\b", code):
        before, _ = previousToken(code, match.start())
        if before not in ("=", "operator"):
            violations.append((path, code.count("\n", 0, match.start()) + 1, "raw-new-delete", "delete expression"))
    for number, line in enumerate(text.splitlines(), start=1):
        if len(line) > COLUMN_LIMIT:
            violations.append((path, number, "line-length", f"{len(line)} columns"))
    for line, comment in comments:
        if HISTORY_PATTERN.search(comment):
            violations.append((path, line, "history-comment", comment.strip()[:80]))
    violations += checkSingleExit(path, code)
    return violations


def main(argv):
    """Entry point: check the given files (or the whole project) and print violations."""
    root = pathlib.Path(__file__).resolve().parent.parent
    if len(argv) > 1:
        files = [pathlib.Path(arg) for arg in argv[1:] if arg.endswith((".h", ".cpp"))]
    else:
        files = sorted(
            f for d in PROJECT_DIRS if (root / d).exists() for f in (root / d).rglob("*")
            if f.suffix in (".h", ".cpp")
        )
    violations = []
    for path in files:
        violations += checkFile(path)
    for path, line, rule, message in violations:
        print(f"{path}:{line}: {rule}: {message}")
    print(f"checkStyle: {len(files)} files, {len(violations)} violations")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
