#include <CppUTest/TestHarness.h>
#include <pw_span/span.h>
#include <pw_status/status.h>
#include <pw_tokenizer/tokenize.h>

TEST_GROUP(PigweedCompat) {
};

TEST(PigweedCompat, TokenizesStringLiteralsAtCompileTime) {
    constexpr pw::tokenizer::Token token = PW_TOKENIZE_STRING("imu sample");

    CHECK_EQUAL(0xF59213DCu, token);
}

TEST(PigweedCompat, ProvidesStatusAndSpanBasics) {
    int samples[] = {1, 2, 3};
    pw::span<int> sample_span(samples);

    CHECK_TRUE(pw::OkStatus().ok());
    CHECK_EQUAL(static_cast<uint32_t>(pw::StatusCode::InvalidArgument),
                static_cast<uint32_t>(pw::Status::InvalidArgument().code()));
    CHECK_EQUAL(3u, sample_span.size());
    CHECK_EQUAL(2, sample_span[1]);
}
