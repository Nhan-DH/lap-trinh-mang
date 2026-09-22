#include "crawler_internal.h"

#include <stdlib.h>
#include <string.h>

/* Giai phong ca mang con de tranh ro ri sau khi crawl that bai. */
void free_string_list(string_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
}

/* Mo rong theo cap so nhan doi de so lan realloc phu thuoc kich thuoc trang. */
int add_string(string_list_t *list, const char *value) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 32 : list->capacity * 2;
        char **new_items = realloc(list->items,
                                   new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return -1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count] = strdup(value);
    if (list->items[list->count] == NULL) {
        return -1;
    }
    list->count++;
    return 0;
}
