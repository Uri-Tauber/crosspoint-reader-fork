## 2024-05-24 - String Allocations in Path Processing
**Learning:** The ESP32-C3 is severely constrained (~380KB usable RAM). Functions called frequently during parsing (like `FsHelpers::normalisePath`) that create many intermediate `std::string` objects cause heap fragmentation and CPU overhead.
**Action:** Use `std::string_view` for substring manipulation and `reserve()` to pre-allocate capacity before joining strings. This avoids continuous dynamic memory allocations on the heap.
