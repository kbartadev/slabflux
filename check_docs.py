#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import re
import sys

# F-stringek (f"...") Python 3.6-ban lettek bevezetve.
if sys.version_info < (3, 6):
    sys.exit("HIBA: A szkript futtatásához Python 3.6+ szükséges. Használd a 'python3 ./check_docs.py' parancsot.")

def check_documentation(root_dir):
    broken_links = []
    missing_indexes = []

    # Regex, ami megtalálja a markdown linkeket: [szöveg](url)
    link_pattern = re.compile(r'\[[^\]]*\]\(([^)]+)\)')

    for dirpath, dirnames, filenames in os.walk(root_dir):
        # 1. Index fájlok ellenőrzése
        # Ha nem a gyökérkönyvtárban vagyunk, elvárjuk, hogy legyen egy index fájl
        if dirpath != root_dir:
            dir_name = os.path.basename(dirpath)
            expected_index1 = "index.md"
            expected_index2 = f"{dir_name}.index.md"
            
            if expected_index1 not in filenames and expected_index2 not in filenames:
                missing_indexes.append(dirpath)

        # 2. Törött linkek ellenőrzése az .md fájlokban
        for filename in filenames:
            if filename.endswith('.md'):
                filepath = os.path.join(dirpath, filename)
                
                try:
                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                except Exception as e:
                    print(f"Hiba a fájl olvasásakor: {filepath} - {e}")
                    continue
                
                links = link_pattern.findall(content)
                for link in links:
                    # Külső linkek és tiszta horgonyok (anchor) ignorálása
                    if link.startswith(('http://', 'https://', 'mailto:', '#')):
                        continue
                    
                    # Horgony levágása a fájlnévről (pl. file.md#szekcio -> file.md)
                    clean_link = link.split('#')[0]
                    if not clean_link:
                        continue

                    # Relatív útvonal feloldása a jelenlegi markdown fájl mappájához képest
                    target_path = os.path.normpath(os.path.join(dirpath, clean_link))
                    
                    if not os.path.exists(target_path):
                        broken_links.append((filepath, link, target_path))

    # Eredmények kiírása
    print("\n=== HIÁNYZÓ INDEX FÁJLOK ===")
    if not missing_indexes:
        print("  [OK] Minden mappában található index fájl.")
    else:
        for mi in missing_indexes:
            print(f"  [HIBA] Hiányzik az index.md vagy {os.path.basename(mi)}.index.md innen: {mi}/")

    print("\n=== TÖRÖTT LINKEK (MISSING FILES) ===")
    if not broken_links:
        print("  [OK] Minden hivatkozott fájl létezik.")
    else:
        for file, link, target in broken_links:
            print(f"  [HIBA] Fájl: {file}")
            print(f"         Link: {link}")
            print(f"         Nem található: {target}\n")

    # Kilépési kód (CI/CD-hez hasznos)
    if broken_links or missing_indexes:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    target_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    print(f"Dokumentáció ellenőrzése a(z) '{target_dir}' könyvtárban...")
    check_documentation(target_dir)