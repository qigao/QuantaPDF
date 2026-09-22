#include <quantapdf/quantapdf.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUANTAPDF_BARCODE_MAX_1D_PAYLOAD ((size_t)4096u)
#define QUANTAPDF_QR_MAX_MODULES 37

typedef struct quantapdf_module_builder {
    unsigned char *modules;
    size_t count;
    size_t capacity;
} quantapdf_module_builder;

typedef struct quantapdf_path_builder {
    quantapdf_composer_path_command *commands;
    size_t count;
    size_t capacity;
} quantapdf_path_builder;

static int quantapdf_barcode_rect_valid(const quantapdf_rect *rect)
{
    return rect != NULL &&
        isfinite(rect->x0) && isfinite(rect->y0) &&
        isfinite(rect->x1) && isfinite(rect->y1) &&
        rect->x1 > rect->x0 && rect->y1 > rect->y0;
}

static quantapdf_status quantapdf_payload_length(
    const char *payload,
    size_t limit,
    size_t *out_length)
{
    size_t length;

    if (payload == NULL || out_length == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    for (length = 0u; length <= limit; ++length) {
        if (payload[length] == '\0') {
            *out_length = length;
            return length == 0u ? QUANTAPDF_ERROR_FORMAT : QUANTAPDF_OK;
        }
    }
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

static int quantapdf_utf8_valid(const unsigned char *text, size_t length)
{
    size_t i = 0u;

    while (i < length) {
        uint32_t codepoint;
        size_t count;
        size_t j;

        if (text[i] < 0x80u) {
            ++i;
            continue;
        }
        if (text[i] >= 0xc2u && text[i] <= 0xdfu) {
            codepoint = (uint32_t)(text[i] & 0x1fu);
            count = 2u;
        } else if (text[i] >= 0xe0u && text[i] <= 0xefu) {
            codepoint = (uint32_t)(text[i] & 0x0fu);
            count = 3u;
        } else if (text[i] >= 0xf0u && text[i] <= 0xf4u) {
            codepoint = (uint32_t)(text[i] & 0x07u);
            count = 4u;
        } else {
            return 0;
        }
        if (count > length - i)
            return 0;
        for (j = 1u; j < count; ++j) {
            if ((text[i + j] & 0xc0u) != 0x80u)
                return 0;
            codepoint =
                (codepoint << 6u) | (uint32_t)(text[i + j] & 0x3fu);
        }
        if ((count == 2u && codepoint < 0x80u) ||
            (count == 3u && codepoint < 0x800u) ||
            (count == 4u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint > 0x10ffffu)
            return 0;
        i += count;
    }
    return 1;
}

static quantapdf_status quantapdf_module_reserve(
    quantapdf_module_builder *builder,
    size_t extra)
{
    unsigned char *grown;
    size_t needed;
    size_t capacity;

    if (extra > SIZE_MAX - builder->count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = builder->count + extra;
    if (needed <= builder->capacity)
        return QUANTAPDF_OK;
    capacity = builder->capacity == 0u ? 128u : builder->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    grown = (unsigned char *)realloc(builder->modules, capacity);
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    builder->modules = grown;
    builder->capacity = capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_module_append(
    quantapdf_module_builder *builder,
    int dark,
    size_t count)
{
    quantapdf_status status;

    status = quantapdf_module_reserve(builder, count);
    if (status != QUANTAPDF_OK)
        return status;
    memset(builder->modules + builder->count, dark ? 1 : 0, count);
    builder->count += count;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_module_append_ascii_bits(
    quantapdf_module_builder *builder,
    const char *bits)
{
    quantapdf_status status;

    while (*bits != '\0') {
        if (*bits != '0' && *bits != '1')
            return QUANTAPDF_ERROR_BACKEND;
        status = quantapdf_module_append(builder, *bits == '1', 1u);
        if (status != QUANTAPDF_OK)
            return status;
        ++bits;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_module_append_bits(
    quantapdf_module_builder *builder,
    unsigned int bits,
    unsigned int count)
{
    quantapdf_status status;
    unsigned int i;

    for (i = count; i > 0u; --i) {
        status = quantapdf_module_append(
            builder, ((bits >> (i - 1u)) & 1u) != 0u, 1u);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_path_reserve(
    quantapdf_path_builder *builder,
    size_t extra)
{
    quantapdf_composer_path_command *grown;
    size_t needed;
    size_t capacity;

    if (extra > SIZE_MAX - builder->count)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    needed = builder->count + extra;
    if (needed <= builder->capacity)
        return QUANTAPDF_OK;
    capacity = builder->capacity == 0u ? 64u : builder->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_path_command *)realloc(
        builder->commands, capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    builder->commands = grown;
    builder->capacity = capacity;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_path_add_rect(
    quantapdf_path_builder *builder,
    float x0,
    float y0,
    float x1,
    float y1)
{
    quantapdf_composer_path_command *command;
    quantapdf_status status;

    status = quantapdf_path_reserve(builder, 5u);
    if (status != QUANTAPDF_OK)
        return status;

    command = &builder->commands[builder->count];
    memset(command, 0, 5u * sizeof(*command));
    command[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    command[0].point1 = (quantapdf_point){x0, y0};
    command[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    command[1].point1 = (quantapdf_point){x1, y0};
    command[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    command[2].point1 = (quantapdf_point){x1, y1};
    command[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    command[3].point1 = (quantapdf_point){x0, y1};
    command[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    builder->count += 5u;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_draw_path_builder(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_path_builder *builder,
    uint32_t argb)
{
    quantapdf_composer_path_options options = {0};

    if (builder->count == 0u)
        return QUANTAPDF_ERROR_FORMAT;
    options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    options.fill = 1;
    options.fill_argb = argb;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return quantapdf_composer_draw_path(
        composer, page_index, builder->commands, builder->count, &options);
}

static quantapdf_status quantapdf_draw_modules(
    quantapdf_composer *composer,
    size_t page_index,
    const quantapdf_rect *bounds,
    uint32_t argb,
    const quantapdf_module_builder *modules)
{
    quantapdf_path_builder path = {0};
    quantapdf_status status = QUANTAPDF_OK;
    float module_width;
    size_t index;

    if (modules->count == 0u)
        return QUANTAPDF_ERROR_FORMAT;
    module_width = (bounds->x1 - bounds->x0) / (float)modules->count;

    index = 0u;
    while (index < modules->count) {
        size_t start;
        if (modules->modules[index] == 0u) {
            ++index;
            continue;
        }
        start = index;
        while (index < modules->count && modules->modules[index] != 0u)
            ++index;
        status = quantapdf_path_add_rect(
            &path,
            bounds->x0 + (float)start * module_width,
            bounds->y0,
            bounds->x0 + (float)index * module_width,
            bounds->y1);
        if (status != QUANTAPDF_OK)
            break;
    }
    if (status == QUANTAPDF_OK)
        status = quantapdf_draw_path_builder(
            composer, page_index, &path, argb);
    free(path.commands);
    return status;
}

static const uint32_t quantapdf_code128_patterns[] = {
    0x212222u, 0x222122u, 0x222221u, 0x121223u, 0x121322u, 0x131222u,
    0x122213u, 0x122312u, 0x132212u, 0x221213u, 0x221312u, 0x231212u,
    0x112232u, 0x122132u, 0x122231u, 0x113222u, 0x123122u, 0x123221u,
    0x223211u, 0x221132u, 0x221231u, 0x213212u, 0x223112u, 0x312131u,
    0x311222u, 0x321122u, 0x321221u, 0x312212u, 0x322112u, 0x322211u,
    0x212123u, 0x212321u, 0x232121u, 0x111323u, 0x131123u, 0x131321u,
    0x112313u, 0x132113u, 0x132311u, 0x211313u, 0x231113u, 0x231311u,
    0x112133u, 0x112331u, 0x132131u, 0x113123u, 0x113321u, 0x133121u,
    0x313121u, 0x211331u, 0x231131u, 0x213113u, 0x213311u, 0x213131u,
    0x311123u, 0x311321u, 0x331121u, 0x312113u, 0x312311u, 0x332111u,
    0x314111u, 0x221411u, 0x431111u, 0x111224u, 0x111422u, 0x121124u,
    0x121421u, 0x141122u, 0x141221u, 0x112214u, 0x112412u, 0x122114u,
    0x122411u, 0x142112u, 0x142211u, 0x241211u, 0x221114u, 0x413111u,
    0x241112u, 0x134111u, 0x111242u, 0x121142u, 0x121241u, 0x114212u,
    0x124112u, 0x124211u, 0x411212u, 0x421112u, 0x421211u, 0x212141u,
    0x214121u, 0x412121u, 0x111143u, 0x111341u, 0x131141u, 0x114113u,
    0x114311u, 0x411113u, 0x411311u, 0x113141u, 0x114131u, 0x311141u,
    0x411131u, 0x211412u, 0x211214u, 0x211232u, 0x2331112u
};

static quantapdf_status quantapdf_append_code128_pattern(
    quantapdf_module_builder *builder,
    unsigned int index)
{
    uint32_t pattern;
    unsigned int count;
    unsigned int i;
    quantapdf_status status;

    if (index >= sizeof(quantapdf_code128_patterns) /
                     sizeof(quantapdf_code128_patterns[0]))
        return QUANTAPDF_ERROR_BACKEND;
    pattern = quantapdf_code128_patterns[index];
    count = index == 106u ? 7u : 6u;
    for (i = 0u; i < count; ++i) {
        unsigned int shift = (count - i - 1u) * 4u;
        unsigned int width = (pattern >> shift) & 0x0fu;
        status = quantapdf_module_append(builder, (i & 1u) == 0u, width);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_encode_code128b(
    quantapdf_module_builder *builder,
    const char *payload,
    size_t length)
{
    unsigned int checksum = 104u;
    quantapdf_status status;
    size_t i;

    status = quantapdf_module_append(builder, 0, 10u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_append_code128_pattern(builder, 104u);
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 0u; i < length; ++i) {
        unsigned char ch = (unsigned char)payload[i];
        unsigned int code;
        if (ch < 0x20u || ch > 0x7eu)
            return QUANTAPDF_ERROR_FORMAT;
        code = (unsigned int)ch - 0x20u;
        status = quantapdf_append_code128_pattern(builder, code);
        if (status != QUANTAPDF_OK)
            return status;
        checksum += code * (unsigned int)(i + 1u);
    }
    status = quantapdf_append_code128_pattern(builder, checksum % 103u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_append_code128_pattern(builder, 106u);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_module_append(builder, 0, 10u);
}

static const char quantapdf_code39_alphabet[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
static const uint16_t quantapdf_code39_patterns[] = {
    0x034u, 0x121u, 0x061u, 0x160u, 0x031u, 0x130u, 0x070u, 0x025u,
    0x124u, 0x064u, 0x109u, 0x049u, 0x148u, 0x019u, 0x118u, 0x058u,
    0x00du, 0x10cu, 0x04cu, 0x01cu, 0x103u, 0x043u, 0x142u, 0x013u,
    0x112u, 0x052u, 0x007u, 0x106u, 0x046u, 0x016u, 0x181u, 0x0c1u,
    0x1c0u, 0x091u, 0x190u, 0x0d0u, 0x085u, 0x184u, 0x0c4u, 0x0a8u,
    0x0a2u, 0x08au, 0x02au
};
#define QUANTAPDF_CODE39_ASTERISK UINT16_C(0x094)

static quantapdf_status quantapdf_append_code39_symbol(
    quantapdf_module_builder *builder,
    uint16_t pattern)
{
    quantapdf_status status;
    unsigned int i;

    for (i = 0u; i < 9u; ++i) {
        unsigned int width =
            (pattern & (UINT16_C(1) << (8u - i))) != 0u ? 3u : 1u;
        status = quantapdf_module_append(builder, (i & 1u) == 0u, width);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static int quantapdf_code39_index(char ch)
{
    const char *found = strchr(quantapdf_code39_alphabet, ch);
    return found == NULL ? -1 : (int)(found - quantapdf_code39_alphabet);
}

static quantapdf_status quantapdf_encode_code39(
    quantapdf_module_builder *builder,
    const char *payload,
    size_t length)
{
    quantapdf_status status;
    size_t i;

    status = quantapdf_module_append(builder, 0, 10u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_append_code39_symbol(
        builder, QUANTAPDF_CODE39_ASTERISK);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_module_append(builder, 0, 1u);
    if (status != QUANTAPDF_OK)
        return status;

    for (i = 0u; i < length; ++i) {
        int index = quantapdf_code39_index(payload[i]);
        if (index < 0)
            return QUANTAPDF_ERROR_FORMAT;
        status = quantapdf_append_code39_symbol(
            builder, quantapdf_code39_patterns[index]);
        if (status != QUANTAPDF_OK)
            return status;
        status = quantapdf_module_append(builder, 0, 1u);
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_append_code39_symbol(
        builder, QUANTAPDF_CODE39_ASTERISK);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_module_append(builder, 0, 10u);
}

static const uint8_t quantapdf_ean_l[] = {
    0x0du, 0x19u, 0x13u, 0x3du, 0x23u,
    0x31u, 0x2fu, 0x3bu, 0x37u, 0x0bu
};
static const uint8_t quantapdf_ean_g[] = {
    0x27u, 0x33u, 0x1bu, 0x21u, 0x1du,
    0x39u, 0x05u, 0x11u, 0x09u, 0x17u
};
static const uint8_t quantapdf_ean_r[] = {
    0x72u, 0x66u, 0x6cu, 0x42u, 0x5cu,
    0x4eu, 0x50u, 0x44u, 0x48u, 0x74u
};
static const char *const quantapdf_ean13_parity[] = {
    "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
    "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"
};

static int quantapdf_digits_valid(const char *payload, size_t length)
{
    size_t i;
    for (i = 0u; i < length; ++i) {
        if (payload[i] < '0' || payload[i] > '9')
            return 0;
    }
    return 1;
}

static int quantapdf_check_digit_valid(const char *payload, size_t length)
{
    unsigned int sum = 0u;
    size_t data_length;
    size_t distance;
    size_t i;

    if (length < 2u || !quantapdf_digits_valid(payload, length))
        return 0;
    data_length = length - 1u;
    for (i = data_length; i > 0u; --i) {
        distance = data_length - i;
        sum += (unsigned int)(payload[i - 1u] - '0') *
            ((distance & 1u) == 0u ? 3u : 1u);
    }
    return (unsigned int)(payload[length - 1u] - '0') ==
        (10u - (sum % 10u)) % 10u;
}

static quantapdf_status quantapdf_append_ean_digit(
    quantapdf_module_builder *builder,
    char digit,
    char set)
{
    unsigned int value = (unsigned int)(digit - '0');
    if (value > 9u)
        return QUANTAPDF_ERROR_FORMAT;
    if (set == 'L')
        return quantapdf_module_append_bits(
            builder, quantapdf_ean_l[value], 7u);
    if (set == 'G')
        return quantapdf_module_append_bits(
            builder, quantapdf_ean_g[value], 7u);
    if (set == 'R')
        return quantapdf_module_append_bits(
            builder, quantapdf_ean_r[value], 7u);
    return QUANTAPDF_ERROR_BACKEND;
}

static quantapdf_status quantapdf_encode_ean13(
    quantapdf_module_builder *builder,
    const char *payload,
    size_t length)
{
    const char *parity;
    quantapdf_status status;
    unsigned int lead;
    size_t i;

    if (length != 13u || !quantapdf_check_digit_valid(payload, length))
        return QUANTAPDF_ERROR_FORMAT;
    lead = (unsigned int)(payload[0] - '0');
    parity = quantapdf_ean13_parity[lead];

    status = quantapdf_module_append(builder, 0, 11u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 0u; i < 6u; ++i) {
        status = quantapdf_append_ean_digit(
            builder, payload[i + 1u], parity[i]);
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "01010");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 7u; i < 13u; ++i) {
        status = quantapdf_append_ean_digit(builder, payload[i], 'R');
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_module_append(builder, 0, 7u);
}

static quantapdf_status quantapdf_encode_upca(
    quantapdf_module_builder *builder,
    const char *payload,
    size_t length)
{
    quantapdf_status status;
    size_t i;

    if (length != 12u || !quantapdf_check_digit_valid(payload, length))
        return QUANTAPDF_ERROR_FORMAT;
    status = quantapdf_module_append(builder, 0, 9u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 0u; i < 6u; ++i) {
        status = quantapdf_append_ean_digit(builder, payload[i], 'L');
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "01010");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 6u; i < 12u; ++i) {
        status = quantapdf_append_ean_digit(builder, payload[i], 'R');
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_module_append(builder, 0, 9u);
}

static quantapdf_status quantapdf_encode_ean8(
    quantapdf_module_builder *builder,
    const char *payload,
    size_t length)
{
    quantapdf_status status;
    size_t i;

    if (length != 8u || !quantapdf_check_digit_valid(payload, length))
        return QUANTAPDF_ERROR_FORMAT;
    status = quantapdf_module_append(builder, 0, 7u);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 0u; i < 4u; ++i) {
        status = quantapdf_append_ean_digit(builder, payload[i], 'L');
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "01010");
    if (status != QUANTAPDF_OK)
        return status;
    for (i = 4u; i < 8u; ++i) {
        status = quantapdf_append_ean_digit(builder, payload[i], 'R');
        if (status != QUANTAPDF_OK)
            return status;
    }
    status = quantapdf_module_append_ascii_bits(builder, "101");
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_module_append(builder, 0, 7u);
}

static const uint8_t quantapdf_qr_ndata[] = {19u, 34u, 55u, 80u, 108u};
static const uint8_t quantapdf_qr_necc[] = {7u, 10u, 15u, 20u, 26u};

static uint8_t quantapdf_qr_gf_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0u;

    for (; b != 0u; b >>= 1u,
         a = (uint8_t)((a << 1u) ^ ((a & 0x80u) != 0u ? 0x1du : 0u))) {
        if ((b & 1u) != 0u)
            result ^= a;
    }
    return result;
}

static void quantapdf_qr_reed_solomon(
    const uint8_t *data,
    int data_count,
    uint8_t *ecc,
    int ecc_count)
{
    uint8_t generator[26] = {0};
    uint8_t root = 1u;
    int i;
    int j;

    generator[ecc_count - 1] = 1u;
    for (i = 0; i < ecc_count; ++i, root = quantapdf_qr_gf_mul(root, 2u)) {
        for (j = 0; j < ecc_count; ++j) {
            generator[j] =
                quantapdf_qr_gf_mul(generator[j], root) ^
                (j + 1 < ecc_count ? generator[j + 1] : 0u);
        }
    }

    memset(ecc, 0, (size_t)ecc_count);
    for (i = 0; i < data_count; ++i) {
        uint8_t factor = data[i] ^ ecc[0];
        memmove(ecc, ecc + 1, (size_t)ecc_count - 1u);
        ecc[ecc_count - 1] = 0u;
        for (j = 0; j < ecc_count; ++j)
            ecc[j] ^= quantapdf_qr_gf_mul(generator[j], factor);
    }
}

static int quantapdf_qr_dist(int x, int y)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    return ax > ay ? ax : ay;
}

static uint8_t quantapdf_qr_pattern(int modules, int x, int y)
{
    int alignment_distance =
        quantapdf_qr_dist(x - modules + 7, y - modules + 7);
    int k;

    for (k = 0; k < 3; ++k) {
        int distance = quantapdf_qr_dist(
            x - (k == 1 ? modules - 4 : 3),
            y - (k == 2 ? modules - 4 : 3));
        if (distance <= 4)
            return (uint8_t)(distance == 2 || distance == 4 ? 2 : 3);
    }
    if (modules > 21 && alignment_distance <= 2)
        return (uint8_t)(alignment_distance == 1 ? 2 : 3);
    if (x == 6 || y == 6)
        return (uint8_t)(2 | (~(x + y) & 1));
    return 0u;
}

static void quantapdf_qr_build(
    const unsigned char *payload,
    int length,
    int version,
    uint8_t grid[QUANTAPDF_QR_MAX_MODULES][QUANTAPDF_QR_MAX_MODULES])
{
    uint8_t codewords[135] = {0};
    int modules = 17 + 4 * version;
    int data_count = quantapdf_qr_ndata[version - 1];
    int position = 0;
    int i;
    int x;
    int y;

    codewords[0] = (uint8_t)(0x40u | ((unsigned int)length >> 4u));
    codewords[1] = (uint8_t)((unsigned int)length << 4u);
    for (i = 0; i < length; ++i) {
        uint8_t ch = payload[i];
        codewords[i + 1] |= (uint8_t)(ch >> 4u);
        codewords[i + 2] = (uint8_t)(ch << 4u);
    }
    for (i = length + 2; i < data_count; ++i)
        codewords[i] = ((i - length) & 1) != 0 ? 0x11u : 0xecu;
    quantapdf_qr_reed_solomon(
        codewords,
        data_count,
        &codewords[data_count],
        quantapdf_qr_necc[version - 1]);

    for (y = 0; y < modules; ++y) {
        for (x = 0; x < modules; ++x)
            grid[y][x] = quantapdf_qr_pattern(modules, x, y);
    }

    for (i = 0; i < 15; ++i) {
        uint8_t bit = (uint8_t)(2u | ((UINT32_C(0x77c4) >> i) & 1u));
        if (i < 8) {
            grid[i + (i > 5)][8] = bit;
            grid[8][modules - 1 - i] = bit;
        } else {
            grid[8][14 - i + (i == 8)] = bit;
            grid[modules - 15 + i][8] = bit;
        }
    }
    grid[modules - 8][8] = 3u;

    for (int right = modules - 1; right >= 1; right -= 2) {
        int vertical;
        if (right == 6)
            right = 5;
        for (vertical = 0; vertical < modules; ++vertical) {
            int column;
            for (column = 0; column < 2; ++column) {
                int px = right - column;
                int py =
                    ((right + 1) & 2) != 0 ? vertical : modules - 1 - vertical;
                if (grid[py][px] == 0u) {
                    grid[py][px] = (uint8_t)(
                        ((codewords[position / 8] >>
                          (7 - position % 8)) ^
                         ~(px + py)) &
                        1);
                    ++position;
                }
            }
        }
    }
}

static quantapdf_status quantapdf_draw_qr(
    quantapdf_composer *composer,
    size_t page_index,
    const char *payload,
    size_t length,
    const quantapdf_rect *bounds,
    uint32_t argb)
{
    uint8_t grid[QUANTAPDF_QR_MAX_MODULES][QUANTAPDF_QR_MAX_MODULES];
    quantapdf_path_builder path = {0};
    quantapdf_status status = QUANTAPDF_OK;
    float width = bounds->x1 - bounds->x0;
    float height = bounds->y1 - bounds->y0;
    float size = width < height ? width : height;
    float left;
    float top;
    float scale;
    int version;
    int modules;
    int y;

    if (length > 106u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!quantapdf_utf8_valid((const unsigned char *)payload, length))
        return QUANTAPDF_ERROR_FORMAT;

    for (version = 1; version <= 5; ++version) {
        if (length + 2u <= quantapdf_qr_ndata[version - 1])
            break;
    }
    if (version > 5)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    modules = 17 + 4 * version;
    scale = size / (float)(modules + 8);
    left = bounds->x0 + (width - size) / 2.0f;
    top = bounds->y0 + (height - size) / 2.0f;

    quantapdf_qr_build(
        (const unsigned char *)payload, (int)length, version, grid);
    for (y = 0; y < modules && status == QUANTAPDF_OK; ++y) {
        int x = 0;
        while (x < modules) {
            int start;
            while (x < modules && (grid[y][x] & 1u) == 0u)
                ++x;
            if (x >= modules)
                break;
            start = x;
            while (x < modules && (grid[y][x] & 1u) != 0u)
                ++x;
            status = quantapdf_path_add_rect(
                &path,
                left + (float)(start + 4) * scale,
                top + (float)(y + 4) * scale,
                left + (float)(x + 4) * scale,
                top + (float)(y + 5) * scale);
        }
    }
    if (status == QUANTAPDF_OK)
        status = quantapdf_draw_path_builder(
            composer, page_index, &path, argb);
    free(path.commands);
    return status;
}

quantapdf_status quantapdf_composer_draw_barcode(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_barcode_kind kind,
    const char *payload_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_barcode_options *options)
{
    quantapdf_module_builder modules = {0};
    quantapdf_status status;
    size_t length = 0u;

    if (composer == NULL || payload_utf8 == NULL ||
        !quantapdf_barcode_rect_valid(bounds) || options == NULL ||
        options->struct_size < QUANTAPDF_BARCODE_OPTIONS_V1_MIN_SIZE ||
        (options->argb >> 24u) != 0xffu)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (kind < QUANTAPDF_BARCODE_CODE_128B ||
        kind > QUANTAPDF_BARCODE_QR)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_payload_length(
        payload_utf8,
        kind == QUANTAPDF_BARCODE_QR
            ? (size_t)106u
            : QUANTAPDF_BARCODE_MAX_1D_PAYLOAD,
        &length);
    if (status != QUANTAPDF_OK)
        return status;

    if (kind == QUANTAPDF_BARCODE_QR)
        return quantapdf_draw_qr(
            composer, page_index, payload_utf8, length, bounds, options->argb);

    switch (kind) {
    case QUANTAPDF_BARCODE_CODE_128B:
        status = quantapdf_encode_code128b(&modules, payload_utf8, length);
        break;
    case QUANTAPDF_BARCODE_CODE_39:
        status = quantapdf_encode_code39(&modules, payload_utf8, length);
        break;
    case QUANTAPDF_BARCODE_EAN_13:
        status = quantapdf_encode_ean13(&modules, payload_utf8, length);
        break;
    case QUANTAPDF_BARCODE_UPC_A:
        status = quantapdf_encode_upca(&modules, payload_utf8, length);
        break;
    case QUANTAPDF_BARCODE_EAN_8:
        status = quantapdf_encode_ean8(&modules, payload_utf8, length);
        break;
    default:
        status = QUANTAPDF_ERROR_ARGUMENT;
        break;
    }

    if (status == QUANTAPDF_OK)
        status = quantapdf_draw_modules(
            composer, page_index, bounds, options->argb, &modules);
    free(modules.modules);
    return status;
}
