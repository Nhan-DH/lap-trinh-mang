#include "crawler_internal.h"

#include <ctype.h>
#include <libxml/HTMLtree.h>
#include <libxml/uri.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int is_tag(const xmlNode *node, const char *tag) {
    return node->name != NULL &&
           strcasecmp((const char *)node->name, tag) == 0;
}

static int is_text_element(const xmlNode *node) {
    const char *tags[] = {
        "title", "p", "h1", "h2", "h3", "h4", "h5", "h6", "li",
        "a", "yt-formatted-string"
    };

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (is_tag(node, tags[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_video_element(const xmlNode *node) {
    return is_tag(node, "video") || is_tag(node, "source") ||
           is_tag(node, "iframe");
}

static void trim_text(char *text) {
    char *start = text;
    char *end;

    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
}

static char *absolute_url(const char *value, const char *base_url) {
    xmlChar *resolved = xmlBuildURI((const xmlChar *)value,
                                    (const xmlChar *)base_url);
    char *url;

    if (resolved == NULL) {
        return NULL;
    }
    url = strdup((const char *)resolved);
    xmlFree(resolved);
    return url;
}

/* Chuyen URL tuong doi thanh URL day du de CSV co the dung doc lap. */
static int collect_url(xmlNode *node, const char *attribute_name,
                       const char *base_url, string_list_t *list) {
    xmlChar *attribute = xmlGetProp(node, (const xmlChar *)attribute_name);
    char *url;
    int result = 0;

    if (attribute == NULL) {
        return 0;
    }

    url = absolute_url((const char *)attribute, base_url);
    if (url != NULL) {
        result = add_string(list, url);
        free(url);
    }
    xmlFree(attribute);
    return result;
}

/* Duyet de quy vi HTML co the long nhieu cap element. */
int collect_nodes(xmlNode *node, const char *base_url,
                  string_list_t *links, string_list_t *texts,
                  string_list_t *videos) {
    for (xmlNode *current = node; current != NULL; current = current->next) {
        if (current->type == XML_ELEMENT_NODE) {
            if (is_tag(current, "a") &&
                collect_url(current, "href", base_url, links) != 0) {
                return -1;
            }
            if (is_video_element(current) &&
                collect_url(current, "src", base_url, videos) != 0) {
                return -1;
            }

            if (is_text_element(current)) {
                xmlChar *content = xmlNodeGetContent(current);
                if (content != NULL) {
                    trim_text((char *)content);
                    if (*content != '\0' &&
                        add_string(texts, (const char *)content) != 0) {
                        xmlFree(content);
                        return -1;
                    }
                    xmlFree(content);
                }
            }
        }

        if (collect_nodes(current->children, base_url, links, texts,
                          videos) != 0) {
            return -1;
        }
    }
    return 0;
}
