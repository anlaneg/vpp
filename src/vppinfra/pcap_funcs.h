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

#ifndef included_vppinfra_pcap_funcs_h
#define included_vppinfra_pcap_funcs_h

/** Write out data to output file. */
clib_error_t *pcap_write (pcap_main_t * pm);

/** Read data from file. */
clib_error_t *pcap_read (pcap_main_t * pm);

/**
 * @brief Add packet
 *
 * @param *pm - pcap_main_t
 * @param time_now - f64
 * @param n_bytes_in_trace - u32
 * @param n_bytes_in_packet - u32
 *
 * @return Packet Data
 *
 */
static inline void *
pcap_add_packet (pcap_main_t * pm,
		 f64 time_now, u32 n_bytes_in_trace, u32 n_bytes_in_packet)
{
  pcap_packet_header_t *h;
  u8 *d;

  //在pm->pcap_data中开一个长为sizeof (h[0]) + n_bytes_in_trace的空间，并使d指向它
  vec_add2 (pm->pcap_data, d, sizeof (h[0]) + n_bytes_in_trace);
  //初始化packet header
  h = (void *) (d);
  h->time_in_sec = time_now;
  h->time_in_usec = 1e6 * (time_now - h->time_in_sec);
  //设置存储在文件中的报文长度及报文capture的实际长度
  h->n_packet_bytes_stored_in_file = n_bytes_in_trace;
  h->n_bytes_in_packet = n_bytes_in_packet;
  //报文数增加
  pm->n_packets_captured++;
  //返回待填充的报文头
  return h->data;
}

/**
 * @brief Add buffer (vlib_buffer_t) to the trace
 *
 * @param *pm - pcap_main_t
 * @param *vm - vlib_main_t
 * @param buffer_index - u32
 * @param n_bytes_in_trace - u32
 *
 */
//将报文buffer_index，添加到pm的capture缓冲中去（如果需要的话）
static inline void
pcap_add_buffer (pcap_main_t * pm,
		 struct vlib_main_t *vm, u32 buffer_index,
		 u32 n_bytes_in_trace)
{
  //由buffer_index转为buffer
  vlib_buffer_t *b = vlib_get_buffer (vm, buffer_index);

  //报取报文总长度
  u32 n = vlib_buffer_length_in_chain (vm, b);

  //n_bytes_in_trace是需要记录的字节长度，n是报文实际长度，取两者最小者
  i32 n_left = clib_min (n_bytes_in_trace, n);
  f64 time_now = vlib_time_now (vm);
  void *d;

  //如果已captured的报文总小于需要capture的报文，则执行报文capture
  if (PREDICT_TRUE (pm->n_packets_captured < pm->n_packets_to_capture))
    {
      clib_spinlock_lock_if_init (&pm->lock);

      //添加待写加入的报文（返回报文写入头）
      d = pcap_add_packet (pm, time_now, n_left, n);
      while (1)
	{
      //采用copy_length防止需要copy多片的情况
	  u32 copy_length = clib_min ((u32) n_left, b->current_length);
	  clib_memcpy_fast (d, b->data + b->current_data, copy_length);
	  n_left -= b->current_length;
	  if (n_left <= 0)
	    break;

	  //需要copy多片，切换到下一片
	  d += b->current_length;
	  ASSERT (b->flags & VLIB_BUFFER_NEXT_PRESENT);
	  b = vlib_get_buffer (vm, b->next_buffer);
	}
      clib_spinlock_unlock_if_init (&pm->lock);
    }
}

#endif /* included_vppinfra_pcap_funcs_h */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
