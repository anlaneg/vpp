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
 * node_funcs.h: processing nodes global functions/inlines
 *
 * Copyright (c) 2008 Eliot Dresselhaus
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** \file
    vlib node functions
*/


#ifndef included_vlib_node_funcs_h
#define included_vlib_node_funcs_h

#include <vppinfra/fifo.h>
#include <vppinfra/tw_timer_1t_3w_1024sl_ov.h>

/** \brief Get vlib node by index.
 @warning This function will ASSERT if @c i is out of range.
 @param vm vlib_main_t pointer, varies by thread
 @param i node index.
 @return pointer to the requested vlib_node_t.
*/
//按索引取node
always_inline vlib_node_t *
vlib_get_node (vlib_main_t * vm, u32 i)
{
  return vec_elt (vm->node_main.nodes, i);
}

/** \brief Get vlib node by graph arc (next) index.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of original node
 @param next_index graph arc index
 @return pointer to the vlib_node_t at the end of the indicated arc
*/
//按索引取node_index的对应next_node
always_inline vlib_node_t *
vlib_get_next_node (vlib_main_t * vm, u32 node_index, u32 next_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n;

  n = vec_elt (nm->nodes, node_index);
  ASSERT (next_index < vec_len (n->next_nodes));
  return vlib_get_node (vm, n->next_nodes[next_index]);
}

/** \brief Get node runtime by node index.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of node
 @return pointer to the indicated vlib_node_runtime_t
*/
//给定node_index获得其对应的node_runtime
always_inline vlib_node_runtime_t *
vlib_node_get_runtime (vlib_main_t * vm, u32 node_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vec_elt (nm->nodes, node_index);
  vlib_process_t *p;
  if (n->type != VLIB_NODE_TYPE_PROCESS)
    //非process情况，通过runtime_index获得
    return vec_elt_at_index (nm->nodes_by_type[n->type], n->runtime_index);
  else
    {
      p = vec_elt (nm->processes, n->runtime_index);
      //process通过process内部结束
      return &p->node_runtime;
    }
}

/** \brief Get node runtime private data by node index.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of the node
 @return pointer to the indicated vlib_node_runtime_t private data
*/
//给定node_index获取其对应的runtime_data
always_inline void *
vlib_node_get_runtime_data (vlib_main_t * vm, u32 node_index)
{
  vlib_node_runtime_t *r = vlib_node_get_runtime (vm, node_index);
  return r->runtime_data;
}

/** \brief Set node runtime private data.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of the node
 @param runtime_data arbitrary runtime private data
 @param n_runtime_data_bytes size of runtime private data
*/
//给定node_index设置其对应的runtime_data
always_inline void
vlib_node_set_runtime_data (vlib_main_t * vm, u32 node_index,
			    void *runtime_data, u32 n_runtime_data_bytes)
{
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_node_runtime_t *r = vlib_node_get_runtime (vm, node_index);

  n->runtime_data_bytes = n_runtime_data_bytes;
  vec_free (n->runtime_data);
  vec_add (n->runtime_data, runtime_data, n_runtime_data_bytes);

  ASSERT (vec_len (n->runtime_data) <= sizeof (vlib_node_runtime_t) -
	  STRUCT_OFFSET_OF (vlib_node_runtime_t, runtime_data));

  if (vec_len (n->runtime_data) > 0)
    clib_memcpy_fast (r->runtime_data, n->runtime_data,
		      vec_len (n->runtime_data));
}

/** \brief Set node dispatch state.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of the node
 @param new_state new state for node, see vlib_node_state_t
*/
//更新指定node的状态
always_inline void
vlib_node_set_state (vlib_main_t * vm, u32 node_index,
		     vlib_node_state_t new_state)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n;
  vlib_node_runtime_t *r;

  n = vec_elt (nm->nodes, node_index);
  if (n->type == VLIB_NODE_TYPE_PROCESS)
    {
      //设置process的状态
      vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);
      r = &p->node_runtime;

      /* When disabling make sure flags are cleared. */
      //清除waiting,pending标记
      p->flags &= ~(VLIB_PROCESS_RESUME_PENDING
		    | VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_CLOCK
		    | VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT);
    }
  else
    r = vec_elt_at_index (nm->nodes_by_type[n->type], n->runtime_index);

  ASSERT (new_state < VLIB_N_NODE_STATE);

  if (n->type == VLIB_NODE_TYPE_INPUT)
    {
      ASSERT (nm->input_node_counts_by_state[n->state] > 0);
      nm->input_node_counts_by_state[n->state] -= 1;
      nm->input_node_counts_by_state[new_state] += 1;
    }

  //更新node,runtime对应的状态
  n->state = new_state;
  r->state = new_state;
}

/** \brief Get node dispatch state.
 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of the node
 @return state for node, see vlib_node_state_t
*/
//获取指定node的状态
always_inline vlib_node_state_t
vlib_node_get_state (vlib_main_t * vm, u32 node_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n;
  n = vec_elt (nm->nodes, node_index);
  return n->state;
}

//将input类型的node引入到中断未决集合中
always_inline void
vlib_node_set_interrupt_pending (vlib_main_t * vm, u32 node_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vec_elt (nm->nodes, node_index);
  ASSERT (n->type == VLIB_NODE_TYPE_INPUT);
  clib_spinlock_lock_if_init (&nm->pending_interrupt_lock);
  vec_add1 (nm->pending_interrupt_node_runtime_indices, n->runtime_index);
  clib_spinlock_unlock_if_init (&nm->pending_interrupt_lock);
}

//通过node取其对应的process
always_inline vlib_process_t *
vlib_get_process_from_node (vlib_main_t * vm, vlib_node_t * node)
{
  vlib_node_main_t *nm = &vm->node_main;
  ASSERT (node->type == VLIB_NODE_TYPE_PROCESS);
  return vec_elt (nm->processes, node->runtime_index);
}

/* Fetches frame with given handle. */
//自heap_aligned_base上合上frame_index就可以得到frame
always_inline vlib_frame_t *
vlib_get_frame_no_check (vlib_main_t * vm, uword frame_index)
{
  vlib_frame_t *f;
  f = vm->heap_aligned_base + (frame_index * VLIB_FRAME_ALIGN);
  return f;
}

//获取f对应的frame index
always_inline u32
vlib_frame_index_no_check (vlib_main_t * vm, vlib_frame_t * f)
{
  uword i;

  ASSERT (((uword) f & (VLIB_FRAME_ALIGN - 1)) == 0);

  //记算偏移量
  i = ((u8 *) f - (u8 *) vm->heap_aligned_base);
  ASSERT ((i / VLIB_FRAME_ALIGN) <= 0xFFFFFFFFULL);

  //按VLIB_FRAME_ALIGN取frame index
  return i / VLIB_FRAME_ALIGN;
}

//通过frame index获得frame
always_inline vlib_frame_t *
vlib_get_frame (vlib_main_t * vm, uword frame_index)
{
  vlib_frame_t *f = vlib_get_frame_no_check (vm, frame_index);
  ASSERT (f->frame_flags & VLIB_FRAME_IS_ALLOCATED);
  return f;
}

always_inline void
vlib_frame_no_append (vlib_frame_t * f)
{
  f->frame_flags |= VLIB_FRAME_NO_APPEND;
}

//将f转换为frame index
always_inline u32
vlib_frame_index (vlib_main_t * vm, vlib_frame_t * f)
{
  uword i = vlib_frame_index_no_check (vm, f);
  ASSERT (vlib_get_frame (vm, i) == f);
  return i;
}

/* Byte alignment for vector arguments. */
#define VLIB_FRAME_VECTOR_ALIGN (1 << 4)

//获取vlib_frame_t后面添加scalar_size并对齐所需要的大小
always_inline u32
vlib_frame_vector_byte_offset (u32 scalar_size)
{
    //标量大小＋sizeof(vlib_frame_t)后接VLIB_FRAME_VECTOR_ALIGN对齐
  return round_pow2 (sizeof (vlib_frame_t) + scalar_size,
		     VLIB_FRAME_VECTOR_ALIGN);
}

/** \brief Get pointer to frame vector data.
 @param f vlib_frame_t pointer
 @return pointer to first vector element in frame
*/
always_inline void *
vlib_frame_vector_args (vlib_frame_t * f)
{
    //取出vlib_frame对应的scalar_size
  return (void *) f + vlib_frame_vector_byte_offset (f->scalar_size);
}

/** \brief Get pointer to frame scalar data.

 @param f vlib_frame_t pointer

 @return arbitrary node scalar data

 @sa vlib_frame_vector_args
*/
always_inline void *
vlib_frame_scalar_args (vlib_frame_t * f)
{
  return vlib_frame_vector_args (f) - f->scalar_size;
}

//取出n节点下要送给next_node_index对应的next_frame结构
always_inline vlib_next_frame_t *
vlib_node_runtime_get_next_frame (vlib_main_t * vm,
				  vlib_node_runtime_t * n, u32 next_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_next_frame_t *nf;

  ASSERT (next_index < n->n_next_nodes);

  //取出n节点对应的next_frame,再取出next_index（即下级node的索引）获得下级node对应的next_frame
  nf = vec_elt_at_index (nm->next_frames, n->next_frame_index + next_index);

  if (CLIB_DEBUG > 0)
    {
      vlib_node_t *node, *next;
      //指明的node
      node = vec_elt (nm->nodes, n->node_index);
      //取此node对应的下一级node
      next = vec_elt (nm->nodes, node->next_nodes[next_index]);
      //nf指出的node_index一定等于next的index
      ASSERT (nf->node_runtime_index == next->runtime_index);
    }

  return nf;
}

/** \brief Get pointer to frame by (@c node_index, @c next_index).

 @warning This is not a function that you should call directly.
 See @ref vlib_get_next_frame instead.

 @param vm vlib_main_t pointer, varies by thread
 @param node_index index of the node
 @param next_index graph arc index

 @return pointer to the requested vlib_next_frame_t

 @sa vlib_get_next_frame
*/

always_inline vlib_next_frame_t *
vlib_node_get_next_frame (vlib_main_t * vm, u32 node_index, u32 next_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n;
  vlib_node_runtime_t *r;

  n = vec_elt (nm->nodes, node_index);
  r = vec_elt_at_index (nm->nodes_by_type[n->type], n->runtime_index);
  return vlib_node_runtime_get_next_frame (vm, r, next_index);
}

vlib_frame_t *vlib_get_next_frame_internal (vlib_main_t * vm,
					    vlib_node_runtime_t * node,
					    u32 next_index,
					    u32 alloc_new_frame);

//申请或者获得next_index可用的frame,获得此frame可使用的vector指针及可存放的大小
#define vlib_get_next_frame_macro(vm,node,next_index,vectors,n_vectors_left,alloc_new_frame) \
do {									\
  /*取出或者申请next_index可用的frame*/\
  vlib_frame_t * _f							\
    = vlib_get_next_frame_internal ((vm), (node), (next_index),		\
				    (alloc_new_frame)/*如果必要就申请new_frame*/);			\
  /*取出frame中已用空间大小*/\
  u32 _n = _f->n_vectors;						\
  /*获得此frame可使用的vector指针*/\
  (vectors) = vlib_frame_vector_args (_f) + _n * sizeof ((vectors)[0]); \
  /*指出此frame中可存放的大小*/\
  (n_vectors_left) = VLIB_FRAME_SIZE - _n;				\
} while (0)


/** \brief Get pointer to next frame vector data by
    (@c vlib_node_runtime_t, @c next_index).
 Standard single/dual loop boilerplate element.
 @attention This is a MACRO, with SIDE EFFECTS.

 @param vm vlib_main_t pointer, varies by thread
 @param node current node vlib_node_runtime_t pointer
 @param next_index requested graph arc index

 @return @c vectors -- pointer to next available vector slot
 @return @c n_vectors_left -- number of vector slots available
*/
//获得可供next_index用来存放报文的vectors及vector可存放的报文数(不容许申请新的next frame)
#define vlib_get_next_frame(vm,node,next_index,vectors/*出参，返回可填充的指针*/,n_vectors_left/*出参，有多少空间可填充*/)	\
  vlib_get_next_frame_macro (vm, node, next_index,			\
			     vectors, n_vectors_left,			\
			     /* alloc new frame */ 0)

#define vlib_get_new_next_frame(vm,node,next_index,vectors,n_vectors_left) \
  vlib_get_next_frame_macro (vm, node, next_index,			\
			     vectors, n_vectors_left,			\
			     /* alloc new frame */ 1)
/** \brief Release pointer to next frame vector data.
 Standard single/dual loop boilerplate element.
 @param vm vlib_main_t pointer, varies by thread
 @param r current node vlib_node_runtime_t pointer
 @param next_index graph arc index
 @param n_packets_left number of slots still available in vector
*/
void
vlib_put_next_frame (vlib_main_t * vm,
		     vlib_node_runtime_t * r,
		     u32 next_index, u32 n_packets_left);

/* Combination get plus put.  Returns vector argument just added. */
#define vlib_set_next_frame(vm,node,next_index,v)			\
({									\
  uword _n_left;							\
  /*取出可供next_index使用的vector及可使用的大小*/\
  vlib_get_next_frame ((vm), (node), (next_index), (v), _n_left);	\
  ASSERT (_n_left > 0);							\
  /*如果vector中已有元素，则将其加入到pending_frame队列*/\
  vlib_put_next_frame ((vm), (node), (next_index), _n_left - 1);	\
  /*返回可填充的vector*/\
  (v);									\
})

//获取vector,并将报文加入（如果之前已有报文，则将vector加入到pending_frame队列上）
always_inline void
vlib_set_next_frame_buffer (vlib_main_t * vm,
			    vlib_node_runtime_t * node,
			    u32 next_index, u32 buffer_index)
{
  u32 *p;
  //获取node的next_index使用的next_frame,并为其append报文buffer_index
  p = vlib_set_next_frame (vm, node, next_index, p);
  p[0] = buffer_index;
}

vlib_frame_t *vlib_get_frame_to_node (vlib_main_t * vm, u32 to_node_index);
void vlib_put_frame_to_node (vlib_main_t * vm, u32 to_node_index,
			     vlib_frame_t * f);

//检查当前是否处理process上下文
always_inline uword
vlib_in_process_context (vlib_main_t * vm)
{
  return vm->node_main.current_process_index != ~0;
}

//如果当前处理process上下文，则返回当前正在运行的进程
always_inline vlib_process_t *
vlib_get_current_process (vlib_main_t * vm)
{
  vlib_node_main_t *nm = &vm->node_main;
  if (vlib_in_process_context (vm))
    return vec_elt (nm->processes, nm->current_process_index);
  return 0;
}

always_inline uword
vlib_current_process (vlib_main_t * vm)
{
  return vlib_get_current_process (vm)->node_runtime.node_index;
}

/** Returns TRUE if a process suspend time is less than 10us
    @param dt - remaining poll time in seconds
    @returns 1 if dt < 10e-6, 0 otherwise
*/
always_inline uword
vlib_process_suspend_time_is_zero (f64 dt)
{
  return dt < 10e-6;
}

/** Suspend a vlib cooperative multi-tasking thread for a period of time
    @param vm - vlib_main_t *
    @param dt - suspend interval in seconds
    @returns VLIB_PROCESS_RESUME_LONGJMP_RESUME, routinely ignored
*/

always_inline uword
vlib_process_suspend (vlib_main_t * vm, f64 dt)
{
  uword r;
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p = vec_elt (nm->processes, nm->current_process_index);

  if (vlib_process_suspend_time_is_zero (dt))
    return VLIB_PROCESS_RESUME_LONGJMP_RESUME;

  //保存恢复点，如果以resume_longjmp跳转到此处，则会导致？？？
  p->flags |= VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_CLOCK;
  r = clib_setjmp (&p->resume_longjmp, VLIB_PROCESS_RESUME_LONGJMP_SUSPEND);
  if (r == VLIB_PROCESS_RESUME_LONGJMP_SUSPEND)
    {
      /* expiration time in 10us ticks */
      p->resume_clock_interval = dt * 1e5;
      clib_longjmp (&p->return_longjmp, VLIB_PROCESS_RETURN_LONGJMP_SUSPEND);
    }

  return r;
}

always_inline void
vlib_process_free_event_type (vlib_process_t * p, uword t,
			      uword is_one_time_event)
{
  ASSERT (!pool_is_free_index (p->event_type_pool, t));
  pool_put_index (p->event_type_pool, t);
  if (is_one_time_event)
    //丢掉标记位
    p->one_time_event_type_bitmap =
      clib_bitmap_andnoti (p->one_time_event_type_bitmap, t);
}

//如果t指出的event是单次event,则归还event type占用的空间
always_inline void
vlib_process_maybe_free_event_type (vlib_process_t * p, uword t)
{
    //t event类型一定不为空
  ASSERT (!pool_is_free_index (p->event_type_pool, t));
  //如果t指出的event是单次event,则归还event type占用的空间
  if (clib_bitmap_get (p->one_time_event_type_bitmap, t))
    vlib_process_free_event_type (p, t, /* is_one_time_event */ 1);
}

//提取当前process收到的一个事件（return_event_type_opaque标记）及其相关的event data
//如果未收到事件，返回0
always_inline void *
vlib_process_get_event_data (vlib_main_t * vm,
			     uword * return_event_type_opaque)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  vlib_process_event_type_t *et;
  uword t;
  void *event_data_vector;

  //取当前正在运行的process
  p = vec_elt (nm->processes, nm->current_process_index);

  /* Find first type with events ready.
     Return invalid type when there's nothing there. */
  //提取首个被bit设置的位编号(即此process发生的首个事件）
  t = clib_bitmap_first_set (p->non_empty_event_type_bitmap);
  if (t == ~0)
    return 0;

  //将t　bit位置为０（事件已提取）
  p->non_empty_event_type_bitmap =
    clib_bitmap_andnoti (p->non_empty_event_type_bitmap, t);

  //取t号事件对应的data
  ASSERT (_vec_len (p->pending_event_data_by_type_index[t]) > 0);
  event_data_vector = p->pending_event_data_by_type_index[t];
  p->pending_event_data_by_type_index[t] = 0;//标明此事件数据已提取

  //返回事件类型
  et = pool_elt_at_index (p->event_type_pool, t);

  /* Return user's opaque value and possibly index. */
  *return_event_type_opaque = et->opaque;

  vlib_process_maybe_free_event_type (p, t);

  //event对应的data
  return event_data_vector;
}

/* Return event data vector for later reuse.  We reuse event data to avoid
   repeatedly allocating event vectors in cases where we care about speed. */
always_inline void
vlib_process_put_event_data (vlib_main_t * vm, void *event_data)
{
  vlib_node_main_t *nm = &vm->node_main;
  vec_add1 (nm->recycled_event_data_vectors, event_data);
}

/** Return the first event type which has occurred and a vector of per-event
    data of that type, or a timeout indication

    @param vm - vlib_main_t pointer
    @param data_vector - pointer to a (uword *) vector to receive event data
    @returns either an event type and a vector of per-event instance data,
    or ~0 to indicate a timeout.
*/

//如果没有任何事件发生，返回~0
//如果有event发生，返回event对应的event type,通过data_vector返回event对应的event data
always_inline uword
vlib_process_get_events (vlib_main_t * vm, uword ** data_vector)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  vlib_process_event_type_t *et;
  uword r, t, l;

  p = vec_elt (nm->processes, nm->current_process_index);

  /* Find first type with events ready.
     Return invalid type when there's nothing there. */
  t = clib_bitmap_first_set (p->non_empty_event_type_bitmap);
  if (t == ~0)
    return t;//没有任何事件，返回～0

  //移除首个event
  p->non_empty_event_type_bitmap =
    clib_bitmap_andnoti (p->non_empty_event_type_bitmap, t);

  //取出首个event 对应的event data
  l = _vec_len (p->pending_event_data_by_type_index[t]);
  if (data_vector)
    vec_add (*data_vector, p->pending_event_data_by_type_index[t], l);
  _vec_len (p->pending_event_data_by_type_index[t]) = 0;

  //获得event type
  et = pool_elt_at_index (p->event_type_pool, t);

  /* Return user's opaque value. */
  r = et->opaque;

  //归还t占用的空间
  vlib_process_maybe_free_event_type (p, t);

  return r;
}

//获得t号event对应的event data,返回event data对应的长度
always_inline uword
vlib_process_get_events_helper (vlib_process_t * p, uword t,
				uword ** data_vector)
{
  uword l;

  //清掉t号event
  p->non_empty_event_type_bitmap =
    clib_bitmap_andnoti (p->non_empty_event_type_bitmap, t);

  //取出t号event对应的data
  l = _vec_len (p->pending_event_data_by_type_index[t]);
  if (data_vector)
    vec_add (*data_vector, p->pending_event_data_by_type_index[t], l);
  _vec_len (p->pending_event_data_by_type_index[t]) = 0;

  //如有必要归还t号event对应的type
  vlib_process_maybe_free_event_type (p, t);

  return l;
}

/* As above but query as specified type of event.  Returns number of
   events found. */
//通过event type查找event index,并依据event index取出 event data,返回event data对应的length
always_inline uword
vlib_process_get_events_with_type (vlib_main_t * vm, uword ** data_vector,
				   uword with_type_opaque)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  uword t, *h;

  //通过event type查找event index,并依据event index取出 event data,返回event data对应的length
  p = vec_elt (nm->processes, nm->current_process_index);
  h = hash_get (p->event_type_index_by_type_opaque, with_type_opaque);
  if (!h)
    /* This can happen when an event has not yet been
       signaled with given opaque type. */
    return 0;

  t = h[0];
  if (!clib_bitmap_get (p->non_empty_event_type_bitmap, t))
    return 0;

  //取t号event对应的event data,返回event对应的长度
  return vlib_process_get_events_helper (p, t, data_vector);
}

//等待一个event发生,如果有event发生，则立即返回，否则suspend process
always_inline uword *
vlib_process_wait_for_event (vlib_main_t * vm)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  uword r;

  p = vec_elt (nm->processes, nm->current_process_index);
  if (clib_bitmap_is_zero (p->non_empty_event_type_bitmap))
    {
      //当bitmap为空时，说明没有事件发生，故直接跳至suspend位置，执行挂起
      p->flags |= VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT;
      r =
	clib_setjmp (&p->resume_longjmp, VLIB_PROCESS_RESUME_LONGJMP_SUSPEND);
      if (r == VLIB_PROCESS_RESUME_LONGJMP_SUSPEND)
	clib_longjmp (&p->return_longjmp,
		      VLIB_PROCESS_RETURN_LONGJMP_SUSPEND);
    }

  //否则有事件发生，返回发生的事件bitmap
  return p->non_empty_event_type_bitmap;
}

//指定event id 获取发生的event,如果未有event发生，则执行process suspend，
//否则已发生的event data的长度及event data
always_inline uword
vlib_process_wait_for_one_time_event (vlib_main_t * vm,
				      uword ** data_vector,
				      uword with_type_index)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  uword r;

  p = vec_elt (nm->processes, nm->current_process_index);
  ASSERT (!pool_is_free_index (p->event_type_pool, with_type_index));
  while (!clib_bitmap_get (p->non_empty_event_type_bitmap, with_type_index))
    {
      //指定索引的bitmap没有被发现，执行suspend
      p->flags |= VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT;
      r =
	clib_setjmp (&p->resume_longjmp, VLIB_PROCESS_RESUME_LONGJMP_SUSPEND);
      if (r == VLIB_PROCESS_RESUME_LONGJMP_SUSPEND)
	clib_longjmp (&p->return_longjmp,
		      VLIB_PROCESS_RETURN_LONGJMP_SUSPEND);
    }

  return vlib_process_get_events_helper (p, with_type_index, data_vector);
}

//使当前process指定evnet type查找一个已发生的event,如果未有event发生，则执行process suspend
//否则返回event data及event data length
always_inline uword
vlib_process_wait_for_event_with_type (vlib_main_t * vm,
				       uword ** data_vector,
				       uword with_type_opaque)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  uword r, *h;

  p = vec_elt (nm->processes, nm->current_process_index);
  h = hash_get (p->event_type_index_by_type_opaque, with_type_opaque);
  while (!h || !clib_bitmap_get (p->non_empty_event_type_bitmap, h[0]))
    {
      //等待的事件没有发生，保存恢复点，执行suspend
      p->flags |= VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT;
      r =
	clib_setjmp (&p->resume_longjmp, VLIB_PROCESS_RESUME_LONGJMP_SUSPEND);
      if (r == VLIB_PROCESS_RESUME_LONGJMP_SUSPEND)
	clib_longjmp (&p->return_longjmp,
		      VLIB_PROCESS_RETURN_LONGJMP_SUSPEND);

      /* See if unknown event type has been signaled now. */
      if (!h)
	h = hash_get (p->event_type_index_by_type_opaque, with_type_opaque);
    }

  return vlib_process_get_events_helper (p, h[0], data_vector);
}

/** Suspend a cooperative multi-tasking thread
    Waits for an event, or for the indicated number of seconds to elapse
    @param vm - vlib_main_t pointer
    @param dt - timeout, in seconds.
    @returns the remaining time interval
*/
//使当前process等待一个event或者clock,等待dt时间,如果有event发生，则返回dt
//否则执行process suspend
always_inline f64
vlib_process_wait_for_event_or_clock (vlib_main_t * vm, f64 dt)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_process_t *p;
  f64 wakeup_time;
  uword r;

  p = vec_elt (nm->processes, nm->current_process_index);

  if (vlib_process_suspend_time_is_zero (dt)
      || !clib_bitmap_is_zero (p->non_empty_event_type_bitmap))
    return dt;

  //计算wakeup的时间
  wakeup_time = vlib_time_now (vm) + dt;

  /* Suspend waiting for both clock and event to occur. */
  //指明进程收到event或者clock到期时，则可以唤醒
  p->flags |= (VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT
	       | VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_CLOCK);

  //记录process的恢复点，并suspend
  r = clib_setjmp (&p->resume_longjmp, VLIB_PROCESS_RESUME_LONGJMP_SUSPEND);
  if (r == VLIB_PROCESS_RESUME_LONGJMP_SUSPEND)
    {
      p->resume_clock_interval = dt * 1e5;
      //执行挂起
      clib_longjmp (&p->return_longjmp, VLIB_PROCESS_RETURN_LONGJMP_SUSPEND);
    }

  /* Return amount of time still left to sleep.
     If <= 0 then we've been waken up by the clock (and not an event). */
  //此时此process收到了event或者clock到期，计算是否满足sleep要求，返回>0不满足，否则满足
  return wakeup_time - vlib_time_now (vm);
}

//分配一个process event type结构
always_inline vlib_process_event_type_t *
vlib_process_new_event_type (vlib_process_t * p, uword with_type_opaque)
{
  vlib_process_event_type_t *et;
  pool_get (p->event_type_pool, et);
  et->opaque = with_type_opaque;
  return et;
}

//为指定process创建event
always_inline uword
vlib_process_create_one_time_event (vlib_main_t * vm, uword node_index,
				    uword with_type_opaque)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);
  vlib_process_event_type_t *et;
  uword t;

  //分配et
  et = vlib_process_new_event_type (p, with_type_opaque);
  //获取et索引号
  t = et - p->event_type_pool;
  //指明此et已被分配
  p->one_time_event_type_bitmap =
    clib_bitmap_ori (p->one_time_event_type_bitmap, t);
  return t;
}

always_inline void
vlib_process_delete_one_time_event (vlib_main_t * vm, uword node_index,
				    uword t)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);

  ASSERT (clib_bitmap_get (p->one_time_event_type_bitmap, t));
  vlib_process_free_event_type (p, t, /* is_one_time_event */ 1);
}

//生成一个event,返回event data的起始地址（用于填充event data)
always_inline void *
vlib_process_signal_event_helper (vlib_node_main_t * nm,
				  vlib_node_t * n,
				  vlib_process_t * p,
				  uword t/*event id*/,
				  uword n_data_elts/*event data数目*/, uword n_data_elt_bytes/*每个event data占用的字节数*/)
{
  uword p_flags, add_to_pending, delete_from_wheel;
  void *data_to_be_written_by_caller;

  //必须为process
  ASSERT (n->type == VLIB_NODE_TYPE_PROCESS);

  //指定位置的event type一定存在
  ASSERT (!pool_is_free_index (p->event_type_pool, t));

  vec_validate (p->pending_event_data_by_type_index, t);

  /* Resize data vector and return caller's data to be written. */
  {
    void *data_vec = p->pending_event_data_by_type_index[t];
    uword l;

    //event data空间不存在，尝试复用
    if (!data_vec && vec_len (nm->recycled_event_data_vectors))
      {
	data_vec = vec_pop (nm->recycled_event_data_vectors);
	_vec_len (data_vec) = 0;
      }

    l = vec_len (data_vec);

    data_vec = _vec_resize (data_vec,
			    /* length_increment */ n_data_elts,
			    /* total size after increment */
			    (l + n_data_elts) * n_data_elt_bytes,
			    /* header_bytes */ 0, /* data_align */ 0);

    //存入event data
    p->pending_event_data_by_type_index[t] = data_vec;
    data_to_be_written_by_caller = data_vec + l * n_data_elt_bytes;
  }

  //指明t号事件发生
  p->non_empty_event_type_bitmap =
    clib_bitmap_ori (p->non_empty_event_type_bitmap, t);

  p_flags = p->flags;

  /* Event was already signalled? */
  add_to_pending = (p_flags & VLIB_PROCESS_RESUME_PENDING) == 0;

  /* Process will resume when suspend time elapses? */
  //准备唤醒process
  delete_from_wheel = 0;
  if (p_flags & VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_CLOCK)
    {
      /* Waiting for both event and clock? */
      if (p_flags & VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT)
	{
          //process即等待clock，又等待event,则检查p->stop_timer_handle是否已启动
          //如已启动，则需要delete
	  if (!TW (tw_timer_handle_is_free)
	      ((TWT (tw_timer_wheel) *) nm->timing_wheel,
	       p->stop_timer_handle))
	    delete_from_wheel = 1;
	  else
	    /* timer just popped so process should already be on the list */
	    add_to_pending = 0;
	}
      else
	/* Waiting only for clock.  Event will be queue and may be
	   handled when timer expires. */
	add_to_pending = 0;
    }

  /* Never add current process to pending vector since current process is
     already running. */
  add_to_pending &= nm->current_process_index != n->runtime_index;

  if (add_to_pending)
    {
      u32 x = vlib_timing_wheel_data_set_suspended_process (n->runtime_index);
      p->flags = p_flags | VLIB_PROCESS_RESUME_PENDING;
      vec_add1 (nm->data_from_advancing_timing_wheel, x);
      //停止对应的定时器
      if (delete_from_wheel)
	TW (tw_timer_stop) ((TWT (tw_timer_wheel) *) nm->timing_wheel,
			    p->stop_timer_handle);
    }

  return data_to_be_written_by_caller;
}

//为node_index生成一个event,要求event-type,指出了event-data的长度
always_inline void *
vlib_process_signal_event_data (vlib_main_t * vm,
				uword node_index,
				uword type_opaque,
				uword n_data_elts, uword n_data_elt_bytes)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);
  uword *h, t;

  /* Must be in main thread */
  ASSERT (vlib_get_thread_index () == 0);

  //如果无type_opaque对应的event type,则加入
  h = hash_get (p->event_type_index_by_type_opaque, type_opaque);
  if (!h)
    {
      vlib_process_event_type_t *et =
	vlib_process_new_event_type (p, type_opaque);
      t = et - p->event_type_pool;
      hash_set (p->event_type_index_by_type_opaque, type_opaque, t);
    }
  else
    t = h[0];

  //产生一个event，并获得其对应的event data指针
  return vlib_process_signal_event_helper (nm, n, p, t, n_data_elts,
					   n_data_elt_bytes);
}

//在dt事件之后，设置一个event(如果dt过短，则直接设置event,否则先启定时器，再设置event)
always_inline void *
vlib_process_signal_event_at_time (vlib_main_t * vm,
				   f64 dt,
				   uword node_index/*针对哪个node生成事件*/,
				   uword type_opaque,
				   uword n_data_elts, uword n_data_elt_bytes)
{
  //取node_index对应的process
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);
  uword *h, t;

  h = hash_get (p->event_type_index_by_type_opaque, type_opaque);
  if (!h)
    {
      //不存在指定的event type,则创建它
      vlib_process_event_type_t *et =
	vlib_process_new_event_type (p, type_opaque);
      t = et - p->event_type_pool;
      hash_set (p->event_type_index_by_type_opaque, type_opaque, t);
    }
  else
    t = h[0];

  //dt过小，则直接为节点n生成一个event,指定event type为t
  if (vlib_process_suspend_time_is_zero (dt))
    return vlib_process_signal_event_helper (nm, n, p, t, n_data_elts,
					     n_data_elt_bytes);
  else
    {
      vlib_signal_timed_event_data_t *te;

      //自pool中分配一个te
      pool_get_aligned (nm->signal_timed_event_data_pool, te, sizeof (te[0]));

      te->n_data_elts = n_data_elts;
      te->n_data_elt_bytes = n_data_elt_bytes;
      te->n_data_bytes = n_data_elts * n_data_elt_bytes;

      /* Assert that structure fields are big enough. */
      ASSERT (te->n_data_elts == n_data_elts);
      ASSERT (te->n_data_elt_bytes == n_data_elt_bytes);
      ASSERT (te->n_data_bytes == n_data_elts * n_data_elt_bytes);

      te->process_node_index = n->runtime_index;
      te->event_type_index = t;

      //启动定时器来生成这个event
      p->stop_timer_handle =
	TW (tw_timer_start) ((TWT (tw_timer_wheel) *) nm->timing_wheel,
			     vlib_timing_wheel_data_set_timed_event
			     (te - nm->signal_timed_event_data_pool)/*指明参数为te索引*/,
			     0 /* timer_id */ ,
			     (vlib_time_now (vm) + dt) * 1e5);

      /* Inline data big enough to hold event? */
      //如果内建的data足够用，则直接返回
      if (te->n_data_bytes < sizeof (te->inline_event_data))
	return te->inline_event_data;
      else
	{
      //内建的data不够用，为其分配足量空间并返回
	  te->event_data_as_vector = 0;
	  vec_resize (te->event_data_as_vector, te->n_data_bytes);
	  return te->event_data_as_vector;
	}
    }
}

//触发单次的event
always_inline void *
vlib_process_signal_one_time_event_data (vlib_main_t * vm,
					 uword node_index,
					 uword type_index,
					 uword n_data_elts,
					 uword n_data_elt_bytes)
{
  vlib_node_main_t *nm = &vm->node_main;
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_process_t *p = vec_elt (nm->processes, n->runtime_index);
  return vlib_process_signal_event_helper (nm, n, p, type_index, n_data_elts,
					   n_data_elt_bytes);
}

always_inline void
vlib_process_signal_event (vlib_main_t * vm,
			   uword node_index, uword type_opaque, uword data)
{
  uword *d = vlib_process_signal_event_data (vm, node_index, type_opaque,
					     1 /* elts */ , sizeof (uword));
  d[0] = data;
}

always_inline void
vlib_process_signal_event_pointer (vlib_main_t * vm,
				   uword node_index,
				   uword type_opaque, void *data)
{
  void **d = vlib_process_signal_event_data (vm, node_index, type_opaque,
					     1 /* elts */ , sizeof (data));
  d[0] = data;
}

/**
 * Signal event to process from any thread.
 *
 * When in doubt, use this.
 */
always_inline void
vlib_process_signal_event_mt (vlib_main_t * vm,
			      uword node_index, uword type_opaque, uword data)
{
  if (vlib_get_thread_index () != 0)
    {
      vlib_process_signal_event_mt_args_t args = {
	.node_index = node_index,
	.type_opaque = type_opaque,
	.data = data,
      };
      vlib_rpc_call_main_thread (vlib_process_signal_event_mt_helper,
				 (u8 *) & args, sizeof (args));
    }
  else
    vlib_process_signal_event (vm, node_index, type_opaque, data);
}

always_inline void
vlib_process_signal_one_time_event (vlib_main_t * vm,
				    uword node_index,
				    uword type_index, uword data)
{
  uword *d =
    vlib_process_signal_one_time_event_data (vm, node_index, type_index,
					     1 /* elts */ , sizeof (uword));
  d[0] = data;
}

always_inline void
vlib_signal_one_time_waiting_process (vlib_main_t * vm,
				      vlib_one_time_waiting_process_t * p)
{
  vlib_process_signal_one_time_event (vm, p->node_index, p->one_time_event,
				      /* data */ ~0);
  clib_memset (p, ~0, sizeof (p[0]));
}

always_inline void
vlib_signal_one_time_waiting_process_vector (vlib_main_t * vm,
					     vlib_one_time_waiting_process_t
					     ** wps)
{
  vlib_one_time_waiting_process_t *wp;
  vec_foreach (wp, *wps) vlib_signal_one_time_waiting_process (vm, wp);
  vec_free (*wps);
}

always_inline void
vlib_current_process_wait_for_one_time_event (vlib_main_t * vm,
					      vlib_one_time_waiting_process_t
					      * p)
{
  p->node_index = vlib_current_process (vm);
  p->one_time_event = vlib_process_create_one_time_event (vm, p->node_index,	/* type opaque */
							  ~0);
  vlib_process_wait_for_one_time_event (vm,
					/* don't care about data */ 0,
					p->one_time_event);
}

always_inline void
vlib_current_process_wait_for_one_time_event_vector (vlib_main_t * vm,
						     vlib_one_time_waiting_process_t
						     ** wps)
{
  vlib_one_time_waiting_process_t *wp;
  vec_add2 (*wps, wp, 1);
  vlib_current_process_wait_for_one_time_event (vm, wp);
}

always_inline u32
vlib_node_runtime_update_main_loop_vector_stats (vlib_main_t * vm,
						 vlib_node_runtime_t * node,
						 uword n_vectors)
{
  u32 i, d, vi0, vi1;
  u32 i0, i1;

  ASSERT (is_pow2 (ARRAY_LEN (node->main_loop_vector_stats)));
  //将main_loop_count映射到main_loop_vector_stats索引上
  i = ((vm->main_loop_count >> VLIB_LOG2_MAIN_LOOPS_PER_STATS_UPDATE)
       & (ARRAY_LEN (node->main_loop_vector_stats) - 1));

  i0 = i ^ 0;//如果i尾部为1，则i0为1，否则0
  i1 = i ^ 1;//如果i尾部为1，则i1为0，否则1

  //main_loop_count_last_dispatch中记录的是上次dispatch此node时main_loop_count,计算两者之间的间隔
  d = ((vm->main_loop_count >> VLIB_LOG2_MAIN_LOOPS_PER_STATS_UPDATE)
       -
       (node->main_loop_count_last_dispatch >>
	VLIB_LOG2_MAIN_LOOPS_PER_STATS_UPDATE));

  //
  vi0 = node->main_loop_vector_stats[i0];
  vi1 = node->main_loop_vector_stats[i1];
  vi0 = d == 0 ? vi0 : 0;
  vi1 = d <= 1 ? vi1 : 0;
  vi0 += n_vectors;
  node->main_loop_vector_stats[i0] = vi0;
  node->main_loop_vector_stats[i1] = vi1;
  node->main_loop_count_last_dispatch = vm->main_loop_count;
  /* Return previous counter. */
  return node->main_loop_vector_stats[i1];
}

always_inline f64
vlib_node_vectors_per_main_loop_as_float (vlib_main_t * vm, u32 node_index)
{
  vlib_node_runtime_t *rt = vlib_node_get_runtime (vm, node_index);
  u32 v;

  v = vlib_node_runtime_update_main_loop_vector_stats (vm, rt,	/* n_vectors */
						       0);
  return (f64) v / (1 << VLIB_LOG2_MAIN_LOOPS_PER_STATS_UPDATE);
}

always_inline u32
vlib_node_vectors_per_main_loop_as_integer (vlib_main_t * vm, u32 node_index)
{
  vlib_node_runtime_t *rt = vlib_node_get_runtime (vm, node_index);
  u32 v;

  v = vlib_node_runtime_update_main_loop_vector_stats (vm, rt,	/* n_vectors */
						       0);
  return v >> VLIB_LOG2_MAIN_LOOPS_PER_STATS_UPDATE;
}

void
vlib_frame_free (vlib_main_t * vm, vlib_node_runtime_t * r, vlib_frame_t * f);

/* Return the edge index if present, ~0 otherwise */
uword vlib_node_get_next (vlib_main_t * vm, uword node, uword next_node);

/* Add next node to given node in given slot. */
uword
vlib_node_add_next_with_slot (vlib_main_t * vm,
			      uword node, uword next_node, uword slot);

/* As above but adds to end of node's next vector. */
always_inline uword
vlib_node_add_next (vlib_main_t * vm, uword node, uword next_node)
{
  return vlib_node_add_next_with_slot (vm, node, next_node, ~0);
}

/* Add next node to given node in given slot. */
uword
vlib_node_add_named_next_with_slot (vlib_main_t * vm,
				    uword node, char *next_name, uword slot);

/* As above but adds to end of node's next vector. */
always_inline uword
vlib_node_add_named_next (vlib_main_t * vm, uword node, char *name)
{
  return vlib_node_add_named_next_with_slot (vm, node, name, ~0);
}

/**
 * Get list of nodes
 */
void
vlib_node_get_nodes (vlib_main_t * vm, u32 max_threads, int include_stats,
		     int barrier_sync, vlib_node_t **** node_dupsp,
		     vlib_main_t *** stat_vmsp);

/* Query node given name. */
vlib_node_t *vlib_get_node_by_name (vlib_main_t * vm, u8 * name);

/* Rename a node. */
void vlib_node_rename (vlib_main_t * vm, u32 node_index, char *fmt, ...);

/* Register new packet processing node.  Nodes can be registered
   dynamically via this call or statically via the VLIB_REGISTER_NODE
   macro. */
u32 vlib_register_node (vlib_main_t * vm, vlib_node_registration_t * r);

/* Register all static nodes registered via VLIB_REGISTER_NODE. */
void vlib_register_all_static_nodes (vlib_main_t * vm);

/* Start a process. */
void vlib_start_process (vlib_main_t * vm, uword process_index);

/* Sync up runtime and main node stats. */
void vlib_node_sync_stats (vlib_main_t * vm, vlib_node_t * n);

/* Node graph initialization function. */
clib_error_t *vlib_node_main_init (vlib_main_t * vm);

format_function_t format_vlib_node_graph;
format_function_t format_vlib_node_name;
format_function_t format_vlib_next_node_name;
format_function_t format_vlib_node_and_next;
format_function_t format_vlib_cpu_time;
format_function_t format_vlib_time;
/* Parse node name -> node index. */
unformat_function_t unformat_vlib_node;

//为指定node的counter_index指定统计计数增加increment
always_inline void
vlib_node_increment_counter (vlib_main_t * vm, u32 node_index,
			     u32 counter_index, u64 increment)
{
    //取要统计的node
  vlib_node_t *n = vlib_get_node (vm, node_index);
  vlib_error_main_t *em = &vm->error_main;
  //取此node针对em->counters的偏移量
  u32 node_counter_base_index = n->error_heap_index;
  //为计算上counter_index后偏移量增加increment
  em->counters[node_counter_base_index + counter_index] += increment;
}

#endif /* included_vlib_node_funcs_h */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
