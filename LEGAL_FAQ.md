# SLABFLUX Legal & Licensing FAQ

This document provides plain-English clarifications regarding the SLABFLUX Source-Available and Ecosystem License. It serves to help developers, freelancers, and enterprises clearly understand their rights and restrictions regarding the intellectual property of this engine.

### 1. Is SLABFLUX "Open Source"?
No. SLABFLUX is published under a **Source-Available, Fair-Code** model. While the source code is public for peer review, educational purposes, and independent developer use, it is not licensed under an OSI-approved Open Source license (such as MIT or GPL) because it explicitly restricts corporate and enterprise deployment.

### 2. I am an independent developer. Can I use this for a side project?
**Yes.** Under the *Independent Developer Production Grant* (Section 3 of the License), individuals, sole proprietors, and hobbyists can use, modify, and even monetize independent products built with SLABFLUX, provided they are not acting as a proxy for a larger corporation.

### 3. I am a freelancer/contractor. Can I use SLABFLUX to build a system for my corporate client?
**No.** This is explicitly prohibited. The license contains a strict proxy clause to prevent this exact loophole. You cannot bypass the *Corporate Production Lock* by having an independent contractor integrate the engine into a corporate environment.

### 4. Can my company evaluate the engine?
**Yes.** Section 2 of the License grants corporations the right to download, compile, and benchmark the software strictly within isolated, non-production sandbox environments to validate its performance claims. However, embedding it into your actual codebase or deploying it to a live environment is strictly prohibited.

### 5. Can I use the SLABFLUX architecture as a reference to build my own identical engine?
Not for corporate use. While general concepts (e.g., ring buffers, io_uring) are public, the specific structural organization, memory layouts, and architectural synthesis of SLABFLUX are protected intellectual property.
Re‑implementing these elements for corporate deployment without authorization would fall outside the license.

### 6. What happens if a corporation uses SLABFLUX without an explicit exemption?
Corporate deployment without an exemption is outside the scope of the license.
To avoid compliance issues, organizations should request authorization before integrating SLABFLUX into production systems.

### 7. What if a corporate entity has compliance or intellectual property inquiries?
Corporate deployment is strictly prohibited under the standard license. For formal copyright validation, legal compliance reviews, or intellectual property inquiries, corporate entities must contact the author directly.

**Contact:** kbartadev@gmail.com

---
*Disclaimer: This FAQ is provided for informational purposes only and does not replace or modify the legal terms defined in the official `LICENSE.md` file. In the event of a conflict, the `LICENSE.md` shall govern.*
