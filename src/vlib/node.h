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
 * node.h: VLIB processing nodes
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

#ifndef included_vlib_node_h
#define included_vlib_node_h

#include <vppinfra/cpu.h>
#include <vppinfra/longjmp.h>
#include <vppinfra/lock.h>
#include <vlib/trace.h>		/* for vlib_trace_filter_t */

/* Forward declaration. */
struct vlib_node_runtime_t;
struct vlib_frame_t;

/* Internal nodes (including output nodes) move data from node to
   node (or out of the graph for output nodes). */
typedef uword (vlib_node_function_t) (struct vlib_main_t * vm,
				      struct vlib_node_runtime_t * node,
				      struct vlib_frame_t * frame);

typedef enum
{
  VLIB_NODE_PROTO_HINT_NONE = 0,
  VLIB_NODE_PROTO_HINT_ETHERNET,
  VLIB_NODE_PROTO_HINT_IP4,
  VLIB_NODE_PROTO_HINT_IP6,
  VLIB_NODE_PROTO_HINT_TCP,
  VLIB_NODE_PROTO_HINT_UDP,
  VLIB_NODE_N_PROTO_HINTS,
} vlib_node_proto_hint_t;

typedef enum
{
  /* An internal node on the call graph (could be output). */
  VLIB_NODE_TYPE_INTERNAL,//内部节点

  /* Nodes which input data into the processing graph.
     Input nodes are called for each iteration of main loop. */
  VLIB_NODE_TYPE_INPUT,//输入节点

  /* Nodes to be called before all input nodes.
     Used, for example, to clean out driver TX rings before
     processing input. */
  VLIB_NODE_TYPE_PRE_INPUT,//输入前节点（例如处理tx缓冲区清空）

  /* "Process" nodes which can be suspended and later resumed. */
  VLIB_NODE_TYPE_PROCESS,//process类型的node,可被挂起的节点，将被创建process

  VLIB_N_NODE_TYPE,
} vlib_node_type_t;

typedef struct _vlib_node_fn_registration
{
  vlib_node_function_t *function;
  int priority;
  struct _vlib_node_fn_registration *next_registration;
  char *name;
} vlib_node_fn_registration_t;

typedef struct _vlib_node_registration
{
  /* Vector processing function for this node. */
  vlib_node_function_t *function;//node对应的报文处理函数

  /* Node function candidate registration with priority */
  //容许为节点执行多个function,按优先级划分（如果此值不为NULL,则在注册节点时
  //将通过此链表查找优先级更大的function做为注册function)
  vlib_node_fn_registration_t *node_fn_registrations;

  /* Node name. */
  char *name;//node名称

  /* Name of sibling (if applicable). */
  char *sibling_of;

  /* Node index filled in by registration. */
  u32 index;//node的索引号

  /* Type of this node. */
  vlib_node_type_t type;//node类型

  /* Error strings indexed by error code for this node. */
  char **error_strings;//错误码对应的字符串数组

  /* Buffer format/unformat for this node. */
  format_function_t *format_buffer;
  unformat_function_t *unformat_buffer;

  /* Trace format/unformat for this node. */
  format_function_t *format_trace;
  unformat_function_t *unformat_trace;

  /* Function to validate incoming frames. */
  u8 *(*validate_frame) (struct vlib_main_t * vm,
			 struct vlib_node_runtime_t *,
			 struct vlib_frame_t * f);

  /* Per-node runtime data. */
  void *runtime_data;

  /* Process stack size. */
  u16 process_log2_n_stack_bytes;//process对应的堆栈大小

  /* Number of bytes of per-node run time data. */
  u8 runtime_data_bytes;//runtime_data所需要内存大小

  /* State for input nodes. */
  u8 state;

  /* Node flags. */
  u16 flags;

  /* protocol at b->data[b->current_data] upon entry to the dispatch fn */
  u8 protocol_hint;

  /* Size of scalar and vector arguments in bytes. */
  u16 scalar_size, vector_size;

  /* Number of error codes used by this node. */
  u16 n_errors;//错误码最大值

  /* Number of next node names that follow. */
  u16 n_next_nodes;//下级node的数目（即next_nodes数组长度）

  /* Constructor link-list, don't ask... */
  struct _vlib_node_registration *next_registration;//指出下一个待注册的node

  /* Names of next nodes which this node feeds into. */
  char *next_nodes[];//下一级node名称

} vlib_node_registration_t;

#ifndef CLIB_MARCH_VARIANT
/**
 * 将x指定的node注册到vm->node_main.node_registrations链表头上，
 * 此宏接着会要求对x进行初始化
 */
#define VLIB_REGISTER_NODE(x,...)                                       \
	/*声明node注册变量*/                                                  \
    __VA_ARGS__ vlib_node_registration_t x;                             \
    /*声明并实现函数__vlib_add_node_registration_##x在main之前执行*/\
static void __vlib_add_node_registration_##x (void)                     \
    __attribute__((__constructor__)) ;                                  \
static void __vlib_add_node_registration_##x (void)                     \
{																	   \
    /*取当前线程的vlib_main*/\
    vlib_main_t * vm = vlib_get_main();                                 \
    /*将x挂接在node_main.node_registrations链表的头部*/\
    x.next_registration = vm->node_main.node_registrations;             \
    vm->node_main.node_registrations = &x;                              \
}                                                                       \
/*声明并实现__vlib_rm_node_registration_##x在退出时执行，用于移除node*/\
static void __vlib_rm_node_registration_##x (void)                      \
    __attribute__((__destructor__)) ;                                   \
static void __vlib_rm_node_registration_##x (void)                      \
{                                                                       \
    vlib_main_t * vm = vlib_get_main();                                 \
    /*自链表vm->node_main.node_registrations中移除x节点*/\
    VLIB_REMOVE_FROM_LINKED_LIST (vm->node_main.node_registrations,     \
                                  &x, next_registration);               \
}                                                                       \
/*用于对变量进行初始化*/\
__VA_ARGS__ vlib_node_registration_t x
#else
#define VLIB_REGISTER_NODE(x,...)                                       \
static __clib_unused vlib_node_registration_t __clib_unused_##x
#endif

#ifndef CLIB_MARCH_VARIANT
#define CLIB_MARCH_VARIANT_STR "default"
#else
#define _CLIB_MARCH_VARIANT_STR(s) __CLIB_MARCH_VARIANT_STR(s)
#define __CLIB_MARCH_VARIANT_STR(s) #s
#define CLIB_MARCH_VARIANT_STR _CLIB_MARCH_VARIANT_STR(CLIB_MARCH_VARIANT)
#endif

//定义node的报文处理函数
//声明函数node##_fn,定义node##_fun_registration变量
#define VLIB_NODE_FN(node)						\
uword CLIB_MARCH_SFX (node##_fn)();					\
static vlib_node_fn_registration_t					\
  CLIB_MARCH_SFX(node##_fn_registration) =				\
  { .function = &CLIB_MARCH_SFX (node##_fn), };				\
									\
/*定义main之前运行函数，将node##_fn_registeration注册到变量node上*/\
static void __clib_constructor						\
CLIB_MARCH_SFX (node##_multiarch_register) (void)			\
{									\
	/*引用我们需要设置的node*/\
  extern vlib_node_registration_t node;					\
  vlib_node_fn_registration_t *r;					\
  r = & CLIB_MARCH_SFX (node##_fn_registration);			\
  /*设置默认的优先级，如果用户指定更高的优先级，则运行时可以被用户设置的function所替代*/\
  r->priority = CLIB_MARCH_FN_PRIORITY();				\
  r->name = CLIB_MARCH_VARIANT_STR;					\
  r->next_registration = node.node_fn_registrations;			\
  node.node_fn_registrations = r;					\
}									\
    /*定义函数node##_fn*/\
uword CLIB_CPU_OPTIMIZED CLIB_MARCH_SFX (node##_fn)

always_inline vlib_node_registration_t *
vlib_node_next_registered (vlib_node_registration_t * c)
{
  c =
    clib_elf_section_data_next (c,
				c->n_next_nodes * sizeof (c->next_nodes[0]));
  return c;
}

typedef struct
{
  /* Total calls, clock ticks and vector elements processed for this node. */
  u64 calls, vectors, clocks, suspends;
  u64 max_clock;
  u64 max_clock_n;
  u64 perf_counter0_ticks;
  u64 perf_counter1_ticks;
  u64 perf_counter_vectors;
} vlib_node_stats_t;

#define foreach_vlib_node_state					\
  /* Input node is called each iteration of main loop.		\
     This is the default (zero). */				\
  _ (POLLING)							\
  /* Input node is called when device signals an interrupt. */	\
  _ (INTERRUPT)							\
  /* Input node is never called. */				\
  _ (DISABLED)

typedef enum
{
#define _(f) VLIB_NODE_STATE_##f,
  foreach_vlib_node_state
#undef _
    VLIB_N_NODE_STATE,
} vlib_node_state_t;

typedef struct vlib_node_t
{
  /* Vector processing function for this node. */
  vlib_node_function_t *function;//node对应的处理function

  /* Node name. */
  u8 *name;//node名称

  /* Node name index in elog string table. */
  u32 name_elog_string;//node名称在elog索引表中的索引

  /* Total statistics for this node. */
  vlib_node_stats_t stats_total;

  /* Saved values as of last clear (or zero if never cleared).
     Current values are always stats_total - stats_last_clear. */
  vlib_node_stats_t stats_last_clear;

  /* Type of this node. */
  vlib_node_type_t type;//节点类型

  /* Node index. */
  u32 index;//node编号

  /* Index of corresponding node runtime. */
  u32 runtime_index;//节点的运行索引（例如process索引号）

  /* Runtime data for this node. */
  void *runtime_data;//runtime对应的私有data

  /* Node flags. */
  u16 flags;

  /* Processing function keeps frame.  Tells node dispatching code not
     to free frame after dispatch is done.  */
#define VLIB_NODE_FLAG_FRAME_NO_FREE_AFTER_DISPATCH (1 << 0)

  /* Node counts as output/drop/punt node for stats purposes. */
#define VLIB_NODE_FLAG_IS_OUTPUT (1 << 1)
#define VLIB_NODE_FLAG_IS_DROP (1 << 2)
#define VLIB_NODE_FLAG_IS_PUNT (1 << 3)
#define VLIB_NODE_FLAG_IS_HANDOFF (1 << 4)

  /* Set if current node runtime has traced vectors. */
#define VLIB_NODE_FLAG_TRACE (1 << 5)

#define VLIB_NODE_FLAG_SWITCH_FROM_INTERRUPT_TO_POLLING_MODE (1 << 6)
#define VLIB_NODE_FLAG_SWITCH_FROM_POLLING_TO_INTERRUPT_MODE (1 << 7)

  /* State for input nodes. */
  u8 state;

  /* Number of bytes of run time data. */
  u8 runtime_data_bytes;//runtime_data对应的字节长度

  /* protocol at b->data[b->current_data] upon entry to the dispatch fn */
  u8 protocol_hint;

  /* Number of error codes used by this node. */
  u16 n_errors;//node对应的错误号数目

  /* Size of scalar and vector arguments in bytes. */
  //此node要求的frame的scalar_size,vector_size
  u16 scalar_size, vector_size;

  /* Handle/index in error heap for this node. */
  u32 error_heap_handle;
  u32 error_heap_index;//其在vlib_error_main_t结构体成员中的索引

  /* Error strings indexed by error code for this node. */
  char **error_strings;//node对应的错误号字符串

  /* Vector of next node names.
     Only used before next_nodes array is initialized. */
  char **next_node_names;//此节点有哪些名称的后续节点（下一级node的名称）

  /* Next node indices for this node. */
  u32 *next_nodes;//此节点有哪些后继节点(索引）

  /* Name of node that we are sibling of. */
  char *sibling_of;

  /* Bitmap of all of this node's siblings. */
  uword *sibling_bitmap;//指出此节点的兄弟节点有哪些

  /* Total number of vectors sent to each next node. */
  u64 *n_vectors_by_next_node;

  /* Hash table mapping next node index into slot in
     next_nodes vector.  Quickly determines whether this node
     is connected to given next node and, if so, with which slot. */
  uword *next_slot_by_node;//根据下一个节点的index,查找其对应的节点slot

  /* Bitmap of node indices which feed this node. */
  uword *prev_node_bitmap;//指出此节点是哪些节点的后继节点

  /* Node/next-index which own enqueue rights with to this node. */
  u32 owner_node_index, owner_next_index;

  /* Buffer format/unformat for this node. */
  format_function_t *format_buffer;
  unformat_function_t *unformat_buffer;

  /* Trace buffer format/unformat for this node. */
  format_function_t *format_trace;

  /* Function to validate incoming frames. */
  u8 *(*validate_frame) (struct vlib_main_t * vm,
			 struct vlib_node_runtime_t *,
			 struct vlib_frame_t * f);
  /* for pretty-printing, not typically valid */
  u8 *state_string;

  /* Node function candidate registration with priority */
  vlib_node_fn_registration_t *node_fn_registrations;//node对应的registrations
} vlib_node_t;

#define VLIB_INVALID_NODE_INDEX ((u32) ~0)

/* Max number of vector elements to process at once per node. */
#define VLIB_FRAME_SIZE 256
#define VLIB_FRAME_ALIGN CLIB_CACHE_LINE_BYTES

/* Calling frame (think stack frame) for a node. */
typedef struct vlib_frame_t
{
  /* Frame flags. */
  u16 frame_flags;

  /* User flags. Used for sending hints to the next node. */
  u16 flags;

  /* Number of scalar bytes in arguments. */
  u8 scalar_size;//frame后面多少偏移量到参数

  /* Number of bytes per vector argument. */
  u8 vector_size;

  /* Number of vector elements currently in frame. */
  u16 n_vectors;//当前frame有多少个vector元素

  /* Scalar and vector arguments to next node. */
  //其后为scalar size ,vector argument,magic
  u8 arguments[0];
} vlib_frame_t;

typedef struct
{
  /* Frame index. */
  //指向可填充的frame index
  u32 frame_index;

  /* Node runtime for this next. */
  //指向此报文是送给哪个node的
  u32 node_runtime_index;

  /* Next frame flags. */
  u32 flags;

  /* Reflects node frame-used flag for this next. */
  //表示next仍可使用此frame
#define VLIB_FRAME_NO_FREE_AFTER_DISPATCH \
  VLIB_NODE_FLAG_FRAME_NO_FREE_AFTER_DISPATCH

  /* Don't append this frame */
  //标记此frame不支持附加frame index
#define VLIB_FRAME_NO_APPEND (1 << 14)

  /* This next frame owns enqueue to node
     corresponding to node_runtime_index. */
#define VLIB_FRAME_OWNER (1 << 15)

  /* Set when frame has been allocated for this next. */
  //标明frame_index字段有效（空间已被申请标记）
#define VLIB_FRAME_IS_ALLOCATED	VLIB_NODE_FLAG_IS_OUTPUT

  /* Set when frame has been added to pending vector. */
  //标记报文已被加入到pending vector中
#define VLIB_FRAME_PENDING VLIB_NODE_FLAG_IS_DROP

  /* Set when frame is to be freed after dispatch. */
  //标记frame在dispatch后需要释放
#define VLIB_FRAME_FREE_AFTER_DISPATCH VLIB_NODE_FLAG_IS_PUNT

  /* Set when frame has traced packets. */
#define VLIB_FRAME_TRACE VLIB_NODE_FLAG_TRACE

  /* Number of vectors enqueue to this next since last overflow. */
  u32 vectors_since_last_overflow;
} vlib_next_frame_t;

always_inline void
vlib_next_frame_init (vlib_next_frame_t * nf)
{
  clib_memset (nf, 0, sizeof (nf[0]));
  nf->frame_index = ~0;
  nf->node_runtime_index = ~0;
}

/* A frame pending dispatch by main loop. */
typedef struct
{
  /* Node and runtime for this frame. */
  u32 node_runtime_index;//指出此pending_frame将由哪个node处理

  /* Frame index (in the heap). */
  u32 frame_index;//对应的frame buffer index

  /* Start of next frames for this node. */
  u32 next_frame_index;//指出此frame　来源的next-frame结构的索引

  /* Special value for next_frame_index when there is no next frame. */
#define VLIB_PENDING_FRAME_NO_NEXT_FRAME ((u32) ~0)
} vlib_pending_frame_t;

typedef struct vlib_node_runtime_t
{
  CLIB_CACHE_LINE_ALIGN_MARK (cacheline0);	/**< cacheline mark */

  //node需要被调用的函数
  vlib_node_function_t *function;	/**< Node function to call. */

  //此节点支持的错误码（会被加上node编号）
  vlib_error_t *errors;			/**< Vector of errors for this node. */

#if __SIZEOF_POINTER__ == 4
  u8 pad[8];
#endif

  u32 clocks_since_last_overflow;	/**< Number of clock cycles. */

  u32 max_clock;			/**< Maximum clock cycle for an
					  invocation. */

  u32 max_clock_n;			/**< Number of vectors in the recorded
					  max_clock. */

  u32 calls_since_last_overflow;	/**< Number of calls. */

  u32 vectors_since_last_overflow;	/**< Number of vector elements
					  processed by this node. */

  u32 perf_counter0_ticks_since_last_overflow; /**< Perf counter 0 ticks */
  u32 perf_counter1_ticks_since_last_overflow; /**< Perf counter 1 ticks */
  u32 perf_counter_vectors_since_last_overflow;	/**< Perf counter vectors */

  //此node在next_frames列表中的起始偏移量
  u32 next_frame_index;			/**< Start of next frames for this
					  node. */

  //节点索引
  u32 node_index;			/**< Node index. */

  u32 input_main_loops_per_call;	/**< For input nodes: decremented
					  on each main loop interation until
					  it reaches zero and function is
					  called.  Allows some input nodes to
					  be called more than others. */

  //保存上一次dispatch此node时main loop的计数
  u32 main_loop_count_last_dispatch;	/**< Saved main loop counter of last
					  dispatch of this node. */

  u32 main_loop_vector_stats[2];

  u16 flags;				/**< Copy of main node flags. */

  u16 state;				/**< Input node state. */

  u16 n_next_nodes;//后继节点的数目

  u16 cached_next_index;		/**< Next frame index that vector
					  arguments were last enqueued to
					  last time this node ran. Set to
					  zero before first run of this
					  node. */

  //node所属的thread
  u16 thread_index;			/**< thread this node runs on */

  //node的私有数据
  u8 runtime_data[0];			/**< Function dependent
					  node-runtime data. This data is
					  thread local, and it is not
					  cloned from main thread. It needs
					  to be initialized for each thread
					  before it is used unless
					  runtime_data template exists in
					  vlib_node_t. */
}
vlib_node_runtime_t;

#define VLIB_NODE_RUNTIME_DATA_SIZE	(sizeof (vlib_node_runtime_t) - STRUCT_OFFSET_OF (vlib_node_runtime_t, runtime_data))

typedef struct
{
  /* Number of allocated frames for this scalar/vector size. */
  u32 n_alloc_frames;//空闲数目

  /* Vector of free frame indices for this scalar/vector size. */
  u32 *free_frame_indices;//同种frame size的空闲的frame index数组
} vlib_frame_size_t;

typedef struct
{
  /* Users opaque value for event type. */
  uword opaque;
} vlib_process_event_type_t;

typedef struct
{
  /* Node runtime for this process. */
  vlib_node_runtime_t node_runtime;//对应的node

  /* Where to longjmp when process is done. */
  //用于记录当前返回点，后面跳过来时将自此点考虑（1。return，则重启执行此process;2.suspend则添加相应定时器将进程挂起）
  //目前此变量有两处跳转点保存：1。启动process时;2.恢复process时
  clib_longjmp_t return_longjmp;

//用于指出需要将进程重新调用
#define VLIB_PROCESS_RETURN_LONGJMP_RETURN ((uword) ~0 - 0)
//返回此值时需要将对应的process挂起
#define VLIB_PROCESS_RETURN_LONGJMP_SUSPEND ((uword) ~0 - 1)

  /* Where to longjmp to resume node after suspend. */
  //用于记录当前恢复点，后面跳过来时将自此点恢复（恢复后有两个动作1。继续执行;2.继续挂起
  clib_longjmp_t resume_longjmp;
#define VLIB_PROCESS_RESUME_LONGJMP_SUSPEND 0
#define VLIB_PROCESS_RESUME_LONGJMP_RESUME  1

  u16 flags;
#define VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_CLOCK (1 << 0)
#define VLIB_PROCESS_IS_SUSPENDED_WAITING_FOR_EVENT (1 << 1)
  /* Set to indicate that this process has been added to resume vector. */
#define VLIB_PROCESS_RESUME_PENDING (1 << 2)

  /* Process function is currently running. */
#define VLIB_PROCESS_IS_RUNNING (1 << 3)

  /* Size of process stack. */
  u16 log2_n_stack_bytes;//process栈空间大小

  u32 suspended_process_frame_index;//此process被挂起时对应的pending frame索引

  /* Number of times this process was suspended. */
  u32 n_suspends;//此process被挂起的次数

  /* Vectors of pending event data indexed by event type index. */
  //指出event id对应的event data
  void **pending_event_data_by_type_index;

  /* Bitmap of event type-indices with non-empty vectors. */
  //标记event id的bitmap(通过此字段拿到event对应的id,再取event data,event type)
  uword *non_empty_event_type_bitmap;

  /* Bitmap of event type-indices which are one time events. */
  //指出event id对应的event是否为单次event
  uword *one_time_event_type_bitmap;

  /* Type is opaque pointer -- typically a pointer to an event handler
     function.  Hash table to map opaque to a type index. */
  //通过event type映射event id的hashtable
  uword *event_type_index_by_type_opaque;

  /* Pool of currently valid event types. */
  //指出event id对应的user给出的event type
  vlib_process_event_type_t *event_type_pool;

  /*
   * When suspending saves clock time (10us ticks) when process
   * is to be resumed.
   */
  //指明需要suspend的时间大小
  u64 resume_clock_interval;

  /* Handle from timer code, to cancel an unexpired timer */
  u32 stop_timer_handle;//记录timer索引，用于timer取消

  /* Default output function and its argument for any CLI outputs
     within the process. */
  vlib_cli_output_function_t *output_function;
  uword output_function_arg;

#ifdef CLIB_UNIX
  /* Pad to a multiple of the page size so we can mprotect process stacks */
#define PAGE_SIZE_MULTIPLE 0x1000
#define ALIGN_ON_MULTIPLE_PAGE_BOUNDARY_FOR_MPROTECT  __attribute__ ((aligned (PAGE_SIZE_MULTIPLE)))
#else
#define ALIGN_ON_MULTIPLE_PAGE_BOUNDARY_FOR_MPROTECT
#endif

  /* Process stack.  Starts here and extends 2^log2_n_stack_bytes
     bytes. */

  //进程对应的栈空间
#define VLIB_PROCESS_STACK_MAGIC (0xdead7ead)
  u32 stack[0] ALIGN_ON_MULTIPLE_PAGE_BOUNDARY_FOR_MPROTECT;
} vlib_process_t __attribute__ ((aligned (CLIB_CACHE_LINE_BYTES)));

#ifdef CLIB_UNIX
  /* Ensure that the stack is aligned on the multiple of the page size */
typedef char
  assert_process_stack_must_be_aligned_exactly_to_page_size_multiple[(sizeof
								      (vlib_process_t)
								      -
								      PAGE_SIZE_MULTIPLE)
								     ==
								     0 ? 0 :
								     -1];
#endif

typedef struct
{
  u32 node_index;

  u32 one_time_event;
} vlib_one_time_waiting_process_t;

typedef struct
{
  u16 n_data_elts;

  u16 n_data_elt_bytes;

  /* n_data_elts * n_data_elt_bytes */
  u32 n_data_bytes;

  /* Process node & event type to be used to signal event. */
  u32 process_node_index;

  u32 event_type_index;

  union
  {
    u8 inline_event_data[64 - 3 * sizeof (u32) - 2 * sizeof (u16)];

    /* Vector of event data used only when data does not fit inline. */
    u8 *event_data_as_vector;//防止inline空间不足时，使用此来指向动态分配的空间
  };
}
vlib_signal_timed_event_data_t;

always_inline uword
vlib_timing_wheel_data_is_timed_event (u32 d)
{
  return d & 1;
}

//通过偶数表示process挂起
always_inline u32
vlib_timing_wheel_data_set_suspended_process (u32 i)
{
  return 0 + 2 * i;
}

//通过奇数来表示event事件
always_inline u32
vlib_timing_wheel_data_set_timed_event (u32 i)
{
  return 1 + 2 * i;
}

always_inline uword
vlib_timing_wheel_data_get_index (u32 d)
{
  return d / 2;
}

typedef struct
{
  /* Public nodes. */
  vlib_node_t **nodes;//存放node

  /* Node index hashed by node name. */
  uword *node_by_name;//hash表，按名称查找node的index,可在数组nodes中找到对应的node

  u32 flags;
#define VLIB_NODE_MAIN_RUNTIME_STARTED (1 << 0)

  /* Nodes segregated by type for cache locality.
     Does not apply to nodes of type VLIB_NODE_TYPE_INTERNAL. */
  //非process类型的node对应的runtime
  vlib_node_runtime_t *nodes_by_type[VLIB_N_NODE_TYPE];

  /* Node runtime indices for input nodes with pending interrupts. */
  //指出有哪些input node处于中断未决状态
  u32 *pending_interrupt_node_runtime_indices;
  clib_spinlock_t pending_interrupt_lock;

  /* Input nodes are switched from/to interrupt to/from polling mode
     when average vector length goes above/below polling/interrupt
     thresholds. */
  u32 polling_threshold_vector_length;
  u32 interrupt_threshold_vector_length;

  /* Vector of next frames. */
  //用于记录自当前node,要送给下一级node的vlib_next_frame_t
  vlib_next_frame_t *next_frames;

  /* Vector of internal node's frames waiting to be called. */
  //vector变量，存放需要传递给node（pending_frame中指出报文属于哪个node)
  //的frame(这些报文将在node调度时被处理）
  vlib_pending_frame_t *pending_frames;

  /* Timing wheel for scheduling time-based node dispatch. */
  void *timing_wheel;//定时器（未指明过期回调）

  vlib_signal_timed_event_data_t *signal_timed_event_data_pool;

  /* Opaque data vector added via timing_wheel_advance. */
  u32 *data_from_advancing_timing_wheel;//缓存当前timing_wheel已过期定时器

  /* CPU time of next process to be ready on timing wheel. */
  f64 time_next_process_ready;

  /* Vector of process nodes.
     One for each node of type VLIB_NODE_TYPE_PROCESS. */
  vlib_process_t **processes;//node注册时，将所有process类型的node创建相应的process

  /* Current running process or ~0 if no process running. */
  //记录当前正在运行的process（通过此值可获知当前是否处于process上下文中）
  u32 current_process_index;

  /* Pool of pending process frames. */
  vlib_pending_frame_t *suspended_process_frames;

  /* Vector of event data vectors pending recycle. */
  //提取可复用的event data vector
  void **recycled_event_data_vectors;

  /* Current counts of nodes in each state. */
  //input类型node按节点状态统计数量
  u32 input_node_counts_by_state[VLIB_N_NODE_STATE];

  /* Hash of (scalar_size,vector_size) to frame_sizes index. */
  uword *frame_size_hash;

  /* Per-size frame allocation information. */
  //按不同frame_size划分的空闲frame集合
  vlib_frame_size_t *frame_sizes;

  /* Time of last node runtime stats clear. */
  f64 time_last_runtime_stats_clear;

  /* Node registrations added by constructors */
  //在main运行前，各node会挂在此链上
  vlib_node_registration_t *node_registrations;
} vlib_node_main_t;


#define FRAME_QUEUE_MAX_NELTS 32
typedef struct
{
  CLIB_CACHE_LINE_ALIGN_MARK (cacheline0);
  u64 head;
  u64 head_hint;
  u64 tail;
  u32 n_in_use;
  u32 nelts;
  u32 written;
  u32 threshold;
  i32 n_vectors[FRAME_QUEUE_MAX_NELTS];
} frame_queue_trace_t;

typedef struct
{
  u64 count[FRAME_QUEUE_MAX_NELTS];
} frame_queue_nelt_counter_t;

#endif /* included_vlib_node_h */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
