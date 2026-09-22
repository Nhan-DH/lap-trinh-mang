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

/* Ghi raw value de ket qua khop format mau: moi muc chiem mot dong. */
int write_csv(const char *filename, const string_list_t *list) {
    FILE *file = fopen(filename, "w");

    if (file == NULL) {
        return -1;
    }

    for (size_t i = 0; i < list->count; i++) {
        fprintf(file, "%s\n", list->items[i]);
    }

    fclose(file);
    return 0;
}
