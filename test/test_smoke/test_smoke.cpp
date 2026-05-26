// test_smoke.cpp — verifica que el entorno native/Unity está vivo.
// Este test es smoke perpetuo: confirma que el toolchain native sigue
// funcionando entre cambios de hitos. No se elimina en hito 3.

#include <unity.h>

void test_smoke_unity_alive() {
    TEST_ASSERT_TRUE(1 + 1 == 2);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_smoke_unity_alive);
    return UNITY_END();
}
