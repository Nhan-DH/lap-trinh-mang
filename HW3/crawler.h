#ifndef CRAWLER_H
#define CRAWLER_H

/**
 * @brief Tai trang web, thu thap link/text/video va ghi thanh CSV.
 *
 * HTML duoc tai qua HTTPS, phan tich, sap xep theo chu cai dau roi ghi vao
 * links.csv, texts.csv va videos.csv.
 *
 * @param[in] domain Ten mien da duoc DNS xac nhan, vi du "www.youtube.com".
 * @return 0 neu thanh cong; -1 neu tai, phan tich hoac ghi file that bai.
 * @note Timeout cua libcurl ngan viec loi mang lam chuong trinh cho vo han.
 *       Ham tu giai phong buffer, XML document va cac danh sach noi bo.
 */
int crawl_website(const char *domain);

#endif