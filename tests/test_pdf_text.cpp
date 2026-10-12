#include "backend/qpdf_text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(EXIT_FAILURE); } } while (0)

static void decoded(std::string raw, std::string const& expected)
{
    char *text = nullptr;
    size_t size = 0;
    CHECK(quantapdf_qpdf_decode_text(reinterpret_cast<unsigned char const *>(raw.data()), raw.size(), &text, &size) == QUANTAPDF_OK);
    CHECK(text != nullptr && size == expected.size() && text[size] == 0);
    raw.assign(raw.size(), 'X');
    CHECK(std::memcmp(text, expected.data(), size) == 0);
    std::free(text);
}

int main()
{
    decoded("", "");
    decoded(std::string("A\0B", 3), std::string("A\0B", 3));
    decoded(std::string("\xff", 1), "\xc3\xbf");
    decoded(std::string("\xfe\xff\0A\0\0", 6), std::string("A\0", 2));
    decoded(std::string("\xfe\xff\xd8\x3d\xde\x00", 6), "\xf0\x9f\x98\x80");
    char *text = reinterpret_cast<char *>(1);
    size_t size = 99;
    CHECK(quantapdf_qpdf_decode_text(nullptr, 1, &text, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == nullptr && size == 0);
    CHECK(quantapdf_qpdf_decode_text(nullptr, 0, &text, &size) == QUANTAPDF_OK);
    CHECK(text != nullptr && size == 0 && text[0] == 0); std::free(text);
    text = reinterpret_cast<char *>(1);
    CHECK(quantapdf_qpdf_decode_text(nullptr, 0, &text, nullptr) == QUANTAPDF_ERROR_ARGUMENT && text == nullptr);
    size = 99;
    CHECK(quantapdf_qpdf_decode_text(nullptr, 0, nullptr, &size) == QUANTAPDF_ERROR_ARGUMENT && size == 0);
    return EXIT_SUCCESS;
}
