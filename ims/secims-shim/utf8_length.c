/*
 * utf8_length() as in Android 10 system/core/libutils/Unicode.cpp: the number of
 * bytes in a valid UTF-8 string, or -1 if it is not valid UTF-8.
 */

#include <stdint.h>
#include <sys/types.h>

static const uint32_t kUnicodeMaxCodepoint = 0x0010FFFF;

ssize_t utf8_length(const char *src)
{
    const char *cur = src;
    size_t ret = 0;
    while (*cur != '\0') {
        const char first_char = *cur++;
        if ((first_char & 0x80) == 0) { /* ASCII */
            ret += 1;
            continue;
        }
        /* (UTF-8's character must not be like 10xxxxxx,
         * but 110xxxxx, 1110xxxx, ... or 1111110x) */
        if ((first_char & 0x40) == 0) {
            return -1;
        }

        int32_t mask, to_ignore_mask;
        size_t num_to_read = 0;
        uint32_t utf32 = 0;
        for (num_to_read = 1, mask = 0x40, to_ignore_mask = 0x80;
             num_to_read < 5 && (first_char & mask);
             num_to_read++, to_ignore_mask |= mask, mask >>= 1) {
            if ((*cur & 0xC0) != 0x80) { /* must be 10xxxxxx */
                return -1;
            }
            /* 0x3F == 00111111 */
            utf32 = (utf32 << 6) + (*cur++ & 0x3F);
        }
        /* "first_char" must be (110xxxxx - 11110xxx) */
        if (num_to_read == 5) {
            return -1;
        }
        to_ignore_mask |= mask;
        utf32 |= ((~to_ignore_mask) & first_char) << (6 * (num_to_read - 1));
        if (utf32 > kUnicodeMaxCodepoint) {
            return -1;
        }

        ret += num_to_read;
    }
    return ret;
}
