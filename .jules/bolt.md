## Learnings
* Passing arguments by `const std::string&` rather than `std::string` prevents unnecessary string duplication.
* In `FileBrowserActivity`, list rendering invokes the filename and extension logic for every file row being displayed. The optimization removed the heap allocation entirely per list row, saving CPU cycles on memory allocation/deallocation on ESP32 constrained hardware.
