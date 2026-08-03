#include <cstdio>

extern "C" void app_main(void);

int main() {
    // Host branches do not touch GPIO/UART, but this verifies that the complete
    // master app scaffold and all of its non-IDF symbols compile and link.
    app_main();
    std::puts("master app host link ok");
    return 0;
}
