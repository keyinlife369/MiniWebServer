// 表单解析器单元测试
#include <gtest/gtest.h>
#include "form_parser.h"

// ========== URL 解码 ==========
TEST(UrlDecodeTest, PlainText) {
    EXPECT_EQ(urlDecode("hello"), "hello");
    EXPECT_EQ(urlDecode("hello world"), "hello world");
}

TEST(UrlDecodeTest, PlusToSpace) {
    EXPECT_EQ(urlDecode("hello+world"), "hello world");
    EXPECT_EQ(urlDecode("a+b+c"), "a b c");
}

TEST(UrlDecodeTest, PercentEncoding) {
    EXPECT_EQ(urlDecode("hello%20world"), "hello world");
    EXPECT_EQ(urlDecode("%41%42%43"), "ABC");     // A=0x41, B=0x42, C=0x43
    EXPECT_EQ(urlDecode("%2e%2e"), "..");         // 路径回溯编码
    EXPECT_EQ(urlDecode("%2f"), "/");             // 斜杠编码
}

TEST(UrlDecodeTest, MixedEncoding) {
    EXPECT_EQ(urlDecode("name=%E6%9D%9C%E6%B5%A9%E5%93%B2"), "name=杜浩哲");
    EXPECT_EQ(urlDecode("key+%3D+value"), "key = value");
}

TEST(UrlDecodeTest, InvalidPercentSequence) {
    // 不完整的百分号编码应保留原字符
    EXPECT_EQ(urlDecode("abc%2"), "abc%2");       // 缺少第二个 hex 字符
    EXPECT_EQ(urlDecode("abc%"), "abc%");         // 只有百分号
    EXPECT_EQ(urlDecode("abc%GG"), "abc%GG");     // 无效 hex
}

TEST(UrlDecodeTest, EmptyString) {
    EXPECT_EQ(urlDecode(""), "");
}

// ========== URL 编码表单解析 ==========
TEST(ParseUrlEncodedTest, SinglePair) {
    auto result = parseUrlEncoded("name=Alice");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result["name"], "Alice");
}

TEST(ParseUrlEncodedTest, MultiplePairs) {
    auto result = parseUrlEncoded("name=Alice&age=25&city=Beijing");
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result["name"], "Alice");
    EXPECT_EQ(result["age"], "25");
    EXPECT_EQ(result["city"], "Beijing");
}

TEST(ParseUrlEncodedTest, UrlEncodedValues) {
    auto result = parseUrlEncoded("msg=hello+world&path=%2fetc%2fhosts");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result["msg"], "hello world");
    EXPECT_EQ(result["path"], "/etc/hosts");
}

TEST(ParseUrlEncodedTest, KeyWithoutValue) {
    auto result = parseUrlEncoded("flag");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result["flag"], "");
}

TEST(ParseUrlEncodedTest, EmptyBody) {
    auto result = parseUrlEncoded("");
    EXPECT_TRUE(result.empty());
}

TEST(ParseUrlEncodedTest, EmptyPairSkipped) {
    auto result = parseUrlEncoded("a=1&&b=2");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result["a"], "1");
    EXPECT_EQ(result["b"], "2");
}

TEST(ParseUrlEncodedTest, ValueContainingEquals) {
    auto result = parseUrlEncoded("expr=a%3Db%2Bc");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result["expr"], "a=b+c");
}

// ========== JSON 透传 ==========
TEST(ParseJsonBodyTest, PassThrough) {
    std::string json = R"({"name":"Alice","age":25})";
    EXPECT_EQ(parseJsonBody(json), json);
}