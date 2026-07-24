# Blueprint: signature_router.hpp

## Architectural Overview
Compile-time signature detection matrix. Uses SFINAE to automatically detect and bind incoming events to the correctly const-qualified handler overloads without virtual dispatch.