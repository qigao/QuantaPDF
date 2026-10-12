#include "core/input_file.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#endif

static quantapdf_status quantapdf_open_input(
    const char *filename,
    FILE **out_file)
{
    *out_file = NULL;
#if defined(_WIN32)
    FILE *file = NULL;
    wchar_t *wide_filename;
    size_t utf8_size;
    int wide_size;

    utf8_size = strlen(filename);
    if (utf8_size >= (size_t)INT_MAX)
        return QUANTAPDF_ERROR_IO;
    wide_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, NULL, 0);
    if (wide_size <= 0 ||
        (size_t)wide_size > SIZE_MAX / sizeof(*wide_filename))
        return QUANTAPDF_ERROR_IO;
    wide_filename = (wchar_t *)malloc(
        (size_t)wide_size * sizeof(*wide_filename));
    if (wide_filename == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            filename,
            -1,
            wide_filename,
            wide_size) != wide_size) {
        free(wide_filename);
        return QUANTAPDF_ERROR_IO;
    }
    if (_wfopen_s(&file, wide_filename, L"rb") != 0)
        file = NULL;
    free(wide_filename);
    if (file == NULL)
        return QUANTAPDF_ERROR_IO;
    *out_file = file;
    return QUANTAPDF_OK;
#else
    *out_file = fopen(filename, "rb");
    return *out_file != NULL ? QUANTAPDF_OK : QUANTAPDF_ERROR_IO;
#endif
}

quantapdf_status quantapdf_read_file(
    const char *filename,
    unsigned char **out_data,
    size_t *out_size)
{
    FILE *file;
    quantapdf_status status;
    unsigned char *data;
    uint64_t file_size;
#if defined(_WIN32)
    __int64 position;
#else
    off_t position;
#endif

    if (out_data == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = NULL;
    *out_size = 0;
    if (filename == NULL || filename[0] == '\0')
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_open_input(filename, &file);
    if (status != QUANTAPDF_OK)
        return status;
#if defined(_WIN32)
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return QUANTAPDF_ERROR_IO;
    }
    position = _ftelli64(file);
#else
    if (fseeko(file, 0, SEEK_END) != 0) {
        fclose(file);
        return QUANTAPDF_ERROR_IO;
    }
    position = ftello(file);
#endif
    if (position < 0) {
        fclose(file);
        return QUANTAPDF_ERROR_IO;
    }
    file_size = (uint64_t)position;
    if (file_size > (uint64_t)SIZE_MAX) {
        fclose(file);
        return QUANTAPDF_ERROR_NOMEM;
    }
#if defined(_WIN32)
    if (_fseeki64(file, 0, SEEK_SET) != 0) {
#else
    if (fseeko(file, 0, SEEK_SET) != 0) {
#endif
        fclose(file);
        return QUANTAPDF_ERROR_IO;
    }

    data = (unsigned char *)malloc(file_size == 0 ? 1 : (size_t)file_size);
    if (data == NULL) {
        fclose(file);
        return QUANTAPDF_ERROR_NOMEM;
    }
    if (file_size != 0 &&
        fread(data, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(data);
        fclose(file);
        return QUANTAPDF_ERROR_IO;
    }
    if (fclose(file) != 0) {
        free(data);
        return QUANTAPDF_ERROR_IO;
    }

    *out_data = data;
    *out_size = (size_t)file_size;
    return QUANTAPDF_OK;
}
