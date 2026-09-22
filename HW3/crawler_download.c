#include "crawler_internal.h"

#include <stdlib.h>
#include <string.h>

#define CRAWL_TIMEOUT_SEC 15L

/* Curl co the goi callback nhieu lan, nen buffer phai noi dung thay vi ghi de. */
static size_t write_download(void *contents, size_t size, size_t count,
                             void *user_data) {
    download_buffer_t *buffer = user_data;
    size_t received = size * count;
    char *new_data = realloc(buffer->data, buffer->length + received + 1);

    if (new_data == NULL) {
        return 0;
    }

    buffer->data = new_data;
    memcpy(buffer->data + buffer->length, contents, received);
    buffer->length += received;
    buffer->data[buffer->length] = '\0';
    return received;
}

/* Tai mot trang HTML va timeout de loi mang khong lam chuong trinh tre vo han. */
int download_page(const char *url, download_buffer_t *buffer) {
    CURL *curl = curl_easy_init();
    CURLcode result;

    if (curl == NULL) {
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CRAWL_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HW3-crawler/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_download);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || buffer->data == NULL) {
        free(buffer->data);
        buffer->data = NULL;
        return -1;
    }
    return 0;
}
