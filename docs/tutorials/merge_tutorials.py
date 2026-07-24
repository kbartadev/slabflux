#!/usr/bin/env python3
import os
import re
from pathlib import Path

"""
SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
SLABFLUX TUTORIAL MERGER

Merges fragmented tutorial files into a single deterministic document.
Logic: Sorts by Phase (Major) and Module (Minor) indices.
"""

def get_tutorial_metadata(filepath):
    """Extracts title and sorting key from filename and content."""
    filename = filepath.name
    # Matches tut.X.Y.name.md or tut.X.name.md
    match = re.search(r'tut\.(\d+)(?:\.(\d+))?', filename)
    
    major = int(match.group(1)) if match else 99
    minor = int(match.group(2)) if match and match.group(2) else 0
    
    title = "Unknown Tutorial"
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                if line.startswith('# '):
                    title = line.strip('# \n')
                    break
    except Exception:
        pass
        
    return (major, minor), title

def merge():
    base_path = Path("/home/kris/src/base/slabflux_rte/docs/tutorials")
    output_file = base_path / "SLABFLUX_MASTER_GUIDE.md"
    
    # 1. Gather all main-track tutorials
    tutorial_files = list(base_path.glob("tut.*.md"))
    
    # 2. Sort by major then minor version
    tutorial_files.sort(key=lambda x: get_tutorial_metadata(x)[0])
    
    print(f"Found {len(tutorial_files)} tutorial modules. Compiling...")

    with open(output_file, 'w', encoding='utf-8') as master:
        # Header
        master.write("# SLABFLUX MASTER GUIDE\n")
        master.write("> *Deterministic Systems Engineering & Runtime Architecture*\n\n")
        master.write("## Table of Contents\n")
        
        # Generate TOC
        for filepath in tutorial_files:
            version, title = get_tutorial_metadata(filepath)
            v_str = f"{version[0]}.{version[1]}"
            anchor = title.lower().replace(' ', '-').replace('&', '').replace('(', '').replace(')', '')
            master.write(f"- [{v_str}: {title}](#{anchor})\n")
        
        master.write("\n---\n\n")
        
        # Inject Content
        for filepath in tutorial_files:
            version, title = get_tutorial_metadata(filepath)
            print(f"  [+] Injecting {version[0]}.{version[1]}: {title}")
            
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
                
                # Standardize headers: ensure the main title is an H1 and increment others if needed
                # (SlabFlux standard is to have the file start with the H1 title)
                
                master.write(f"<!-- START OF TUTORIAL {version[0]}.{version[1]} -->\n")
                master.write(content)
                master.write("\n\n---\n\n")

    print(f"\nSuccess. Unified guide generated at: {output_file}")

if __name__ == "__main__":
    # Phase Order Mapping:
    # 1.x: Foundations (Memory, Dispatch, Routing)
    # 2.x: Orchestration (Ignition, Arbitration, States)
    # 3.x: Compute (SIMD, AI)
    # 4.x: Hardware (SSDS, Multiplexing)
    # 5.x: I/O (Bypass, Journaling)
    # 6.x: Resilience (Chaos, Quarantine, CAT)
    # 7.x: Gateways (Parser, Protocol, Reactor)
    # 8.x: Utilities (Time, Strings)
    
    if not os.path.exists("/home/kris/src/base/slabflux_rte/docs/tutorials"):
        print("Error: Tutorial directory not found. Check absolute paths.")
    else:
        merge()
