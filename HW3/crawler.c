#include "crawler_internal.h"

#include <stdio.h>
#include <stdlib.h>

static int build_base_url(const char *domain, char *base_url,
                          size_t base_url_size) {
    int length = snprintf(base_url, base_url_size, "https://%s/", domain);
    return length >= 0 && (size_t)length < base_url_size ? 0 : -1;
}

/*
 * Tai mot trang, trich xuat ba loai du lieu va ghi ra ba CSV.
 *
 * @param domain Ten mien da duoc DNS xac nhan.
 * @return 0 neu hoan tat; -1 neu tai, phan tich hoac ghi file that bai.
 */
int crawl_website(const char *domain) {
    char base_url[512];
    download_buffer_t buffer = {0};
    string_list_t links = {0};
    string_list_t texts = {0};
    string_list_t videos = {0};
    htmlDocPtr document = NULL;
    int result = -1;

    if (build_base_url(domain, base_url, sizeof(base_url)) != 0) {
        return -1;
    }
    if (download_page(base_url, &buffer) != 0) {
        return -1;
    }

    document = htmlReadMemory(buffer.data, (int)buffer.length, base_url,
                             NULL, HTML_PARSE_RECOVER | HTML_PARSE_NONET);
    free(buffer.data);
    if (document == NULL) {
        return -1;
    }

    if (collect_nodes(xmlDocGetRootElement(document), base_url, &links,
                      &texts, &videos) != 0) {
        goto cleanup;
    }

    sort_string_list(&links);
    sort_string_list(&texts);
    sort_string_list(&videos);

    if (write_csv("links.csv", "url", &links) != 0 ||
        write_csv("texts.csv", "text", &texts) != 0 ||
        write_csv("videos.csv", "video_url", &videos) != 0) {
        goto cleanup;
    }
    result = 0;

cleanup:
    xmlFreeDoc(document);
    free_string_list(&links);
    free_string_list(&texts);
    free_string_list(&videos);
    return result;
}