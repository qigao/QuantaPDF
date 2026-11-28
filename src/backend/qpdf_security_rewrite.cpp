#include "qpdf_document_internal.h"
#include "secure_random.h"
#include "validation/validation.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/RandomDataProvider.hh>

#include <array>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

enum class quantapdf_qpdf_security_operation {
    encrypt,
    decrypt,
    reencrypt
};

class quantapdf_qpdf_random_provider final : public RandomDataProvider
{
  public:
    void provideRandomData(unsigned char *data, size_t size) override
    {
        quantapdf_status const status = quantapdf_secure_random(data, size);
        if (status != QUANTAPDF_OK)
            throw std::runtime_error("QuantaPDF secure random failed");
    }
};

quantapdf_qpdf_random_provider *quantapdf_qpdf_process_random_provider()
{
    // qpdf retains this raw pointer globally; it must outlive every qpdf call.
    static auto *provider = new quantapdf_qpdf_random_provider();
    return provider;
}

class quantapdf_qpdf_random_provider_initializer final
{
  public:
    quantapdf_qpdf_random_provider_initializer()
    {
        QUtil::setRandomDataProvider(
            quantapdf_qpdf_process_random_provider());
    }
};

quantapdf_qpdf_random_provider_initializer
    quantapdf_qpdf_random_provider_process_initializer;

quantapdf_status quantapdf_qpdf_security_signature_preflight(
    QPDF& pdf,
    size_t source_size)
{
    uint32_t findings = 0;
    quantapdf_status status = quantapdf_qpdf_audit_document(
        pdf, source_size, &findings);
    if (status != QUANTAPDF_OK)
        return status;
    quantapdf_validation_report report{};
    status = quantapdf_validate_security(findings, QUANTAPDF_AUDIT_SIGNATURE, &report);
    if (status != QUANTAPDF_OK)
        return status;

    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (object.isStream())
            object = object.getDict();
        if (!object.isDictionary())
            continue;
        bool is_signature = false;
        status = quantapdf_qpdf_security_signature_dictionary(
            object, &is_signature);
        if (status != QUANTAPDF_OK)
            return status;
        QPDFObjectHandle field_type = object.getKey("/FT");
        QPDFObjectHandle value = object.getKey("/V");
        if (field_type.isName() && field_type.getName() == "/Sig" &&
            !value.isNull()) {
            if (!value.isDictionary())
                return QUANTAPDF_ERROR_FORMAT;
            status = quantapdf_qpdf_security_signature_dictionary(
                value, &is_signature);
            if (status == QUANTAPDF_OK || !is_signature)
                return QUANTAPDF_ERROR_FORMAT;
            return status;
        }
    }
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_qpdf_security_validate_id(
    QPDFObjectHandle trailer,
    bool create_if_missing)
{
    if (!trailer.hasKey("/ID")) {
        if (!create_if_missing)
            return QUANTAPDF_OK;
        std::array<unsigned char, 16> random_id{};
        quantapdf_status const status = quantapdf_secure_random(
            random_id.data(), random_id.size());
        if (status != QUANTAPDF_OK)
            return status;
        std::string value(
            reinterpret_cast<char const *>(random_id.data()),
            random_id.size());
        QPDFObjectHandle identifiers = QPDFObjectHandle::newArray();
        identifiers.appendItem(QPDFObjectHandle::newString(value));
        identifiers.appendItem(QPDFObjectHandle::newString(value));
        trailer.replaceKey("/ID", identifiers);
        return QUANTAPDF_OK;
    }

    QPDFObjectHandle identifiers = trailer.getKey("/ID");
    if (!identifiers.isArray() || identifiers.getArrayNItems() != 2)
        return QUANTAPDF_ERROR_FORMAT;
    QPDFObjectHandle first = identifiers.getArrayItem(0);
    QPDFObjectHandle second = identifiers.getArrayItem(1);
    if (!first.isString() || first.getStringValue().empty() ||
        !second.isString())
        return QUANTAPDF_ERROR_FORMAT;
    return QUANTAPDF_OK;
}

std::string quantapdf_qpdf_security_random_hex()
{
    static constexpr char digits[] = "0123456789abcdef";
    std::array<unsigned char, 32> random{};
    quantapdf_status const status = quantapdf_secure_random(
        random.data(), random.size());
    if (status != QUANTAPDF_OK)
        throw std::runtime_error("QuantaPDF secure random failed");
    std::string hex(random.size() * 2u, '0');
    for (size_t index = 0; index < random.size(); ++index) {
        hex[index * 2u] = digits[random[index] >> 4u];
        hex[index * 2u + 1u] = digits[random[index] & 0x0fu];
    }
    return hex;
}

quantapdf_status quantapdf_qpdf_security_copy_output(
    QPDFWriter& writer,
    unsigned char **out_data,
    size_t *out_size)
{
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (buffer == nullptr || buffer->getSize() == 0 ||
        buffer->getBuffer() == nullptr)
        return QUANTAPDF_ERROR_BACKEND;
    auto *copy = static_cast<unsigned char *>(
        std::malloc(buffer->getSize()));
    if (copy == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    std::memcpy(copy, buffer->getBuffer(), buffer->getSize());
    *out_data = copy;
    *out_size = buffer->getSize();
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_qpdf_security_rewrite(
    quantapdf_qpdf_document *document,
    quantapdf_qpdf_security_operation operation,
    quantapdf_encryption_options const *options,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (document == nullptr || document->source_data == nullptr ||
        document->source_size == 0 || document->password == nullptr ||
        (operation != quantapdf_qpdf_security_operation::decrypt &&
         options == nullptr))
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-security-rewrite",
            reinterpret_cast<char const *>(document->source_data),
            document->source_size,
            document->password);
        (void)pdf->getAllPages();
        (void)pdf->getAllObjects();
        if (pdf->anyWarnings())
            return QUANTAPDF_ERROR_FORMAT;

        quantapdf_status status =
            quantapdf_qpdf_security_signature_preflight(
                *pdf, document->source_size);
        if (status != QUANTAPDF_OK)
            return status;
        bool const encrypted = pdf->isEncrypted();
        if ((operation == quantapdf_qpdf_security_operation::encrypt &&
             encrypted) ||
            (operation != quantapdf_qpdf_security_operation::encrypt &&
             !encrypted))
            return QUANTAPDF_ERROR_UNSUPPORTED;

        status = quantapdf_qpdf_security_validate_id(
            pdf->getTrailer(),
            operation != quantapdf_qpdf_security_operation::decrypt);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);

        if (operation == quantapdf_qpdf_security_operation::decrypt) {
            writer.setPreserveEncryption(false);
            writer.setDeterministicID(true);
            writer.write();
        } else {
            if (QUtil::getRandomDataProvider() !=
                quantapdf_qpdf_process_random_provider())
                return QUANTAPDF_ERROR_UNSUPPORTED;
            QPDFObjectHandle trailer = pdf->getTrailer();
            bool const had_info = trailer.hasKey("/Info");
            QPDFObjectHandle original_info = trailer.getKey("/Info");
            QPDFObjectHandle private_info = QPDFObjectHandle::newDictionary({
                {"/QuantaPDFSecuritySeed",
                 QPDFObjectHandle::newString(
                     quantapdf_qpdf_security_random_hex())}});
            trailer.replaceKey("/Info", private_info);

            qpdf_r3_print_e print = qpdf_r3p_none;
            if ((options->permissions &
                 QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY) != 0u)
                print = qpdf_r3p_full;
            else if ((options->permissions &
                      QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION) != 0u)
                print = qpdf_r3p_low;

            writer.setR6EncryptionParameters(
                options->user_password_utf8,
                options->owner_password_utf8,
                true,
                (options->permissions & QUANTAPDF_PERMISSION_COPY) != 0u,
                (options->permissions & QUANTAPDF_PERMISSION_ASSEMBLE) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_FILL_FORMS) != 0u,
                (options->permissions &
                 QUANTAPDF_PERMISSION_MODIFY_OTHER) != 0u,
                print,
                options->encrypt_metadata != 0);

            if (had_info)
                trailer.replaceKey("/Info", original_info);
            else
                trailer.removeKey("/Info");
            writer.write();
        }

        status = quantapdf_qpdf_security_copy_output(
            writer, out_data, out_size);
        if (status != QUANTAPDF_OK)
            return status;
        if (pdf->anyWarnings()) {
            std::free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            return QUANTAPDF_ERROR_FORMAT;
        }
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

} // namespace

extern "C" quantapdf_status quantapdf_qpdf_encrypt_pdf(
    quantapdf_qpdf_document *document,
    quantapdf_encryption_options const *options,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::encrypt,
        options, out_data, out_size);
}

extern "C" quantapdf_status quantapdf_qpdf_decrypt_pdf(
    quantapdf_qpdf_document *document,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::decrypt,
        nullptr, out_data, out_size);
}

extern "C" quantapdf_status quantapdf_qpdf_reencrypt_pdf(
    quantapdf_qpdf_document *document,
    quantapdf_encryption_options const *options,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_security_rewrite(
        document, quantapdf_qpdf_security_operation::reencrypt,
        options, out_data, out_size);
}
