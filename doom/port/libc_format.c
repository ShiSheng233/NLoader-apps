#include "doom_port.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The fixed MIT vendor exports names with a trailing underscore. Keeping
 * the wrapper separate avoids its public header's standard-name macros. */
extern int vsnprintf_(char *, size_t, const char *, va_list);

int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments)
{
    if (!format || (capacity && !buffer))
    {
        errno = EINVAL;
        return -1;
    }
    return vsnprintf_(buffer, capacity, format, arguments);
}
int snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}
int vfprintf(FILE *file, const char *format, va_list arguments)
{
    char buffer[256];
    int result = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (result < 0)
        return result;
    size_t length = (size_t)result < sizeof(buffer) ? (size_t)result : sizeof(buffer) - 1;
    if (fwrite(buffer, 1, length, file) != length)
        return -1;
    return result;
}
int fprintf(FILE *file, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(file, format, arguments);
    va_end(arguments);
    return result;
}
int vprintf(const char *format, va_list arguments) { return vfprintf(stdout, format, arguments); }
int printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}
void _putchar(char character)
{
    if (character)
        nlr_doom_log_bytes(&character, 1);
}

static int digit(int character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    character = tolower(character);
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

/* doomgeneric uses only the scalar integer conversions below. Other scanf
 * forms are refused explicitly; config-file scanning is disabled upstream. */
int sscanf(const char *text, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int matched = 0;
    while (*format)
    {
        if (isspace((unsigned char)*format))
        {
            while (isspace((unsigned char)*format))
                ++format;
            while (isspace((unsigned char)*text))
                ++text;
            continue;
        }
        if (*format != '%')
        {
            if (*text != *format)
                break;
            ++text;
            ++format;
            continue;
        }
        ++format;
        unsigned base = *format == 'x' || *format == 'X' ? 16u : *format == 'o' ? 8u : 10u;
        int automatic = *format == 'i';
        if (!automatic && *format != 'd' && *format != 'u' && *format != 'o' && *format != 'x' &&
            *format != 'X')
        {
            errno = ENOSYS;
            break;
        }
        ++format;
        while (isspace((unsigned char)*text))
            ++text;
        int negative = *text == '-';
        if (*text == '-' || *text == '+')
            ++text;
        if (automatic && *text == '0')
            base = 8;
        if ((automatic || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            text += 2;
        }
        uint32_t value = 0;
        int count = 0, next;
        while ((next = digit((unsigned char)*text)) >= 0 && (unsigned)next < base)
        {
            if (value > (UINT32_MAX - (unsigned)next) / base)
            {
                errno = ERANGE;
                va_end(arguments);
                return matched;
            }
            value = value * base + (unsigned)next;
            ++text;
            ++count;
        }
        if (!count)
            break;
        uint32_t *destination = va_arg(arguments, uint32_t *);
        *destination = negative ? 0u - value : value;
        ++matched;
    }
    va_end(arguments);
    return matched;
}
