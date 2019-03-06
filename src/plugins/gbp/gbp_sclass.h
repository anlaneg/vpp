/*
 * Copyright (c) 2018 Cisco and/or its affiliates.
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

#ifndef __GBP_SCLASS_H__
#define __GBP_SCLASS_H__

#include <plugins/gbp/gbp.h>

/**
 * Grouping of global data for the GBP source EPG classification feature
 */
typedef struct gbp_sclass_main_t_
{
  /**
   * Next nodes for L2 output features
   */
  u32 gel_l2_input_feat_next[32];
  u32 gel_l2_output_feat_next[32];
} gbp_sclass_main_t;

extern gbp_sclass_main_t gbp_sclass_main;

extern void gbp_sclass_enable_l2 (u32 sw_if_index);
extern void gbp_sclass_disable_l2 (u32 sw_if_index);
extern void gbp_sclass_enable_ip (u32 sw_if_index);
extern void gbp_sclass_disable_ip (u32 sw_if_index);

#endif

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
