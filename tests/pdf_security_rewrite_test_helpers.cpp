#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/RandomDataProvider.hh>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <thread>

class quantapdf_security_sentinel_provider final : public RandomDataProvider
{
  public:
    void provideRandomData(unsigned char *data, size_t size) override
    {
        std::memset(data, 0xa5, size);
    }
};

extern "C" {

typedef struct quantapdf_security_inspection {
    int encrypted;
    int revision;
    int version;
    int stream_aesv3;
    int string_aesv3;
    int file_aesv3;
    int extension_level;
    int encrypt_metadata;
    int allow_accessibility;
    int allow_copy;
    int allow_assemble;
    int allow_fill_forms;
    int allow_annotate_and_fill_forms;
    int allow_modify_other;
    int allow_print_low_resolution;
    int allow_print_high_quality;
    unsigned char id1[16];
    unsigned char id2[16];
    unsigned char encryption_key[32];
    size_t encryption_key_size;
} quantapdf_security_inspection;

int quantapdf_security_inspect_pdf(
    unsigned char const *data,
    size_t size,
    char const *password,
    quantapdf_security_inspection *out)
{
    if (data == nullptr || size == 0 || out == nullptr)
        return 0;
    *out = {};
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-security-inspection",
            reinterpret_cast<char const *>(data),
            size,
            password == nullptr ? "" : password);
        int permissions = 0;
        QPDF::encryption_method_e stream_method = QPDF::e_none;
        QPDF::encryption_method_e string_method = QPDF::e_none;
        QPDF::encryption_method_e file_method = QPDF::e_none;
        out->encrypted = pdf->isEncrypted(
            out->revision,
            permissions,
            out->version,
            stream_method,
            string_method,
            file_method) ? 1 : 0;
        out->stream_aesv3 = stream_method == QPDF::e_aesv3;
        out->string_aesv3 = string_method == QPDF::e_aesv3;
        out->file_aesv3 = file_method == QPDF::e_aesv3;
        out->extension_level = pdf->getExtensionLevel();
        out->encrypt_metadata = 1;
        if (out->encrypted) {
            QPDFObjectHandle encrypt = pdf->getTrailer().getKey("/Encrypt");
            QPDFObjectHandle value = encrypt.getKey("/EncryptMetadata");
            if (value.isBool())
                out->encrypt_metadata = value.getBoolValue() ? 1 : 0;
            out->allow_accessibility = pdf->allowAccessibility();
            out->allow_copy = pdf->allowExtractAll();
            out->allow_assemble = pdf->allowModifyAssembly();
            out->allow_fill_forms = pdf->allowModifyForm();
            out->allow_annotate_and_fill_forms =
                pdf->allowModifyAnnotation();
            out->allow_modify_other = pdf->allowModifyOther();
            out->allow_print_low_resolution = pdf->allowPrintLowRes();
            out->allow_print_high_quality = pdf->allowPrintHighRes();
            QPDFObjectHandle identifiers =
                pdf->getTrailer().getKey("/ID");
            if (identifiers.isArray() &&
                identifiers.getArrayNItems() == 2 &&
                identifiers.getArrayItem(0).isString() &&
                identifiers.getArrayItem(1).isString()) {
                std::string const id1 =
                    identifiers.getArrayItem(0).getStringValue();
                std::string const id2 =
                    identifiers.getArrayItem(1).getStringValue();
                if (id1.size() >= sizeof(out->id1))
                    std::memcpy(out->id1, id1.data(), sizeof(out->id1));
                if (id2.size() >= sizeof(out->id2))
                    std::memcpy(out->id2, id2.data(), sizeof(out->id2));
            }
            std::string const key = pdf->getEncryptionKey();
            out->encryption_key_size = key.size();
            if (key.size() >= sizeof(out->encryption_key))
                std::memcpy(
                    out->encryption_key,
                    key.data(),
                    sizeof(out->encryption_key));
        }
        return pdf->anyWarnings() ? 0 : 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_metadata_fixture(
    char const *source_path,
    char const *output_path)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle metadata = pdf->newStream(
            "<x:xmpmeta>QUANTAPDF_METADATA_CLEAR_MARKER</x:xmpmeta>");
        metadata.getDict().replaceKey(
            "/Type", QPDFObjectHandle::newName("/Metadata"));
        metadata.getDict().replaceKey(
            "/Subtype", QPDFObjectHandle::newName("/XML"));
        pdf->getRoot().replaceKey("/Metadata", metadata);
        QPDFObjectHandle info = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Title", QPDFObjectHandle::newUnicodeString(
                    "QuantaPDF Security Metadata")}}));
        pdf->getTrailer().replaceKey("/Info", info);

        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_signature_fixture(
    char const *source_path,
    char const *output_path,
    int kind,
    int encrypt)
{
    if (source_path == nullptr || output_path == nullptr ||
        kind < 1 || kind > 12)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = QPDFObjectHandle::newNull();
        if (kind >= 1 && kind <= 3) {
            QPDFObjectHandle byte_range = QPDFObjectHandle::newArray();
            for (int index = 0; index < 4; ++index)
                byte_range.appendItem(QPDFObjectHandle::newInteger(0));
            signature = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Type", QPDFObjectHandle::newName(
                        kind == 2 ? "/DocTimeStamp" : "/Sig")},
                    {"/ByteRange", byte_range},
                    {"/Contents", QPDFObjectHandle::newString("signed")}}));
        } else if (kind >= 5 && kind <= 6) {
            QPDFObjectHandle byte_range = QPDFObjectHandle::newArray();
            for (int index = 0; index < 4; ++index)
                byte_range.appendItem(QPDFObjectHandle::newInteger(0));
            QPDFObjectHandle malformed = QPDFObjectHandle::newDictionary({
                {"/ByteRange", byte_range},
                {"/Contents", QPDFObjectHandle::newString("signed")}});
            if (kind == 6)
                malformed.replaceKey(
                    "/Type", QPDFObjectHandle::newName("/NotSignature"));
            signature = pdf->makeIndirectObject(malformed);
        } else if (kind >= 7 && kind <= 8) {
            signature = QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Sig")}});
        } else if (kind >= 9) {
            QPDFObjectHandle byte_range = QPDFObjectHandle::newArray();
            for (int index = 0; index < 4; ++index)
                byte_range.appendItem(QPDFObjectHandle::newInteger(0));
            signature = QPDFObjectHandle::newDictionary({
                {"/ByteRange", byte_range},
                {"/Contents", QPDFObjectHandle::newString("signed")}});
            if (kind == 11 || kind == 12)
                signature.replaceKey(
                    "/Type", QPDFObjectHandle::newName("/DocTimeStamp"));
        }
        if (kind == 1) {
            pdf->getRoot().replaceKey(
                "/QuantaPDFReachableSignature", signature);
        } else if (kind == 3) {
            QPDFObjectHandle permissions = QPDFObjectHandle::newDictionary({
                {"/DocMDP", signature}});
            pdf->getRoot().replaceKey("/Perms", permissions);
        } else if (kind == 4) {
            QPDFObjectHandle field = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/FT", QPDFObjectHandle::newName("/Sig")},
                    {"/V", QPDFObjectHandle::newString("malformed")}}));
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(field);
            pdf->getRoot().replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", fields}}));
        } else if (kind == 7 || kind == 9 || kind == 12) {
            QPDFObjectHandle field = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/FT", QPDFObjectHandle::newName("/Sig")},
                    {"/V", signature}}));
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(field);
            pdf->getRoot().replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", fields}}));
        } else if (kind == 8 || kind == 10 || kind == 11) {
            pdf->getRoot().replaceKey(
                "/Perms", QPDFObjectHandle::newDictionary({
                    {"/DocMDP", signature}}));
        }

        QPDFWriter writer(*pdf, output_path);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(true);
        if (encrypt) {
            writer.setR6EncryptionParameters(
                "sig-user", "sig-owner", true, false, false, false,
                false, false, qpdf_r3p_none, true);
        } else {
            writer.setDeterministicID(true);
        }
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_incremental_signature_fixture(
    char const *source_path,
    char const *output_path,
    char const *password)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->processFile(source_path, password == nullptr ? "" : password);
        QPDFObjGen const root = pdf->getRoot().getObjGen();
        long long const object_number =
            pdf->getTrailer().getKey("/Size").getIntValue();
        if (!root.isIndirect() || object_number <= 0)
            return 0;

        std::ifstream input(source_path, std::ios::binary);
        std::string bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        if (!input.good() && !input.eof())
            return 0;
        size_t const marker = bytes.rfind("startxref");
        if (marker == std::string::npos)
            return 0;
        size_t const number_begin = bytes.find_first_of(
            "0123456789", marker + std::strlen("startxref"));
        if (number_begin == std::string::npos)
            return 0;
        size_t parsed = 0;
        unsigned long long const previous_xref = std::stoull(
            bytes.substr(number_begin), &parsed);
        if (parsed == 0)
            return 0;

        std::ostringstream update;
        size_t const object_offset = bytes.size();
        update << object_number
               << " 0 obj\n<< /Type /Sig /ByteRange [0 0 0 0] "
                  "/Contents <00> >>\nendobj\n";
        size_t const xref_offset = object_offset + update.str().size();
        update << "xref\n" << object_number << " 1\n"
               << std::setw(10) << std::setfill('0') << object_offset
               << " 00000 n \ntrailer\n<< /Size "
               << (object_number + 1) << " /Root "
               << root.getObj() << ' ' << root.getGen()
               << " R /Prev " << previous_xref
               << " >>\nstartxref\n" << xref_offset << "\n%%EOF\n";
        bytes += update.str();

        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output.good() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_id_fixture(
    char const *source_path,
    char const *output_path,
    int malformed)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        if (!malformed)
            return 1;

        std::ifstream input(output_path, std::ios::binary);
        std::string bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        input.close();
        size_t const begin = bytes.find("/ID [");
        if (begin == std::string::npos)
            return 0;
        size_t const end = bytes.find(']', begin);
        if (end == std::string::npos)
            return 0;
        bytes.replace(begin, end - begin + 1u, "/ID [() ()]");
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output.good() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_canonicalize_fixture(
    char const *source_path,
    char const *output_path)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->processFile(source_path);
        for (QPDFObjectHandle page : pdf->getAllPages()) {
            if (page.getKey("/Resources").isNull())
                page.replaceKey(
                    "/Resources", QPDFObjectHandle::newDictionary());
            QPDFObjectHandle annotations = page.getKey("/Annots");
            if (annotations.isArray()) {
                QPDFObjectHandle normalized = QPDFObjectHandle::newArray();
                for (QPDFObjectHandle annotation :
                     annotations.getArrayAsVector()) {
                    if (annotation.isDictionary())
                        normalized.appendItem(annotation);
                }
                page.replaceKey("/Annots", normalized);
            }
        }
        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_check_static_provider_boundary(
    char const *source_path)
{
    if (source_path == nullptr)
        return 0;

    RandomDataProvider *const original = QUtil::getRandomDataProvider();
    quantapdf_security_sentinel_provider sentinel;
    QUtil::setRandomDataProvider(&sentinel);
    quantapdf_document *document = nullptr;
    quantapdf_output *output = nullptr;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "provider-user",
        "provider-owner",
        QUANTAPDF_PERMISSION_ALL,
        1};
    bool boundary_ok =
        quantapdf_open(test_context(), source_path, nullptr, &document) == QUANTAPDF_OK &&
        quantapdf_encrypt_pdf(document, &options, &output) ==
            QUANTAPDF_ERROR_UNSUPPORTED &&
        output == nullptr &&
        QUtil::getRandomDataProvider() == &sentinel;
    quantapdf_drop_output(output);
    quantapdf_close(document);
    QUtil::setRandomDataProvider(original);
    if (!boundary_ok)
        return 0;

    document = nullptr;
    if (quantapdf_open(test_context(), source_path, nullptr, &document) != QUANTAPDF_OK)
        return 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_ok{true};
    std::thread worker([&]() {
        try {
            unsigned char bytes[64];
            while (!stop.load(std::memory_order_relaxed))
                QUtil::initializeWithRandomBytes(bytes, sizeof(bytes));
        } catch (...) {
            worker_ok.store(false, std::memory_order_relaxed);
        }
    });
    for (int iteration = 0; iteration < 16; ++iteration) {
        output = nullptr;
        if (quantapdf_encrypt_pdf(document, &options, &output) !=
            QUANTAPDF_OK) {
            worker_ok.store(false, std::memory_order_relaxed);
            break;
        }
        quantapdf_drop_output(output);
    }
    stop.store(true, std::memory_order_relaxed);
    worker.join();
    quantapdf_close(document);
    return worker_ok.load(std::memory_order_relaxed) ? 1 : 0;
}

}
