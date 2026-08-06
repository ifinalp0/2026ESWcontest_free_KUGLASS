#include <cstdio>

extern "C" void app_main(void);

int main() {
    app_main();
    std::puts("esp32_b app host link ok");
    return 0;
}
