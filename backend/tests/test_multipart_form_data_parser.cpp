#include <cassert>
#include <iostream>
#include <string>

#include "jobpilot/http/multipart_form_data_parser.h"

using namespace jobpilot::core;

namespace
{
    void TestValidBinaryUpload()
    {
        const std::string boundary = "jobpilot-boundary";
        std::string body =
            "--jobpilot-boundary\r\n"
            "Content-Disposition: form-data; name=\"title\"\r\n"
            "\r\n"
            "C++ resume\r\n"
            "--jobpilot-boundary\r\n"
            "Content-Disposition: form-data; name=\"file\"; "
            "filename=\"resume.pdf\"\r\n"
            "Content-Type: application/pdf\r\n"
            "\r\n"
            "PDF";
        body.push_back('\0');
        body.append(
            "bytes\r\n"
            "--jobpilot-boundary--\r\n");

        MultipartFormData form;
        const MultipartParseResult result =
            MultipartFormDataParser::Parse(
                "multipart/form-data; boundary=\"" +
                    boundary + "\"",
                body,
                &form);

        assert(result == MultipartParseResult::kSuccess);
        assert(form.parts.size() == 2U);
        assert(form.parts[0].name == "title");
        assert(form.parts[0].body == "C++ resume");
        assert(form.parts[1].name == "file");
        assert(form.parts[1].filename.has_value());
        assert(*form.parts[1].filename == "resume.pdf");
        assert(form.parts[1].contentType.has_value());
        assert(*form.parts[1].contentType == "application/pdf");
        std::string expectedBytes = "PDF";
        expectedBytes.push_back('\0');
        expectedBytes.append("bytes");

        assert(
            std::string(
                form.parts[1].body.data(),
                form.parts[1].body.size()) ==
            expectedBytes);
    }

    void TestMalformedForms()
    {
        MultipartFormData form;

        assert(
            MultipartFormDataParser::Parse(
                "application/json",
                "{}",
                &form) ==
            MultipartParseResult::kUnsupportedContentType);

        assert(
            MultipartFormDataParser::Parse(
                "multipart/form-data; boundary=x",
                "--x\r\n"
                "Content-Disposition: form-data; name=\"title\"\r\n"
                "\r\n"
                "value",
                &form) ==
            MultipartParseResult::kMalformed);

        assert(
            MultipartFormDataParser::Parse(
                "multipart/form-data; boundary=x",
                "--x\r\n"
                "Content-Disposition: form-data; name=\"a\"; "
                "filename=\"a.txt\"\r\n"
                "\r\n"
                "a\r\n"
                "--x\r\n"
                "Content-Disposition: form-data; name=\"b\"; "
                "filename=\"b.txt\"\r\n"
                "\r\n"
                "b\r\n"
                "--x--\r\n",
                &form) ==
            MultipartParseResult::kMalformed);
    }
} // namespace

int main()
{
    TestValidBinaryUpload();
    TestMalformedForms();

    std::cout << "multipart form data parser tests passed\n";
    return 0;
}
