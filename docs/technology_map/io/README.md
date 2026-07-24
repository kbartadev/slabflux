# SLABFLUX I/O: Documentation Architecture

This directory strictly enforces a **3-Tier Documentation Model** to separate deep implementation mechanics from high-level summaries and academic citations.

### 1. Component Documentation (The Main Files)
**Format:** `component_name.md` (e.g., `non_temporal_writer.md`)
**Purpose:** This is the *actual* documentation. It contains the architectural overviews, hardware implementation directives, core SIMD logic, and operational mechanisms. 
**Rule:** This is the single source of truth for the codebase.

### 2. Architectural Blueprints
**Format:** `blueprint_component_name.md`
**Purpose:** High-level structural maps and executive summaries. These act as quick-reference cards that link out to the main component documentation.

### 3. Bibliographic Foundations
**Format:** `foundation_component_name.md`
**Purpose:** Academic citations, hardware manual references (e.g., Intel SDM), and engineering whitepapers that theoretically back the component's design.

---

### Maintainer Notice
Never delete, merge, or flatten the base component files into the blueprints. They must remain isolated to preserve the integrity and readability of the actual system documentation.