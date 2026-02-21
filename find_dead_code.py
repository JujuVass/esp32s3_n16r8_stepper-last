#!/usr/bin/env python3
"""
Dead Code Detector for ESP32 PlatformIO C++ projects.
Scans .h/.cpp/.ino files for symbols (functions, variables, classes, #defines)
and reports those that appear to be unused (defined but never referenced elsewhere).

Usage: python find_dead_code.py [project_root]
"""

import os
import re
import sys
from collections import defaultdict
from pathlib import Path

# ============================================================================
# CONFIG
# ============================================================================
SCAN_DIRS = ["src", "include"]
EXTENSIONS = {".h", ".cpp", ".ino", ".c"}
SKIP_DIRS = {".pio", ".git", "node_modules", "lib", "test", "data"}

# Symbols that are always "used" by the framework (entry points, ISR, etc.)
FRAMEWORK_SYMBOLS = {
    "setup", "loop",                          # Arduino entry points
    "motorTask", "networkTask",               # FreeRTOS tasks (passed as function pointers)
    "sendStatus", "stopMovement",             # extern callbacks (GlobalState.h)
    "setRgbLed",                              # Called from multiple places
    "IRAM_ATTR",                              # Attribute, not a symbol
}

# ============================================================================
# REGEX PATTERNS
# ============================================================================

# Function definitions:  ReturnType ClassName::funcName(  OR  ReturnType funcName(
RE_FUNC_DEF = re.compile(
    r'^\s*(?:static\s+)?(?:inline\s+)?(?:virtual\s+)?(?:const\s+)?'
    r'(?:[\w:<>*&]+\s+)+?'          # matches return type tokens
    r'((?:\w+::)?\w+)'              # capture: optional Class:: + funcName
    r'\s*\([^;]*\)\s*(?:const\s*)?(?:override\s*)?\{'  # ( params ) {
    , re.MULTILINE
)

# Function declarations in headers:  ReturnType funcName( ... );
RE_FUNC_DECL = re.compile(
    r'^\s*(?:static\s+)?(?:inline\s+)?(?:virtual\s+)?(?:const\s+)?'
    r'(?:[\w:<>*&]+\s+)+?'
    r'(\w+)'
    r'\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*0\s*)?;',
    re.MULTILINE
)

# #define MACRO_NAME
RE_DEFINE = re.compile(r'^\s*#define\s+(\w+)', re.MULTILINE)

# Class/struct definitions
RE_CLASS = re.compile(r'^\s*(?:class|struct)\s+(\w+)\s*[:{]', re.MULTILINE)

# Global/namespace variable definitions (simplified)
RE_GLOBAL_VAR = re.compile(
    r'^(?:extern\s+)?(?:volatile\s+)?(?:static\s+)?(?:constexpr\s+)?(?:constinit\s+)?'
    r'(?:const\s+)?'
    r'(?:unsigned\s+)?(?:long\s+)?'
    r'(?:[\w:<>*&]+)\s+'
    r'(\w+)\s*(?:=|;|\[)',
    re.MULTILINE
)

# Enum values
RE_ENUM_VALUE = re.compile(r'^\s*(\w+)\s*(?:=\s*[^,}]+)?\s*[,}]', re.MULTILINE)

# #include directives
RE_INCLUDE = re.compile(r'^\s*#include\s+[<"]([^>"]+)[>"]', re.MULTILINE)


def collect_files(root: Path) -> list[Path]:
    """Collect all source files to scan."""
    files = []
    for scan_dir in SCAN_DIRS:
        dir_path = root / scan_dir
        if not dir_path.exists():
            continue
        for f in dir_path.rglob("*"):
            if f.suffix in EXTENSIONS and not any(skip in f.parts for skip in SKIP_DIRS):
                files.append(f)
    # Also scan root-level .ino/.cpp
    for f in root.glob("*.ino"):
        files.append(f)
    for f in root.glob("*.cpp"):
        files.append(f)
    return sorted(set(files))


def strip_comments(content: str) -> str:
    """Remove C/C++ comments (both // and /* */)."""
    # Remove block comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # Remove line comments
    content = re.sub(r'//[^\n]*', '', content)
    return content


def strip_strings(content: str) -> str:
    """Remove string literals to avoid false matches."""
    content = re.sub(r'"(?:[^"\\]|\\.)*"', '""', content)
    return content


def _extract_functions(clean: str, filepath: Path) -> dict:
    """Extract function definitions from cleaned source."""
    symbols = {}
    for m in RE_FUNC_DEF.finditer(clean):
        name = m.group(1)
        base_name = name.split("::")[-1] if "::" in name else name
        if base_name not in FRAMEWORK_SYMBOLS and not base_name.startswith("_"):
            line_no = clean[:m.start()].count('\n') + 1
            symbols[base_name] = {"type": "function", "file": str(filepath), "line": line_no, "full_name": name}
    return symbols


def _extract_defines(clean: str, filepath: Path) -> dict:
    """Extract #define macros from cleaned source."""
    symbols = {}
    guard_pattern = filepath.stem.upper().replace(".", "_") + "_H"
    for m in RE_DEFINE.finditer(clean):
        name = m.group(1)
        if name not in FRAMEWORK_SYMBOLS and not name.startswith("_"):
            if name == guard_pattern or name.endswith("_H") and len(name) < 30:
                continue
            line_no = clean[:m.start()].count('\n') + 1
            symbols[name] = {"type": "define", "file": str(filepath), "line": line_no, "full_name": name}
    return symbols


def _extract_classes(clean: str, filepath: Path) -> dict:
    """Extract class/struct definitions from cleaned source."""
    symbols = {}
    for m in RE_CLASS.finditer(clean):
        name = m.group(1)
        if not name.startswith("_"):
            line_no = clean[:m.start()].count('\n') + 1
            symbols[name] = {"type": "class", "file": str(filepath), "line": line_no, "full_name": name}
    return symbols


def extract_symbols(filepath: Path, content: str) -> dict:
    """Extract all defined symbols from a file."""
    clean = strip_strings(strip_comments(content))
    symbols = {}
    symbols.update(_extract_functions(clean, filepath))
    symbols.update(_extract_defines(clean, filepath))
    symbols.update(_extract_classes(clean, filepath))
    return symbols


def find_unused_includes(filepath: Path, content: str, _all_files: list[Path]) -> list[dict]:
    """Find #include directives for project headers that might be unused.
    
    Note: filepath and _all_files reserved for future full-preprocessor analysis.
    """
    issues = []
    clean = strip_comments(content)
    
    for m in RE_INCLUDE.finditer(clean):
        included = m.group(1)
        if included.startswith("core/") or included.startswith("hardware/") or \
           included.startswith("communication/") or included.startswith("movement/"):
            pass  # Too many false positives without a real preprocessor
    
    return issues


def count_references(symbol_name: str, all_contents: dict[str, str], defining_file: str) -> list[str]:
    """Count how many files reference a symbol (excluding its definition file for certain checks)."""
    referencing_files = []
    # Use word boundary to avoid partial matches
    pattern = re.compile(r'\b' + re.escape(symbol_name) + r'\b')
    
    for filepath, content in all_contents.items():
        matches = list(pattern.finditer(content))
        if filepath == defining_file:
            # In the defining file, need more than just the definition itself
            if len(matches) > 1:
                referencing_files.append(filepath)
        else:
            if matches:
                referencing_files.append(filepath)
    
    return referencing_files


def analyze_header_includes(all_files: list[Path], all_contents: dict[str, str], root: Path) -> list[dict]:
    """Check for includes that might not be needed."""
    issues = []
    
    for filepath in all_files:
        content = all_contents[str(filepath)]
        clean = strip_comments(content)
        
        for m in RE_INCLUDE.finditer(clean):
            included = m.group(1)
            # Only check project-local includes
            if not any(included.startswith(p) for p in ["core/", "hardware/", "communication/", "movement/"]):
                continue
            
            # Find the included header
            header_path = None
            for search_dir in ["include", "src"]:
                candidate = root / search_dir / included
                if candidate.exists():
                    header_path = candidate
                    break
            
            if not header_path or str(header_path) not in all_contents:
                continue
            
            # Extract symbols defined in the header
            header_content = strip_strings(strip_comments(all_contents[str(header_path)]))
            header_symbols = set()
            
            for func_m in RE_FUNC_DECL.finditer(header_content):
                header_symbols.add(func_m.group(1))
            for func_m in RE_FUNC_DEF.finditer(header_content):
                name = func_m.group(1).split("::")[-1]
                header_symbols.add(name)
            for cls_m in RE_CLASS.finditer(header_content):
                header_symbols.add(cls_m.group(1))
            for def_m in RE_DEFINE.finditer(header_content):
                header_symbols.add(def_m.group(1))
            for var_m in RE_GLOBAL_VAR.finditer(header_content):
                header_symbols.add(var_m.group(1))
            
            # Remove noise
            header_symbols -= {"if", "else", "for", "while", "return", "include",
                             "define", "endif", "ifdef", "ifndef", "pragma"}
            
            # Check if ANY symbol from that header is used in this .cpp file
            file_clean = strip_strings(strip_comments(content))
            used_any = False
            for sym in header_symbols:
                if len(sym) < 3:
                    continue
                if re.search(r'\b' + re.escape(sym) + r'\b', file_clean):
                    used_any = True
                    break
            
            if not used_any and header_symbols:
                line_no = clean[:m.start()].count('\n') + 1
                rel_path = filepath.relative_to(root)
                issues.append({
                    "file": str(rel_path),
                    "line": line_no,
                    "include": included,
                    "header_symbols": sorted(header_symbols)[:5],  # show first 5 for context
                })
    
    return issues


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    root = root.resolve()
    
    print(f"🔍 Dead Code Detector — scanning {root}")
    print(f"{'='*70}")
    
    # Collect files
    files = collect_files(root)
    print(f"📁 Found {len(files)} source files\n")
    
    # Read all contents (stripped of comments for reference counting)
    all_contents_raw = {}  # raw (for symbol extraction context)
    all_contents_clean = {}  # cleaned (for reference counting)
    
    for f in files:
        try:
            raw = f.read_text(encoding='utf-8', errors='replace')
            all_contents_raw[str(f)] = raw
            all_contents_clean[str(f)] = strip_strings(strip_comments(raw))
        except Exception as e:
            print(f"  ⚠️ Could not read {f}: {e}")
    
    # Also read JS files for cross-language references (API routes, etc.)
    js_contents = ""
    js_dir = root / "data" / "js"
    if js_dir.exists():
        for jsf in js_dir.rglob("*.js"):
            try:
                js_contents += jsf.read_text(encoding='utf-8', errors='replace') + "\n"
            except Exception:
                pass
    
    # Extract symbols from all files
    all_symbols = {}
    for f in files:
        syms = extract_symbols(f, all_contents_raw[str(f)])
        for name, info in syms.items():
            # Keep the first definition (header wins for extern declarations)
            if name not in all_symbols:
                all_symbols[name] = info
    
    print(f"🔎 Extracted {len(all_symbols)} symbols\n")
    
    # ========================================================================
    # 1. UNUSED SYMBOLS (defined but never referenced elsewhere)
    # ========================================================================
    print("=" * 70)
    print("1. POTENTIALLY UNUSED SYMBOLS")
    print("   (Defined but referenced in 0 other locations)")
    print("=" * 70)
    
    unused = []
    for name, info in sorted(all_symbols.items()):
        refs = count_references(name, all_contents_clean, info["file"])
        
        # Also check JS content
        js_ref = bool(re.search(r'\b' + re.escape(name) + r'\b', js_contents)) if js_contents else False
        
        if not refs and not js_ref:
            unused.append((name, info))
    
    if unused:
        # Group by file
        by_file = defaultdict(list)
        for name, info in unused:
            rel = Path(info["file"]).relative_to(root)
            by_file[str(rel)].append((name, info))
        
        for fpath in sorted(by_file.keys()):
            print(f"\n  📄 {fpath}")
            for name, info in sorted(by_file[fpath], key=lambda x: x[1]["line"]):
                print(f"     L{info['line']:>4}  [{info['type']:>8}]  {info['full_name']}")
    else:
        print("  ✅ No unused symbols detected!")
    
    # ========================================================================
    # 2. UNUSED #define MACROS
    # ========================================================================
    print(f"\n{'='*70}")
    print("2. POTENTIALLY UNUSED #define MACROS")
    print("=" * 70)
    
    unused_defines = []
    for name, info in sorted(all_symbols.items()):
        if info["type"] != "define":
            continue
        refs = count_references(name, all_contents_clean, info["file"])
        js_ref = bool(re.search(r'\b' + re.escape(name) + r'\b', js_contents)) if js_contents else False
        
        if not refs and not js_ref:
            unused_defines.append((name, info))
    
    if unused_defines:
        for name, info in sorted(unused_defines, key=lambda x: x[1]["file"]):
            rel = Path(info["file"]).relative_to(root)
            print(f"  {rel}:{info['line']}  {name}")
    else:
        print("  ✅ No unused macros detected!")
    
    # ========================================================================
    # 3. POTENTIALLY UNNECESSARY #include DIRECTIVES
    # ========================================================================
    print(f"\n{'='*70}")
    print("3. POTENTIALLY UNNECESSARY #include (project headers)")
    print("   (No symbols from included header appear to be used)")
    print("=" * 70)
    
    include_issues = analyze_header_includes(files, all_contents_raw, root)
    if include_issues:
        for issue in sorted(include_issues, key=lambda x: x["file"]):
            print(f"  {issue['file']}:{issue['line']}  #include \"{issue['include']}\"")
            print(f"     Header exports: {', '.join(issue['header_symbols'])}...")
    else:
        print("  ✅ All includes appear to be used!")
    
    # ========================================================================
    # 4. DUPLICATE EXTERN DECLARATIONS
    # ========================================================================
    print(f"\n{'='*70}")
    print("4. EXTERN DECLARATIONS — checking for orphans")
    print("=" * 70)
    
    extern_pattern = re.compile(r'^\s*extern\s+(?:volatile\s+)?(?:[\w:*&<>]+\s+)+(\w+)\s*;', re.MULTILINE)
    
    for f in files:
        if not str(f).endswith('.h'):
            continue
        content = all_contents_clean[str(f)]
        for m in extern_pattern.finditer(content):
            name = m.group(1)
            if name in ("C",):  # extern "C"
                continue
            # Check if this extern has a corresponding definition in any .cpp
            defined_in_cpp = False
            for cf in files:
                if not (str(cf).endswith('.cpp') or str(cf).endswith('.ino')):
                    continue
                cpp_content = all_contents_clean[str(cf)]
                # Look for non-extern definition
                if re.search(r'(?<!extern\s)\b' + re.escape(name) + r'\b\s*[=;({[]', cpp_content):
                    defined_in_cpp = True
                    break
            
            if not defined_in_cpp:
                rel = Path(f).relative_to(root)
                line_no = content[:m.start()].count('\n') + 1
                print(f"  ⚠️  {rel}:{line_no}  extern {name} — no definition found in .cpp files")
    
    # ========================================================================
    # SUMMARY
    # ========================================================================
    total_issues = len(unused)
    print(f"\n{'='*70}")
    print(f"SUMMARY: {total_issues} potentially unused symbols found")
    print(f"{'='*70}")
    print("⚠️  These are CANDIDATES — verify manually before removing!")
    print("    False positives: framework callbacks, ISRs, linker symbols,")
    print("    template instantiations, and macro-generated code.")
    
    return 0 if total_issues == 0 else 1


if __name__ == "__main__":
    main()
