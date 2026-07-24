#!/usr/bin/env python3
import os
import re
import subprocess
import tempfile
from pathlib import Path

TUTORIALS_DIR = Path(__file__).resolve().parent.parent / "docs" / "tutorials"

def extract_cpp_blocks(markdown_text):
    """Extracts all C++ code blocks from a markdown string."""
    pattern = re.compile(r'```cpp\n(.*?)\n```', re.DOTALL)
    return pattern.findall(markdown_text)

def validate_cpp_code(code, index, file_name):
    """Compiles the C++ snippet syntax without linking."""
    
    # We inject a dummy main if not present so it compiles as a standalone unit
    if "int main" not in code and "void main" not in code:
        compilable_code = code + "\nint main() { return 0; }\n"
    else:
        compilable_code = code

    with tempfile.NamedTemporaryFile(suffix=".cpp", delete=False) as tmp:
        tmp.write(compilable_code.encode('utf-8'))
        tmp_path = tmp.name

    # Compile with -fsyntax-only to check syntax without requiring object files or linking
    # Add include paths for SlabFlux if necessary: -I../../include
    cmd = [
        "g++",
        "-std=c++20",
        "-fsyntax-only",
        "-I" + str(TUTORIALS_DIR.parent.parent / "include"),
        tmp_path
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    os.remove(tmp_path)

    if result.returncode == 0:
        print(f"[OK] {file_name} - Snippet {index}")
        return True
    else:
        print(f"[FAILED] {file_name} - Snippet {index}")
        print("--- Compiler Output ---")
        print(result.stderr)
        print("-----------------------")
        return False

def main():
    print("Starting SlabFlux Tutorial Code Validation...\n")
    all_passed = True
    
    for md_file in sorted(TUTORIALS_DIR.glob("*.md")):
        content = md_file.read_text()
        snippets = extract_cpp_blocks(content)
        for i, snippet in enumerate(snippets, start=1):
            if not validate_cpp_code(snippet, i, md_file.name):
                all_passed = False

    if not all_passed:
        exit(1)

if __name__ == "__main__":
    main()
