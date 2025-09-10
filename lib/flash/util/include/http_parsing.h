/*
 * mTCP source code is distributed under the Modified BSD Licence.
 *
 * Copyright (C) 2015 EunYoung Jeong, Shinae Woo, Muhammad Jamshed, Haewon Jeong, 
 *                    Sunghwan Ihm, Dongsu Han, KyoungSoo Park
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __NRE_HTTP_PARSING
#define __NRE_HTTP_PARSING

#define HTTP_STR "HTTP"
#define HTTPV0_STR "HTTP/1.0"
#define HTTPV1_STR "HTTP/1.1"
#define HTTP_GET "GET"
#define HTTP_POST "POST"
#define HTTP_CLOSE "Close"
#define HTTP_KEEP_ALIVE "Keep-Alive"
#define HOST_HDR "\nHost:"
#define CONTENT_LENGTH_HDR "\nContent-Length:"
#define CONTENT_TYPE_HDR "\nContent-Type:"
#define CACHE_CONTROL_HDR "\nCache-Control:"
#define CONNECTION_HDR "\nConnection:"
#define DATE_HDR "\nDate:"
#define EXPIRES_HDR "\nExpires:"
#define AGE_HDR "\nAge:"
#define LAST_MODIFIED_HDR "\nLast-Modified:"
#define IF_MODIFIED_SINCE_HDR "\nIf-Modified_Since:"
#define PRAGMA_HDR "\nPragma:"
#define RANGE_HDR "\nRange:"
#define IF_RANGE_HDR "\nIf-Range:"
#define ETAG_HDR "\nETag:"

enum { GET = 1, POST };

int find_http_header(char *data, int len);
int is_http_response(char *data, int len);
int is_http_request(char *data, int len);

char *http_header_str_val(const char *buf, const char *key, const int key_len, char *value, int value_len);
long int http_header_long_val(const char *buf, const char *key, int key_len);

char *http_get_http_version_resp(char *data, int len, char *value, int value_len);
char *http_get_url(char *data, int data_len, char *value, int value_len);
int http_get_status_code(void *response);
int http_get_maxage(char *cache_ctl, int len);

time_t http_header_date(const char *data, const char *field, int len);

enum { HTTP_09, HTTP_10, HTTP_11 }; /* http version */
int http_parse_first_resp_line(const char *data, int len, int *scode, int *ver);
int http_check_header_field(const char *data, const char *field);

#endif
