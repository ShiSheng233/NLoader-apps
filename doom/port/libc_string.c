#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void *memcpy(void *destination, const void *source, size_t length)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    for (size_t i = 0; i < length; ++i)
        to[i] = from[i];
    return destination;
}
void *memmove(void *destination, const void *source, size_t length)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    if ((uintptr_t)to < (uintptr_t)from)
        return memcpy(destination, source, length);
    while (length)
    {
        --length;
        to[length] = from[length];
    }
    return destination;
}
void *memset(void *destination, int value, size_t length)
{
    unsigned char *to = destination;
    for (size_t i = 0; i < length; ++i)
        to[i] = (unsigned char)value;
    return destination;
}
int memcmp(const void *left, const void *right, size_t length)
{
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < length; ++i)
        if (a[i] != b[i])
            return (int)a[i] - b[i];
    return 0;
}
size_t strlen(const char *text)
{
    size_t n = 0;
    while (text[n])
        ++n;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && (unsigned char)*a == (unsigned char)*b)
    {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}
char *strncpy(char *to, const char *from, size_t length)
{
    size_t i = 0;
    for (; i < length && from[i]; ++i)
        to[i] = from[i];
    for (; i < length; ++i)
        to[i] = 0;
    return to;
}
char *strchr(const char *text, int character)
{
    for (;;)
    {
        if ((unsigned char)*text == (unsigned char)character)
            return (char *)text;
        if (!*text)
            return NULL;
        ++text;
    }
}
char *strrchr(const char *text, int character)
{
    const char *last = NULL;
    do
    {
        if ((unsigned char)*text == (unsigned char)character)
            last = text;
    } while (*text++);
    return (char *)last;
}
char *strstr(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (!length)
        return (char *)text;
    for (; *text; ++text)
        if (!strncmp(text, needle, length))
            return (char *)text;
    return NULL;
}
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isprint(int c) { return c >= 32 && c <= 126; }
int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
    {
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        int difference = tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]);
        if (difference || !a[i])
            return difference;
    }
    return 0;
}
int abs(int value) { return value < 0 ? -value : value; }
double fabs(double value) { return value < 0 ? -value : value; }
int atoi(const char *text)
{
    while (isspace((unsigned char)*text))
        ++text;
    int negative = *text == '-';
    if (*text == '-' || *text == '+')
        ++text;
    unsigned value = 0;
    while (isdigit((unsigned char)*text))
        value = value * 10u + (unsigned)(*text++ - '0');
    return negative ? (int)(0u - value) : (int)value;
}
double atof(const char *text)
{
    while (isspace((unsigned char)*text))
        ++text;
    int negative = *text == '-';
    if (*text == '-' || *text == '+')
        ++text;
    double value = 0;
    while (isdigit((unsigned char)*text))
        value = value * 10 + (*text++ - '0');
    if (*text == '.')
    {
        double scale = 0.1;
        ++text;
        while (isdigit((unsigned char)*text))
        {
            value += (*text++ - '0') * scale;
            scale *= 0.1;
        }
    }
    if (*text == 'e' || *text == 'E')
    {
        int exponent = atoi(text + 1);
        if (exponent > 308)
            exponent = 308;
        if (exponent < -308)
            exponent = -308;
        while (exponent > 0)
        {
            value *= 10;
            --exponent;
        }
        while (exponent < 0)
        {
            value /= 10;
            ++exponent;
        }
    }
    return negative ? -value : value;
}
