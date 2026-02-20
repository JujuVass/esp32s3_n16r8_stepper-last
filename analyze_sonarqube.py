#!/usr/bin/env python3
"""
SonarQube Report Analyzer
=========================
Quick analysis of VS Code Problems panel JSON export (sonarqube.txt).

Usage:
    python analyze_sonarqube.py                          # default: Desktop/sonarqube.txt
    python analyze_sonarqube.py path/to/sonarqube.txt    # custom path
    python analyze_sonarqube.py --diff old.txt new.txt   # compare two reports
"""

import json
import sys
import os
from collections import Counter, defaultdict
from pathlib import Path

# ============================================================================
# CONFIGURATION
# ============================================================================

# Project root (auto-detected from script location)
PROJECT_ROOT = Path(__file__).parent.resolve()

# Default input file
DEFAULT_INPUT = Path(os.environ.get("USERPROFILE", "~")) / "Desktop" / "sonarqube.txt"

# SonarQube rule descriptions (extend as needed)
RULE_DESCRIPTIONS = {
    # Code quality & cleanup
    "S125":  "Remove commented-out code",
    "S134":  "Control flow nesting too deep (if/for/while)",
    "S107":  "Too many function parameters",
    "S1066": "Collapsible if-statements",
    "S1068": "Unused private member",
    "S1103": "Trailing whitespace / formatting",
    "S1117": "Local variable shadows outer variable",
    "S1155": "Use isEmpty() instead of size()==0",
    "S1172": "Unused function parameters",
    "S1188": "Lambda/closure too long — extract to function",
    "S1448": "Class has too many methods",
    "S1481": "Unused local variables",
    "S1659": "Multiple variables per declaration",
    "S1709": "Constructors should be explicit",
    "S1820": "Struct/class has too many fields",
    "S1854": "Dead stores (assigned but never used)",
    "S1905": "Unnecessary cast",
    "S1912": "Use reentrant localtime_r",
    # Modern C++ idioms
    "S3230": "Prefer in-class initializers over constructor init-list",
    "S3358": "Nested ternary operators — simplify",
    "S3490": "Use = default for trivial constructors/destructors",
    "S3491": "Use nullptr instead of 0/NULL",
    "S3628": "Use range-based for loop",
    "S3642": "Use enum class instead of plain enum",
    "S3687": "Use structured bindings (C++17)",
    "S3776": "Cognitive complexity too high — refactor",
    "S4962": "Use nullptr for pointer comparison",
    # C++ safety & type rules
    "S5008": "Use integral types from <cstdint>",
    "S5025": "Use smart pointers instead of raw new/delete",
    "S5028": "Use #pragma once instead of include guards",
    "S5205": "Use string_view for non-owning references",
    "S5276": "Implicit narrowing conversion (double->float, etc.)",
    "S5350": "Use std::midpoint for safe midpoint calc",
    "S5421": "Global variables should be const",
    "S5566": "Use default member initializers",
    "S5817": "Methods should be const when possible",
    "S5827": "Use auto in variable declarations",
    "S5945": "Use std::array/vector instead of C-style arrays",
    "S5955": "Use class types instead of struct for complex types",
    # Style & auto
    "S6004": "Use auto where type is obvious",
    "S6018": "Use override on virtual methods",
    "S6164": "Use constexpr for compile-time computable functions",
    "S6177": "Use std::string_view parameters",
    "S6179": "File too long — split into smaller units",
    "S6191": "Move-only types should be moved, not copied",
    "S6229": "Use std::format / format strings",
    "S954":  "Include what you use",
    "S959":  "Header/include guard issues (#pragma once)",
    "S995":  "Function definition in header file",
}

# Severity mapping (VS Code severity levels)
SEVERITY_MAP = {
    8: "Error",
    4: "Warning",
    2: "Info",
    1: "Hint",
}

# ============================================================================
# CORE ANALYSIS
# ============================================================================

def load_report(filepath):
    """Load and parse the JSON report file."""
    with open(filepath, "r", encoding="utf-8") as f:
        return json.load(f)


def normalize_path(resource, project_root=None):
    """Convert absolute resource path to project-relative path."""
    # Remove leading /c: style prefix
    path = resource.replace("/c:/", "C:/").replace("/", "\\")
    if project_root:
        try:
            return str(Path(path).relative_to(project_root))
        except ValueError:
            pass
    # Fallback: just show filename
    return Path(path).name


def extract_rule(code):
    """Extract rule ID from code field (e.g., 'cpp:S5276' → 'S5276')."""
    if not code:
        return "unknown"
    return code.split(":")[-1] if ":" in str(code) else str(code)


def analyze(issues, project_root=None):
    """Analyze issues and return structured results."""
    sonarlint = [i for i in issues if i.get("owner") == "sonarlint"]
    other = [i for i in issues if i.get("owner") != "sonarlint"]

    # By rule
    rule_counter = Counter()
    rule_files = defaultdict(lambda: Counter())
    rule_severities = {}
    
    for issue in sonarlint:
        rule = extract_rule(issue.get("code", ""))
        filepath = normalize_path(issue.get("resource", ""), project_root)
        rule_counter[rule] += 1
        rule_files[rule][filepath] += 1
        rule_severities[rule] = issue.get("severity", 0)

    # By file
    file_counter = Counter()
    file_rules = defaultdict(lambda: Counter())
    
    for issue in sonarlint:
        filepath = normalize_path(issue.get("resource", ""), project_root)
        rule = extract_rule(issue.get("code", ""))
        file_counter[filepath] += 1
        file_rules[filepath][rule] += 1

    # By severity
    severity_counter = Counter()
    for issue in sonarlint:
        sev = SEVERITY_MAP.get(issue.get("severity", 0), f"Unknown({issue.get('severity')})")
        severity_counter[sev] += 1

    return {
        "total": len(issues),
        "sonarlint_count": len(sonarlint),
        "other_count": len(other),
        "other_issues": other,
        "by_rule": rule_counter,
        "rule_files": rule_files,
        "rule_severities": rule_severities,
        "by_file": file_counter,
        "file_rules": file_rules,
        "by_severity": severity_counter,
    }


# ============================================================================
# DISPLAY
# ============================================================================

def print_header(text, char="="):
    """Print a formatted header."""
    print(f"\n{char * 70}")
    print(f"  {text}")
    print(f"{char * 70}")


def print_summary(results):
    """Print the main summary."""
    print_header("SONARQUBE REPORT ANALYSIS")
    
    print(f"\n  Total issues:    {results['total']}")
    print(f"  SonarLint:       {results['sonarlint_count']}")
    print(f"  Other (IDE):     {results['other_count']}")
    
    if results["other_issues"]:
        print(f"\n  Non-SonarLint issues:")
        for issue in results["other_issues"]:
            filepath = Path(issue.get("resource", "")).name
            line = issue.get("startLineNumber", "?")
            msg = issue.get("message", "")[:80]
            print(f"    {filepath}:{line} — {msg}")


def print_by_severity(results):
    """Print breakdown by severity."""
    print_header("BY SEVERITY", "-")
    for sev, count in results["by_severity"].most_common():
        bar = "#" * min(count // 5, 40)
        print(f"  {sev:<10} {count:>5}  {bar}")


def print_by_rule(results, top_n=None):
    """Print breakdown by rule with descriptions and file details."""
    print_header("BY RULE (sorted by count)")
    
    rules = results["by_rule"].most_common(top_n)
    max_count = rules[0][1] if rules else 1
    
    for rule, count in rules:
        desc = RULE_DESCRIPTIONS.get(rule, "")
        sev = SEVERITY_MAP.get(results["rule_severities"].get(rule, 0), "?")
        bar_len = int((count / max_count) * 30)
        bar = "#" * bar_len
        
        print(f"\n  {rule:<8} {count:>4}  {bar}  [{sev}]")
        if desc:
            print(f"           {desc}")
        
        # Show top files for this rule
        top_files = results["rule_files"][rule].most_common(5)
        for filepath, fcount in top_files:
            print(f"           {fcount:>3}× {filepath}")
        remaining = len(results["rule_files"][rule]) - 5
        if remaining > 0:
            print(f"           ... +{remaining} more files")


def print_by_file(results, top_n=20):
    """Print breakdown by file."""
    print_header("BY FILE (top {})".format(top_n))
    
    files = results["by_file"].most_common(top_n)
    max_count = files[0][1] if files else 1
    
    for filepath, count in files:
        bar_len = int((count / max_count) * 25)
        bar = "#" * bar_len
        rules = results["file_rules"][filepath]
        rule_summary = ", ".join(f"{r}({c})" for r, c in rules.most_common(4))
        
        print(f"  {count:>4}  {bar:<25}  {filepath}")
        print(f"        {rule_summary}")


def print_actionable(results):
    """Print suggested action plan based on issue counts."""
    print_header("ACTION PLAN (by impact)")
    
    rules = results["by_rule"].most_common()
    total = results["sonarlint_count"]
    cumulative = 0
    
    print(f"\n  {'Rule':<8} {'Count':>5} {'%':>5} {'Cumul%':>7}  Description")
    print(f"  {'-'*8} {'-'*5} {'-'*5} {'-'*7}  {'-'*35}")
    
    for rule, count in rules:
        cumulative += count
        pct = (count / total * 100) if total else 0
        cum_pct = (cumulative / total * 100) if total else 0
        desc = RULE_DESCRIPTIONS.get(rule, "(?)")
        
        marker = " <-- 80%" if cum_pct >= 80 and (cum_pct - pct * 100 / total) < 80 else ""
        print(f"  {rule:<8} {count:>5} {pct:>4.1f}% {cum_pct:>5.1f}%   {desc}{marker}")


# ============================================================================
# DIFF MODE
# ============================================================================

def diff_reports(old_path, new_path, project_root=None):
    """Compare two reports and show changes."""
    old_issues = load_report(old_path)
    new_issues = load_report(new_path)
    
    old_results = analyze(old_issues, project_root)
    new_results = analyze(new_issues, project_root)
    
    print_header("DIFF REPORT: {} → {}".format(Path(old_path).name, Path(new_path).name))
    
    old_total = old_results["sonarlint_count"]
    new_total = new_results["sonarlint_count"]
    delta = new_total - old_total
    sign = "+" if delta > 0 else ""
    
    print(f"\n  SonarLint issues: {old_total} → {new_total} ({sign}{delta})")
    
    # By rule delta
    all_rules = set(old_results["by_rule"].keys()) | set(new_results["by_rule"].keys())
    deltas = []
    for rule in all_rules:
        old_count = old_results["by_rule"].get(rule, 0)
        new_count = new_results["by_rule"].get(rule, 0)
        diff = new_count - old_count
        if diff != 0:
            deltas.append((diff, rule, old_count, new_count))
    
    deltas.sort(key=lambda x: x[0])  # Most reduced first
    
    if deltas:
        print(f"\n  {'Rule':<8} {'Old':>5} {'New':>5} {'Delta':>6}  Description")
        print(f"  {'-'*8} {'-'*5} {'-'*5} {'-'*6}  {'-'*35}")
        
        for diff, rule, old_c, new_c in deltas:
            sign = "+" if diff > 0 else ""
            desc = RULE_DESCRIPTIONS.get(rule, "(?)")
            indicator = "+" if diff < 0 else "-" if diff > 0 else " "
            print(f"  {rule:<8} {old_c:>5} {new_c:>5} {sign}{diff:>5}  {indicator} {desc}")
    
    # New/resolved files
    old_files = set(old_results["by_file"].keys())
    new_files = set(new_results["by_file"].keys())
    
    resolved_files = old_files - new_files
    new_problem_files = new_files - old_files
    
    if resolved_files:
        print(f"\n  Files fully resolved ({len(resolved_files)}):")
        for f in sorted(resolved_files):
            print(f"    [OK] {f}")
    
    if new_problem_files:
        print(f"\n  New files with issues ({len(new_problem_files)}):")
        for f in sorted(new_problem_files):
            print(f"    [!!] {f}")


# ============================================================================
# MAIN
# ============================================================================

def main():
    args = sys.argv[1:]
    
    # Diff mode
    if len(args) >= 3 and args[0] == "--diff":
        diff_reports(args[1], args[2], PROJECT_ROOT)
        return
    
    # Single report mode
    input_file = Path(args[0]) if args else DEFAULT_INPUT
    
    if not input_file.exists():
        print(f"Error: File not found: {input_file}")
        print(f"Usage: python {Path(__file__).name} [sonarqube.txt]")
        print(f"       python {Path(__file__).name} --diff old.txt new.txt")
        sys.exit(1)
    
    print(f"  Loading: {input_file}")
    issues = load_report(input_file)
    results = analyze(issues, PROJECT_ROOT)
    
    print_summary(results)
    print_by_severity(results)
    print_by_rule(results)
    print_by_file(results)
    print_actionable(results)
    
    print(f"\n{'=' * 70}")
    print(f"  Report: {input_file.name} | {results['sonarlint_count']} SonarLint issues")
    print(f"{'=' * 70}\n")


if __name__ == "__main__":
    main()
