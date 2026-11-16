#include "content/text_art.h"

#include <catch2/catch_test_macros.hpp>
#include <initializer_list>

namespace {
using namespace cataclysmx;

eastl::string_view FileView(const String& text) { return {text.data(), text.size()}; }

void InvalidFile(eastl::string_view text, ContentError error = ContentError::InvalidData) {
    Diagnostics diagnostics;
    const auto parsed = ParseTextArtFile(text, &diagnostics);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == error);
    REQUIRE_FALSE(diagnostics.Empty());
    const auto& location = diagnostics.Frames().front().location;
    REQUIRE(location.has_value());
    CHECK(location->file.empty());
    CHECK(location->path.empty());
    REQUIRE(location->line.has_value());
    REQUIRE(location->column.has_value());
    CHECK(location->line.value() >= 1);
    CHECK(location->column.value() >= 1);
    const auto quiet = ParseTextArtFile(text);
    REQUIRE_FALSE(quiet.has_value());
    CHECK(quiet.error() == error);
}

TEST_CASE("External art resolves local aliases and forward defaults to canonical RGB", "[content][text-art-file]") {
    const auto parsed = ParseTextArtFile(
        "@text-art 1\n@default paper\n@color ink #0102aF\n@color alias #0102AF\n"
        "@color paper #F5f5F5\n---\n <ink>é<alias>Ω<reset><< > \nЖ\n");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->lines.size() == 2);
    CHECK(parsed->width == 7);
    REQUIRE(parsed->lines[0].size() == 3);
    CHECK(parsed->lines[0][0] == ArtRun{" ", ArtColor::Rgb(245, 245, 245), 1});
    CHECK(parsed->lines[0][1] == ArtRun{"éΩ", ArtColor::Rgb(1, 2, 175), 2});
    CHECK(parsed->lines[0][2] == ArtRun{"< > ", ArtColor::Rgb(245, 245, 245), 4});
    CHECK(parsed->lines[1][0] == ArtRun{"Ж", ArtColor::Rgb(245, 245, 245), 1});
    const auto equivalent = ParseTextArtFile(
        "@text-art 1\n@color p #f5f5f5\n@color i #0102af\n@default p\n---\n"
        " <i>éΩ<default><< > \nЖ");
    REQUIRE(equivalent.has_value());
    CHECK(*parsed == *equivalent);

    const auto context = ParseTextArtFile("@text-art 1\n@color red #DC5353\n---\n<red>a\nb<reset>c<default>d");
    REQUIRE(context.has_value());
    CHECK(context->lines[0][0].color == ArtColor::Rgb(220, 83, 83));
    CHECK(context->lines[1][0] == ArtRun{"bcd", {}, 3});
    const String legacy[] = {"<r>a", "bcd"};
    const auto inlineArt = ParseTextArt({legacy, 2});
    REQUIRE(inlineArt.has_value());
    CHECK(*context == *inlineArt);
}

TEST_CASE("External art line endings preserve intentional blank rows and spaces", "[content][text-art-file]") {
    const auto lf = ParseTextArtFile("@text-art 1\n---\n\n é─█ \n\n");
    const auto crlf = ParseTextArtFile("@text-art 1\r\n---\r\n\r\n é─█ \r\n\r\n");
    REQUIRE(lf.has_value());
    REQUIRE(crlf.has_value());
    CHECK(*lf == *crlf);
    REQUIRE(lf->lines.size() == 3);
    CHECK(lf->lines[0].empty());
    CHECK(lf->lines[1][0] == ArtRun{" é─█ ", {}, 5});
    CHECK(lf->lines[2].empty());
    const auto terminated = ParseTextArtFile("@text-art 1\n---\nx\n");
    const auto unterminated = ParseTextArtFile("@text-art 1\n---\nx");
    REQUIRE(terminated.has_value());
    REQUIRE(unterminated.has_value());
    CHECK(*terminated == *unterminated);
    const auto emptyRow = ParseTextArtFile("@text-art 1\n---\n\n");
    REQUIRE(emptyRow.has_value());
    CHECK(emptyRow->lines.size() == 1);
    CHECK(emptyRow->width == 0);
    InvalidFile("@text-art 1\n---\n");
    InvalidFile("@text-art 1\n---");
    InvalidFile("@text-art 1\r---\nx");
    InvalidFile("@text-art 1\n---\nx\r");
}

TEST_CASE("External art requires exact version and header grammar", "[content][text-art-file]") {
    for (const char* version : {"", "0", "01", "+1", "-1", "1.0", "1 ", " 1", "4294967296", "2x"}) {
        CAPTURE(version);
        const String text = String("@text-art ") + version + "\n---\nx";
        InvalidFile(FileView(text));
    }
    for (const char* version : {"2", "42", "4294967295"}) {
        CAPTURE(version);
        const String text = String("@text-art ") + version + "\n---\nx";
        InvalidFile(FileView(text), ContentError::UnsupportedVersion);
    }
    for (const char* text : {"", "---\nx", "\n@text-art 1\n---\nx", "\xef\xbb\xbf@text-art 1\n---\nx",
        "@text-art 1", "@text-art 1\nx", "@text-art 1\n--- \nx", "@text-art 1\n\n---\nx",
        "@text-art 1\n@color red #12345\n---\nx", "@text-art 1\n@color red #1234567\n---\nx",
        "@text-art 1\n@color red #12345g\n---\nx", "@text-art 1\n@color red 123456\n---\nx",
        "@text-art 1\n@color red #123456 \n---\nx", "@text-art 1\n@color  red #123456\n---\nx",
        "@text-art 1\n@color red #123456\n@color red #654321\n---\nx",
        "@text-art 1\n@default red\n---\nx",
        "@text-art 1\n@default red\n@default red\n@color red #123456\n---\nx"}) {
        CAPTURE(text);
        InvalidFile(text);
    }
    for (const char* name : {"default", "reset", "Red", "1red", "_red", "red-blue", "", "red blue"}) {
        CAPTURE(name);
        const String text = String("@text-art 1\n@color ") + name + " #123456\n---\nx";
        InvalidFile(FileView(text));
    }
    for (const char* tag : {"<red>", "<r>", "<white>", "<>", "</ink>", "<ink >", "<ink", "<"}) {
        CAPTURE(tag);
        const String text = String("@text-art 1\n@color ink #000000\n---\n") + tag;
        InvalidFile(FileView(text));
    }
}

TEST_CASE("External art palette count and name lengths have explicit boundaries", "[content][text-art-file]") {
    String name(64, 'a');
    String file = String("@text-art 1\n@color ") + name + " #000000\n---\n<" + name + ">x";
    CHECK(ParseTextArtFile(FileView(file)).has_value());
    name += 'a';
    file = String("@text-art 1\n@color ") + name + " #000000\n---\nx";
    InvalidFile(FileView(file));
    String header = "@text-art 1\n";
    for (int i = 0; i < 64; ++i) {
        header += "@color c";
        header += static_cast<char>('a' + i / 26);
        header += static_cast<char>('a' + i % 26);
        header += " #123456\n";
    }
    file = header + "---\n<ccl>x";
    CHECK(ParseTextArtFile(FileView(file)).has_value());
    file = header + "@color extra #123456\n---\nx";
    InvalidFile(FileView(file));
}

TEST_CASE("External art errors locate physical lines and UTF-8 byte columns", "[content][text-art-file]") {
    for (const char* bad : {"<unknown>", "\t", "\x1b", "\xc0\xaf", "\xc3", "\x80", "\xed\xa0\x80",
        "\xf4\x90\x80\x80", "\xcc\x81", "\xef\xb8\x8f", "\xe2\x80\x8b", "\xe2\x80\xae",
        "\xe4\xb8\xad", "\xf0\x9f\x98\x80", "\xc2\xad", "\xd2\x83", "\r"}) {
        CAPTURE(bad);
        const String text = String("@text-art 1\r\n@color ink #123456\r\n---\r\nok\r\né") + bad;
        Diagnostics diagnostics;
        InvalidFile(FileView(text));
        CHECK_FALSE(ParseTextArtFile(FileView(text), &diagnostics).has_value());
        REQUIRE_FALSE(diagnostics.Empty());
        const auto& location = diagnostics.Frames().front().location;
        REQUIRE(location.has_value());
        CHECK(location->line.value() == 5);
        CHECK(location->column.value() == 3);
    }
    String nul = "@text-art 1\n---\nx";
    nul += '\0';
    InvalidFile(FileView(nul));
    Diagnostics diagnostics;
    CHECK_FALSE(ParseTextArtFile("@text-art 1\n@color ink #12345z\n---\nx", &diagnostics).has_value());
    REQUIRE_FALSE(diagnostics.Empty());
    const auto& location = diagnostics.Frames().front().location;
    REQUIRE(location.has_value());
    CHECK(location->line.value() == 2);
    CHECK(location->column.value() == 18);
}

TEST_CASE("External art bounds rows cells physical bytes body bytes and file size", "[content][text-art-file]") {
    String file = "@text-art 1\n---\n";
    for (int i = 0; i < 128; ++i) { file.append(256, ' '); file += '\n'; }
    const auto maximum = ParseTextArtFile(FileView(file));
    REQUIRE(maximum.has_value());
    CHECK(maximum->lines.size() == 128);
    CHECK(maximum->width == 256);
    file += '\n';
    InvalidFile(FileView(file));
    file = "@text-art 1\n---\n";
    file.append(257, ' ');
    InvalidFile(FileView(file));

    String row;
    for (int i = 0; i < 585; ++i) { row += "<reset>"; }
    row += ' '; // Exactly 4096 source bytes, but one cell.
    file = "@text-art 1\r\n---\r\n";
    for (int i = 0; i < 32; ++i) { file += row; file += "\r\n"; }
    CHECK(ParseTextArtFile(FileView(file)).has_value()); // Exactly 131072 body bytes.
    file += 'x';
    InvalidFile(FileView(file));
    file = String("@text-art 1\n---\n") + row + ' ';
    InvalidFile(FileView(file)); // 4097 physical bytes, still only two cells.
    file = "@text-art 1\n---\n";
    file.append(147457 - file.size(), ' ');
    InvalidFile(FileView(file));
}

}
