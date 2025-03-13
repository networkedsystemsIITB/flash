#ifndef __FLASH_CFG_PARSER_H
#define __FLASH_CFG_PARSER_H

#include <flash_defines.h>
#include <cjson/cJSON.h>

struct NFGroup *parse_json(const char *filename);
void free_nf_group(struct NFGroup *nf_group);

#endif /* __FLASH_CFG_PARSER_H */
