/*
 * Copyright (c) 2015 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * plugin.h: plugin handling
 */

#ifndef __included_plugin_h__
#define __included_plugin_h__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
  u8 *name;//配置信息
  u8 *filename;//插件文件路径
  struct stat file_info;
  void *handle;//插件so handle
} plugin_info_t;

typedef struct
{
  /* loaded plugin info */
  plugin_info_t *plugin_info;//各插件配置
  uword *plugin_by_name_hash;//hash表（通过名称查找插件，返回plugin_info_t类型）

  /* path and name filter */
  u8 *plugin_path;//插件地址
  u8 *plugin_name_filter;//需要过滤的插件配置

  /* convenience */
  vat_main_t *vat_main;

} plugin_main_t;

extern plugin_main_t vat_plugin_main;

int vat_plugin_init (vat_main_t * vam);
int vat_load_new_plugins (plugin_main_t * pm);

#endif

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
