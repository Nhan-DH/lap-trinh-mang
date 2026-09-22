#include "crawler_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static int first_letter_compare(const void *left, const void *right) {
    const char *first = *(const char *const *)left;
    const char *second = *(const char *const *)right;
    unsigned char first_letter = (unsigned char)tolower((unsigned char)*first);
    unsigned char second_letter =
        (unsigned char)tolower((unsigned char)*second);

    if (first_letter != second_letter) {
        return first_letter < second_letter ? -1 : 1;
    }
    return strcasecmp(first, second);
}

/* Sort theo ky tu dau, sau do dung ca chuoi de ket qua co thu tu on dinh. */
void sort_string_list(string_list_t *list) {
    qsort(list->items, list->count, sizeof(*list->items),
          first_letter_compare);
}

static void write_csv_value(FILE *file, const char *value) {
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '"') {
            fputc('"', file);
        }
        fputc(*cursor, file);
    }
}

/* Ghi CSV va escape dau nhay de text HTML khong lam hong cau truc cot. */
int write_csv(const char *filename, const char *header,
              const string_list_t *list) {
    FILE *file = fopen(filename, "w");

    if (file == NULL) {
        return -1;
    }

    fprintf(file, "order,%s\n", header);
    for (size_t i = 0; i < list->count; i++) {
        fprintf(file, "%zu,\"", i + 1);
        write_csv_value(file, list->items[i]);
        fprintf(file, "\"\n");
    }

    fclose(file);
    return 0;
}
