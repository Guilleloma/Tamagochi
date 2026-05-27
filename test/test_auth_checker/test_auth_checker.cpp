// =============================================================================
// test_auth_checker.cpp — tests para AuthChecker (Basic auth + base64 decode).
//
// Framework: Unity (pio test -e native).
// AuthChecker es lógica pura sin Arduino: testeable en host directamente.
//
// Base64 precalculadas (offline con `echo -n "x:y" | base64`):
//   "admin:1234" → "YWRtaW46MTIzNA=="
//   "other:1234" → "b3RoZXI6MTIzNA=="
//   "admin:wrong"→ "YWRtaW46d3Jvbmc="
//
// Hito 4 (Fase 3): 6 tests según sección 9.4 del plan.
// =============================================================================

#include <unity.h>
#include "AuthChecker.h"

void setUp()    {}
void tearDown() {}

// Credenciales de referencia para todos los tests.
static const char kExpectedUser[] = "admin";
static const char kExpectedPass[] = "1234";

// =============================================================================
// 1. accepts_valid_basic_auth_header
// Header "Basic YWRtaW46MTIzNA==" con expected admin/1234 → true.
// =============================================================================
void test_accepts_valid_basic_auth_header() {
    const char* header = "Basic YWRtaW46MTIzNA==";
    TEST_ASSERT_TRUE(AuthChecker::checkBasicAuth(header, kExpectedUser, kExpectedPass));
}

// =============================================================================
// 2. rejects_missing_authorization
// authHeader == nullptr → false.
// =============================================================================
void test_rejects_missing_authorization() {
    TEST_ASSERT_FALSE(AuthChecker::checkBasicAuth(nullptr, kExpectedUser, kExpectedPass));
}

// =============================================================================
// 3. rejects_malformed_basic_header
// "Basic ???" — la parte b64 contiene '?' que es inválido en base64 → false.
// =============================================================================
void test_rejects_malformed_basic_header() {
    const char* header = "Basic ???";
    TEST_ASSERT_FALSE(AuthChecker::checkBasicAuth(header, kExpectedUser, kExpectedPass));
}

// =============================================================================
// 4. rejects_wrong_scheme
// "Bearer xyz" no empieza por "Basic " → false.
// =============================================================================
void test_rejects_wrong_scheme() {
    const char* header = "Bearer xyz";
    TEST_ASSERT_FALSE(AuthChecker::checkBasicAuth(header, kExpectedUser, kExpectedPass));
}

// =============================================================================
// 5. rejects_wrong_user
// base64("other:1234") = "b3RoZXI6MTIzNA==" con expected admin/1234 → false.
// =============================================================================
void test_rejects_wrong_user() {
    const char* header = "Basic b3RoZXI6MTIzNA==";
    TEST_ASSERT_FALSE(AuthChecker::checkBasicAuth(header, kExpectedUser, kExpectedPass));
}

// =============================================================================
// 6. rejects_wrong_password
// base64("admin:wrong") = "YWRtaW46d3Jvbmc=" con expected admin/1234 → false.
// =============================================================================
void test_rejects_wrong_password() {
    const char* header = "Basic YWRtaW46d3Jvbmc=";
    TEST_ASSERT_FALSE(AuthChecker::checkBasicAuth(header, kExpectedUser, kExpectedPass));
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_accepts_valid_basic_auth_header);
    RUN_TEST(test_rejects_missing_authorization);
    RUN_TEST(test_rejects_malformed_basic_header);
    RUN_TEST(test_rejects_wrong_scheme);
    RUN_TEST(test_rejects_wrong_user);
    RUN_TEST(test_rejects_wrong_password);

    return UNITY_END();
}
