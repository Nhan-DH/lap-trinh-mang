#ifndef CRAWLER_INTERNAL_H
#define CRAWLER_INTERNAL_H

#include <stddef.h>

#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

#include "crawler.h"

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} string_list_t;

typedef struct {
    char *data;
    size_t length;
} download_buffer_t;

/**
 * @brief Giai phong moi chuoi va mang con tro cua danh sach.
 * @param[in,out] list Danh sach do module crawler quan ly.
 */
void free_string_list(string_list_t *list);

/**
 * @brief Them mot ban sao cua chuoi vao danh sach.
 * @param[in,out] list Danh sach nhan chuoi moi.
 * @param[in] value Chuoi can sao chep; ownership van thuoc caller.
 * @return 0 neu thanh cong; -1 neu cap phat bo nho that bai.
 */
int add_string(string_list_t *list, const char *value);

/**
 * @brief Tai toan bo noi dung HTML cua mot URL.
 * @param[in] url URL HTTPS can tai.
 * @param[out] buffer Buffer duoc cap phat; caller phai giai phong data.
 * @return 0 neu thanh cong; -1 neu curl loi, timeout hoac cap phat loi.
 */
int download_page(const char *url, download_buffer_t *buffer);

/**
 * @brief Duyet cay HTML va thu thap ba nhom du lieu.
 * @param[in] node Node bat dau duyet.
 * @param[in] base_url URL goc de resolve lien ket tuong doi.
 * @param[out] links Danh sach href cua the a.
 * @param[out] texts Danh sach text cua cac the noi dung.
 * @param[out] videos Danh sach src cua video/source/iframe.
 * @return 0 neu thanh cong; -1 neu cap phat chuoi that bai.
 */
int collect_nodes(xmlNode *node, const char *base_url,
                  string_list_t *links, string_list_t *texts,
                  string_list_t *videos);

/**
 * @brief Sap xep danh sach theo chu cai dau, khong phan biet hoa thuong.
 * @param[in,out] list Danh sach can sap xep tai cho.
 */
void sort_string_list(string_list_t *list);

/**
 * @brief Ghi danh sach thanh CSV voi cot order va cot du lieu.
 * @param[in] filename Ten file CSV can tao.
 * @param[in] header Ten cot du lieu.
 * @param[in] list Danh sach gia tri can ghi.
 * @return 0 neu ghi thanh cong; -1 neu khong mo duoc file.
 */
int write_csv(const char *filename, const char *header,
              const string_list_t *list);

#endif
