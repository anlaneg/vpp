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
  Copyright (c) 2005,2009 Eliot Dresselhaus

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <vppinfra/elog.h>
#include <vppinfra/cache.h>
#include <vppinfra/error.h>
#include <vppinfra/format.h>
#include <vppinfra/hash.h>
#include <vppinfra/math.h>

//针对em->lock加锁
static inline void
elog_lock (elog_main_t * em)
{
  if (PREDICT_FALSE (em->lock != 0))
    while (clib_atomic_test_and_set (em->lock))
      ;
}

//针对em->lock解锁
static inline void
elog_unlock (elog_main_t * em)
{
  if (PREDICT_FALSE (em->lock != 0))
    {
      CLIB_MEMORY_BARRIER ();
      *em->lock = 0;
    }
}

/* Non-inline version. */
//非inline版本，申请一个event
void *
elog_event_data (elog_main_t * em,
		 elog_event_type_t * type, elog_track_t * track, u64 cpu_time)
{
  return elog_event_data_inline (em, type, track, cpu_time);
}

static void
new_event_type (elog_main_t * em, uword i/*event_type编号*/)
{
  elog_event_type_t *t = vec_elt_at_index (em->event_types, i);

  //如果hash未创建，则创建它
  if (!em->event_type_by_format)
    em->event_type_by_format =
      hash_create_vec ( /* size */ 0, sizeof (u8), sizeof (uword));

  //添加此type的format到event type index的映射
  t->type_index_plus_one = i + 1;
  hash_set_mem (em->event_type_by_format, t->format, i);
}

//查找t对应的event type index,如果查找失败，则创建它
static uword
find_or_create_type (elog_main_t * em, elog_event_type_t * t)
{
  //通过type format查找event type index
  uword *p = hash_get_mem (em->event_type_by_format, t->format);
  uword i;

  if (p)
    i = p[0];//拿到对应的index
  else
    {
      //如果未找到对应的type,则创建它
      i = vec_len (em->event_types);
      vec_add1 (em->event_types, t[0]);
      new_event_type (em, i);
    }

  return i;
}

/* External function to register types. */
word
elog_event_type_register (elog_main_t * em, elog_event_type_t * t)
{
  elog_event_type_t *static_type = t;
  word l;

  elog_lock (em);

  /* Multiple simultaneous registration attempts, */
  //如果t的type索引已赋值，则直接返回
  if (t->type_index_plus_one > 0)
    {
      elog_unlock (em);
      return t->type_index_plus_one - 1;
    }

  l = vec_len (em->event_types);

  //为其设置type索引
  t->type_index_plus_one = 1 + l;

  ASSERT (t->format);

  /* If format args are not specified try to be smart about providing defaults
     so most of the time user does not have to specify them. */
  if (!t->format_args)
    {
      uword i, l;
      char *this_arg;

      //遍历format字符串，取出format指定格式化标识符
      l = strlen (t->format);
      for (i = 0; i < l; i++)
	{
      //非%,跳过
	  if (t->format[i] != '%')
	    continue;
	  //最后一个字符为'%'时跳过
	  if (i + 1 >= l)
	    continue;
	  //防止%%转议
	  if (t->format[i + 1] == '%')	/* %% */
	    continue;

	  //取格式化符后面的字节
	  switch (t->format[i + 1])
	    {
	    default:
	    case 'd':
	    case 'x':
	    case 'u':
	      //32位整数
	      this_arg = "i4";	/* size of u32 */
	      break;
	    case 'f':
	      //
	      this_arg = "f8";	/* defaults to f64 */
	      break;
	    case 's':
	      this_arg = "s0";	/* defaults to null terminated string. */
	      break;
	    }

	  //记录format对应的格式化符号
	  t->format_args =
	    (char *) format ((u8 *) t->format_args, "%s", this_arg);
	}

      /* Null terminate. */
      //添加字符符终止符
      vec_add1 (t->format_args, 0);
    }

  //注册此event type
  vec_add1 (em->event_types, t[0]);

  //使用最后一个位置（即当加入的event type)
  t = em->event_types + l;

  /* Make copies of strings for hashing etc. */
  //构造format参数
  if (t->function)
    t->format = (char *) format (0, "%s %s%c", t->function, t->format, 0);
  else
    t->format = (char *) format (0, "%s%c", t->format, 0);

  t->format_args = (char *) format (0, "%s%c", t->format_args, 0);

  /* Construct string table. */
  //构造枚举类型对应的字面值
  {
    uword i;
    t->n_enum_strings = static_type->n_enum_strings;
    for (i = 0; i < t->n_enum_strings; i++)
      {
	if (!static_type->enum_strings[i])
	  static_type->enum_strings[i] = "MISSING";
	vec_add1 (t->enum_strings_vector,
		  (char *) format (0, "%s%c", static_type->enum_strings[i],
				   0));
      }
  }

  new_event_type (em, l);
  elog_unlock (em);

  return l;
}

//track注册
word
elog_track_register (elog_main_t * em, elog_track_t * t)
{
  word l;

  elog_lock (em);

  l = vec_len (em->tracks);

  t->track_index_plus_one = 1 + l;

  ASSERT (t->name);

  vec_add1 (em->tracks, t[0]);

  t = em->tracks + l;

  t->name = (char *) format (0, "%s%c", t->name, 0);

  elog_unlock (em);

  return l;
}

//解析p指出的两位１０进制数字，如果解析成功，结果由number返回，返回消耗的字节数，如果解析失败，结果为０
static uword
parse_2digit_decimal (char *p, uword * number)
{
  uword i = 0;
  u8 digits[2];

  digits[0] = digits[1] = 0;
  //解析两位的数字
  while (p[i] >= '0' && p[i] <= '9')
    {
      if (i >= 2)
	break;
      digits[i] = p[i] - '0';
      i++;
    }

  //发现１或者２位的数字
  if (i >= 1 && i <= 2)
    {
      if (i == 1)
	*number = digits[0];
      else
	*number = 10 * digits[0] + digits[1];
      return i;
    }
  else
    //解析失败，返回０
    return 0;
}

//分析fmt,result中存放format格式串（即％号之后的格式化符号，例如"-2.3ld"，如果result_len长度不足时，仅存放result_len长度个字符）
//s中保存%之前的数据，返回值为格式化符号最后一个字符到format指针长度
static u8 *
fixed_format (u8 * s, char *fmt, char *result, uword * result_len)
{
  char *f = fmt;
  char *percent;
  uword l = 0;

  //使f指向有效的%符，如果未遇着，则指向'\0'
  while (1)
    {
      if (f[0] == 0)
	break;//空串或者串结束，跳出
      if (f[0] == '%' && f[1] != '%')
	break;//遇着格式化符号，但非%转议，跳出
      f++;//前移
    }

  //如果遇着%或者遇着‘\0'，则f与fmt之间的字符加入s
  if (f > fmt)
    vec_add (s, fmt, f - fmt);

  //未遇到%号，退出，返回字符串长度
  if (f[0] != '%')
    goto done;

  /* Skip percent. */
  percent = f++;//跳过%号（记录%号位置）

  /* Skip possible +-= justification. */
  f += f[0] == '+' || f[0] == '-' || f[0] == '=';//跳过对齐方式

  /* Skip possible X.Y width. */
  while ((f[0] >= '0' && f[0] <= '9') || f[0] == '.')//跳过宽度
    f++;

  /* Skip wlL as in e.g. %Ld. */
  f += f[0] == 'w' || f[0] == 'l' || f[0] == 'L';//跳过修饰符

  /* Finally skip format letter. */
  f += f[0] != 0;//跳过format字符，如果有的话

  //选择两者最小的前缀长度，并自％号位置copy，将其填充到result中
  ASSERT (*result_len > f - percent);
  l = clib_min (f - percent, *result_len - 1);
  clib_memcpy (result, percent, l);
  result[l] = 0;

done:
  //返回前缀长度
  *result_len = f - fmt;
  return s;
}

//va按收两个参数，1.em 用于提供event元数据;2.e 用于提供对应的event
//此函数负责按event指定的type解析event数据，并将其格式化为字符串s,并返回
u8 *
format_elog_event (u8 * s, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  elog_event_t *e = va_arg (*va, elog_event_t *);
  elog_event_type_t *t;
  char *a, *f;
  void *d = (u8 *) e->data;
  char arg_format[64];

  //取event 类型
  t = vec_elt_at_index (em->event_types, e->type);

  f = t->format;
  a = t->format_args;
  while (1)
    {
      uword n_bytes = 0, n_digits, f_bytes = 0;

      f_bytes = sizeof (arg_format);
      //取格式化符号
      s = fixed_format (s, f, arg_format, &f_bytes);
      f += f_bytes;//准备下一次分析

      //无format参数情况时，f当前一定达到结尾
      if (a == 0 || a[0] == 0)
	{
	  /* Format must also be at end. */
	  ASSERT (f[0] == 0);
	  break;
	}

      /* Don't go past end of event data. */
      ASSERT (d < (void *) (e->data + sizeof (e->data)));

      //format参数前可能有两位的整数，将其转换为n_bytes
      n_digits = parse_2digit_decimal (a + 1, &n_bytes);
      switch (a[0])
	{
	case 'i':
	case 't':
	case 'T':
	  {
	    u32 i = 0;
	    u64 l = 0;

	    if (n_bytes == 1)
	      i = ((u8 *) d)[0];//取单字节
	    else if (n_bytes == 2)
	      i = clib_mem_unaligned (d, u16);//取双字节
	    else if (n_bytes == 4)
	      i = clib_mem_unaligned (d, u32);//取4字节
	    else if (n_bytes == 8)
	      l = clib_mem_unaligned (d, u64);//取8字节
	    else
	      ASSERT (0);//格式化错误
	    if (a[0] == 't')
	      {
	        //取枚举对应的字符串
		char *e =
		  vec_elt (t->enum_strings_vector, n_bytes == 8 ? l : i);
		s = format (s, arg_format, e);
	      }
	    else if (a[0] == 'T')
	      {
	        //取字符串表
		char *e =
		  vec_elt_at_index (em->string_table, n_bytes == 8 ? l : i);
		s = format (s, arg_format, e);
	      }
	    //按整数格式解析l
	    else if (n_bytes == 8)
	      s = format (s, arg_format, l);
	    else
	      s = format (s, arg_format, i);
	  }
	  break;

	case 'f':
	  {
	    f64 x = 0;
	    if (n_bytes == 4)
	      x = clib_mem_unaligned (d, f32);
	    else if (n_bytes == 8)
	      x = clib_mem_unaligned (d, f64);
	    else
	      ASSERT (0);
	    s = format (s, arg_format, x);
	  }
	  break;

	case 's':
	  s = format (s, arg_format, d);
	  if (n_bytes == 0)
	    n_bytes = strlen (d) + 1;
	  break;

	default:
	  ASSERT (0);
	  break;
	}

      ASSERT (n_digits > 0 && n_digits <= 2);
      a += 1 + n_digits;
      d += n_bytes;
    }

  return s;
}

//按收3个参数;1.em 用于提供event元数据;2.e 对应的event
//用于输出event对应的track名称
u8 *
format_elog_track_name (u8 * s, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  elog_event_t *e = va_arg (*va, elog_event_t *);
  elog_track_t *t = vec_elt_at_index (em->tracks, e->track);
  return format (s, "%s", t->name);
}

u8 *
format_elog_track (u8 * s, va_list * args)
{
  elog_main_t *em = va_arg (*args, elog_main_t *);
  f64 dt = va_arg (*args, f64);
  int track_index = va_arg (*args, int);
  elog_event_t *e, *es;
  u8 indent;

  indent = format_get_indent (s) + 1;

  es = elog_peek_events (em);
  vec_foreach (e, es)
  {
    if (e->track != track_index)
      continue;
    //按格式输出event发生时的时间＋dt,输出event对应的格式后字符串
    s = format (s, "%U%18.9f: %U\n", format_white_space, indent, e->time + dt,
		format_elog_event, em, e);
  }
  vec_free (es);
  return s;
}

//获取当前时间填充et
void
elog_time_now (elog_time_stamp_t * et)
{
  u64 cpu_time_now, os_time_now_nsec;
  struct timespec ts;

#ifdef CLIB_UNIX
  {
#include <sys/syscall.h>
#ifdef __APPLE__
    clock_gettime (CLOCK_REALTIME, &ts);
#else
    syscall (SYS_clock_gettime, CLOCK_REALTIME, &ts);
#endif
    cpu_time_now = clib_cpu_time_now ();
    /* Subtract 3/30/2017's worth of seconds to retain precision */
    os_time_now_nsec = 1e9 * (ts.tv_sec - 1490885108) + ts.tv_nsec;
  }
#else
  cpu_time_now = clib_cpu_time_now ();
  os_time_now_nsec = 0;
#endif

  et->cpu = cpu_time_now;
  et->os_nsec = os_time_now_nsec;
}

//os时间差
always_inline i64
elog_time_stamp_diff_os_nsec (elog_time_stamp_t * t1, elog_time_stamp_t * t2)
{
  return (i64) t1->os_nsec - (i64) t2->os_nsec;
}

//cpu时间差
always_inline i64
elog_time_stamp_diff_cpu (elog_time_stamp_t * t1, elog_time_stamp_t * t2)
{
  return (i64) t1->cpu - (i64) t2->cpu;
}


always_inline f64
elog_nsec_per_clock (elog_main_t * em)
{
  return ((f64) elog_time_stamp_diff_os_nsec (&em->serialize_time,
					      &em->init_time)
	  / (f64) elog_time_stamp_diff_cpu (&em->serialize_time,
					    &em->init_time));
}

//elog空间申请
void
elog_alloc (elog_main_t * em, u32 n_events)
{
  //如果已初始化，则释放，并重新初始化
  if (em->event_ring)
    vec_free (em->event_ring);

  /* Ring size must be a power of 2. */
  em->event_ring_size = n_events = max_pow2 (n_events);

  /* Leave an empty ievent at end so we can always speculatively write
     and event there (possibly a long form event). */
  vec_resize_aligned (em->event_ring, n_events, CLIB_CACHE_LINE_BYTES);
}

//elog初始化
void
elog_init (elog_main_t * em, u32 n_events)
{
  clib_memset (em, 0, sizeof (em[0]));

  em->lock = 0;

  //申请支持n_events个时间的elog
  if (n_events > 0)
    elog_alloc (em, n_events);

  clib_time_init (&em->cpu_timer);

  em->n_total_events_disable_limit = ~0;

  /* Make track 0. */
  em->default_track.name = "default";
  elog_track_register (em, &em->default_track);

  elog_time_now (&em->init_time);
}

/* Returns number of events in ring and start index. */
//返回ring中总事件数目，及起始索引
static uword
elog_event_range (elog_main_t * em, uword * lo)
{
  uword l = em->event_ring_size;
  u64 i = em->n_total_events;

  /* Ring never wrapped? */
  //当前事件数量未达或已达到最大值
  if (i <= (u64) l)
    {
      if (lo)
	*lo = 0;
      return i;
    }
  else
    {
      //事件数量超过l时，取余求首个位置
      if (lo)
	*lo = i & (l - 1);
      return l;
    }
}

//自em中peek所有事件（仅peek,不出队）
elog_event_t *
elog_peek_events (elog_main_t * em)
{
  elog_event_t *e, *f, *es = 0;
  uword i, j, n;

  //获取事件数目，及左侧索引j
  n = elog_event_range (em, &j);
  for (i = 0; i < n; i++)
    {
      vec_add2 (es, e, 1);//空间申请

      //获取j号元素
      f = vec_elt_at_index (em->event_ring, j);
      //将j号元素给值给e
      e[0] = f[0];

      /* Convert absolute time from cycles to seconds from start. */
      //时间设置
      e->time =
	(e->time_cycles -
	 em->init_time.cpu) * em->cpu_timer.seconds_per_clock;

      //指向下一个元素
      j = (j + 1) & (em->event_ring_size - 1);
    }

  return es;
}

/* Add a formatted string to the string table. */
//向em->string_table中添加一个格式化后的字符串
u32
elog_string (elog_main_t * em, char *fmt, ...)
{
  u32 offset;
  va_list va;

  va_start (va, fmt);
  offset = vec_len (em->string_table);
  em->string_table = (char *) va_format ((u8 *) em->string_table, fmt, &va);
  va_end (va);

  /* Null terminate string if it is not already. */
  if (vec_end (em->string_table)[-1] != 0)
    vec_add1 (em->string_table, 0);

  return offset;
}

//提取em中event设置在em->events
elog_event_t *
elog_get_events (elog_main_t * em)
{
  if (!em->events)
    em->events = elog_peek_events (em);
  return em->events;
}

static void
maybe_fix_string_table_offset (elog_event_t * e,
			       elog_event_type_t * t, u32 offset)
{
  void *d = (u8 *) e->data;
  char *a;

  if (offset == 0)
    return;

  a = t->format_args;

  while (1)
    {
      uword n_bytes = 0, n_digits;

      //遇到字符串终止符，退出
      if (a[0] == 0)
	break;

      /* Don't go past end of event data. */
      ASSERT (d < (void *) (e->data + sizeof (e->data)));

      //获取2位的整数，解释为参数位宽
      n_digits = parse_2digit_decimal (a + 1, &n_bytes);
      switch (a[0])
	{
	case 'T':
	  ASSERT (n_bytes == 4);
	  clib_mem_unaligned (d, u32) += offset;//按类型跳offset
	  break;

	case 'i':
	case 't':
	case 'f':
	case 's':
	  break;

	default:
	  ASSERT (0);
	  break;
	}

      ASSERT (n_digits > 0 && n_digits <= 2);
      a += 1 + n_digits;//跳过参数
      d += n_bytes;//跳过
    }
}

//两个event比较
static int
elog_cmp (void *a1, void *a2)
{
  elog_event_t *e1 = a1;
  elog_event_t *e2 = a2;

  if (e1->time < e2->time)
    return -1;

  if (e1->time > e2->time)
    return 1;

  return 0;
}

/*
 * merge two event logs. Complicated and cranky.
 */
void
elog_merge (elog_main_t * dst, u8 * dst_tag, elog_main_t * src, u8 * src_tag,
	    f64 align_tweak)
{
  elog_event_t *e;
  uword l;
  u32 string_table_offset_for_src_events;
  u32 track_offset_for_src_tracks;
  elog_track_t newt;
  int i;

  clib_memset (&newt, 0, sizeof (newt));

  /* Acquire src and dst events */
  elog_get_events (src);
  elog_get_events (dst);

  //dst对应的字符表大小
  string_table_offset_for_src_events = vec_len (dst->string_table);
  vec_append (dst->string_table, src->string_table);//合入字符表

  l = vec_len (dst->events);
  vec_append (dst->events, src->events);//合入event

  /* Prepend the supplied tag (if any) to all dst track names */
  //更新dst的tracks名称
  if (dst_tag)
    {
      for (i = 0; i < vec_len (dst->tracks); i++)
	{
	  elog_track_t *t = vec_elt_at_index (dst->tracks, i);
	  char *new_name;

	  new_name = (char *) format (0, "%s:%s%c", dst_tag, t->name, 0);
	  vec_free (t->name);
	  t->name = new_name;
	}
    }

  /*
   * Remember where we started allocating new tracks while merging
   */
  //src对应的tracks表大小
  track_offset_for_src_tracks = vec_len (dst->tracks);

  /* Copy / tag source tracks */
  //更新src tracks的名称，并将其注册进dst中
  for (i = 0; i < vec_len (src->tracks); i++)
    {
      elog_track_t *t = vec_elt_at_index (src->tracks, i);
      if (src_tag)
	newt.name = (char *) format (0, "%s:%s%c", src_tag, t->name, 0);
      else
	newt.name = (char *) format (0, "%s%c", t->name, 0);
      (void) elog_track_register (dst, &newt);
      vec_free (newt.name);
    }

  /* Across all (copied) src events... */
  for (e = dst->events + l; e < vec_end (dst->events); e++)
    {
      elog_event_type_t *t = vec_elt_at_index (src->event_types, e->type);

      /* Remap type from src -> dst. */
      e->type = find_or_create_type (dst, t);

      /* Remap string table offsets for 'T' format args */
      maybe_fix_string_table_offset (e, t,
				     string_table_offset_for_src_events);

      /* Remap track */
      e->track += track_offset_for_src_tracks;
    }

  /* Adjust event times for relative starting times of event streams. */
  {
    f64 dt_event, dt_os_nsec, dt_clock_nsec;

    /* Set clock parameters if dst was not generated by unserialize. */
    if (dst->serialize_time.cpu == 0)
      {
	dst->init_time = src->init_time;
	dst->serialize_time = src->serialize_time;
	dst->nsec_per_cpu_clock = src->nsec_per_cpu_clock;
      }

    dt_os_nsec =
      elog_time_stamp_diff_os_nsec (&src->init_time, &dst->init_time);

    dt_event = dt_os_nsec;
    dt_clock_nsec =
      (elog_time_stamp_diff_cpu (&src->init_time, &dst->init_time) * .5 *
       (dst->nsec_per_cpu_clock + src->nsec_per_cpu_clock));

    /*
     * Heuristic to see if src/dst came from same time source.
     * If frequencies are "the same" and os clock and cpu clock agree
     * to within 100e-9 secs about time difference between src/dst
     * init_time, then we use cpu clock.  Otherwise we use OS clock.
     *
     * When merging event logs from different systems, time paradoxes
     * at the O(1ms) level are to be expected. Hence, the "align_tweak"
     * parameter. If two events logged on different processors are known
     * to occur in a specific order - and with a reasonably-estimated
     * interval - supply a non-zero "align_tweak" parameter
     */
    if (fabs (src->nsec_per_cpu_clock - dst->nsec_per_cpu_clock) < 1e-2
	&& fabs (dt_os_nsec - dt_clock_nsec) < 100)
      dt_event = dt_clock_nsec;

    /* Convert to seconds. */
    dt_event *= 1e-9;

    /*
     * Move the earlier set of events later, to avoid creating
     * events which precede the Big Bang (aka have negative timestamps).
     *
     * Not to any scale, we have something like the following picture:
     *
     * DST capture start point
     *       ^
     *       +--- dt_event --+
     *                       v
     *                 SRC capture start point
     *
     * In this case dt_event is positive, src started after dst,
     * to put src events onto a common timebase we have to move them
     * forward in time. Naturally, the opposite case is
     * possible, too: dt_event will be negative, and so we have to
     * move dst events forward in time by the |dt_event|.
     * In both cases, we add align_tweak.
     */
    if (dt_event > 0)
      {
	/* Src started after dst. */
	for (e = dst->events + l; e < vec_end (dst->events); e++)
	  e->time += dt_event + align_tweak;
      }
    else
      {
	/* Dst started after src. */
	dt_event = -dt_event;
	for (e = dst->events + 0; e < dst->events + l; e++)
	  e->time += dt_event + align_tweak;
      }
  }

  /* Sort events by increasing time. */
  vec_sort_with_function (dst->events, elog_cmp);

  dst->n_total_events = vec_len (dst->events);

  /* Recreate the event ring or the results won't serialize */
  {
    int i;

    ASSERT (dst->cpu_timer.seconds_per_clock);

    elog_alloc (dst, vec_len (dst->events));
    for (i = 0; i < vec_len (dst->events); i++)
      {
	elog_event_t *es, *ed;

	es = dst->events + i;
	ed = dst->event_ring + i;

	ed[0] = es[0];
      }
  }
}

//将elog event打成串
static void
serialize_elog_event (serialize_main_t * m, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  elog_event_t *e = va_arg (*va, elog_event_t *);
  elog_event_type_t *t = vec_elt_at_index (em->event_types, e->type);
  u8 *d = e->data;
  u8 *p = (u8 *) t->format_args;

  serialize_integer (m, e->type, sizeof (e->type));
  serialize_integer (m, e->track, sizeof (e->track));
  serialize (m, serialize_f64, e->time);

  while (*p)
    {
      uword n_digits, n_bytes = 0;

      n_digits = parse_2digit_decimal ((char *) p + 1, &n_bytes);

      switch (p[0])
	{
	case 'i':
	case 't':
	case 'T':
	  if (n_bytes == 1)
	    serialize_integer (m, d[0], sizeof (u8));
	  else if (n_bytes == 2)
	    serialize_integer (m, clib_mem_unaligned (d, u16), sizeof (u16));
	  else if (n_bytes == 4)
	    serialize_integer (m, clib_mem_unaligned (d, u32), sizeof (u32));
	  else if (n_bytes == 8)
	    serialize (m, serialize_64, clib_mem_unaligned (d, u64));
	  else
	    ASSERT (0);
	  break;

	case 's':
	  serialize_cstring (m, (char *) d);
	  if (n_bytes == 0)
	    n_bytes = strlen ((char *) d) + 1;
	  break;

	case 'f':
	  if (n_bytes == 4)
	    serialize (m, serialize_f32, clib_mem_unaligned (d, f32));
	  else if (n_bytes == 8)
	    serialize (m, serialize_f64, clib_mem_unaligned (d, f64));
	  else
	    ASSERT (0);
	  break;

	default:
	  ASSERT (0);
	  break;
	}

      p += 1 + n_digits;
      d += n_bytes;
    }
}

//将串解成elog event
static void
unserialize_elog_event (serialize_main_t * m, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  elog_event_t *e = va_arg (*va, elog_event_t *);
  elog_event_type_t *t;
  u8 *p, *d;

  {
    u16 tmp[2];

    unserialize_integer (m, &tmp[0], sizeof (e->type));
    unserialize_integer (m, &tmp[1], sizeof (e->track));

    e->type = tmp[0];
    e->track = tmp[1];

    /* Make sure it fits. */
    ASSERT (e->type == tmp[0]);
    ASSERT (e->track == tmp[1]);
  }

  t = vec_elt_at_index (em->event_types, e->type);

  unserialize (m, unserialize_f64, &e->time);

  d = e->data;
  p = (u8 *) t->format_args;

  while (p && *p)
    {
      uword n_digits, n_bytes = 0;
      u32 tmp;

      n_digits = parse_2digit_decimal ((char *) p + 1, &n_bytes);

      switch (p[0])
	{
	case 'i':
	case 't':
	case 'T':
	  if (n_bytes == 1)
	    {
	      unserialize_integer (m, &tmp, sizeof (u8));
	      d[0] = tmp;
	    }
	  else if (n_bytes == 2)
	    {
	      unserialize_integer (m, &tmp, sizeof (u16));
	      clib_mem_unaligned (d, u16) = tmp;
	    }
	  else if (n_bytes == 4)
	    {
	      unserialize_integer (m, &tmp, sizeof (u32));
	      clib_mem_unaligned (d, u32) = tmp;
	    }
	  else if (n_bytes == 8)
	    {
	      u64 x;
	      unserialize (m, unserialize_64, &x);
	      clib_mem_unaligned (d, u64) = x;
	    }
	  else
	    ASSERT (0);
	  break;

	case 's':
	  {
	    char *t;
	    unserialize_cstring (m, &t);
	    if (n_bytes == 0)
	      n_bytes = strlen (t) + 1;
	    clib_memcpy (d, t, clib_min (n_bytes, vec_len (t)));
	    vec_free (t);
	    break;
	  }

	case 'f':
	  if (n_bytes == 4)
	    {
	      f32 x;
	      unserialize (m, unserialize_f32, &x);
	      clib_mem_unaligned (d, f32) = x;
	    }
	  else if (n_bytes == 8)
	    {
	      f64 x;
	      unserialize (m, unserialize_f64, &x);
	      clib_mem_unaligned (d, f64) = x;
	    }
	  else
	    ASSERT (0);
	  break;

	default:
	  ASSERT (0);
	  break;
	}

      p += 1 + n_digits;
      d += n_bytes;
    }
}

//将elog event type打成串
static void
serialize_elog_event_type (serialize_main_t * m, va_list * va)
{
  elog_event_type_t *t = va_arg (*va, elog_event_type_t *);
  int n = va_arg (*va, int);
  int i, j;
  for (i = 0; i < n; i++)
    {
      serialize_cstring (m, t[i].format);
      serialize_cstring (m, t[i].format_args);
      serialize_integer (m, t[i].type_index_plus_one,
			 sizeof (t->type_index_plus_one));
      serialize_integer (m, t[i].n_enum_strings,
			 sizeof (t[i].n_enum_strings));
      for (j = 0; j < t[i].n_enum_strings; j++)
	serialize_cstring (m, t[i].enum_strings_vector[j]);
    }
}

static void
unserialize_elog_event_type (serialize_main_t * m, va_list * va)
{
  elog_event_type_t *t = va_arg (*va, elog_event_type_t *);
  int n = va_arg (*va, int);
  int i, j;
  for (i = 0; i < n; i++)
    {
      unserialize_cstring (m, &t[i].format);
      unserialize_cstring (m, &t[i].format_args);
      unserialize_integer (m, &t[i].type_index_plus_one,
			   sizeof (t->type_index_plus_one));
      unserialize_integer (m, &t[i].n_enum_strings,
			   sizeof (t[i].n_enum_strings));
      vec_resize (t[i].enum_strings_vector, t[i].n_enum_strings);
      for (j = 0; j < t[i].n_enum_strings; j++)
	unserialize_cstring (m, &t[i].enum_strings_vector[j]);
    }
}

static void
serialize_elog_track (serialize_main_t * m, va_list * va)
{
  elog_track_t *t = va_arg (*va, elog_track_t *);
  int n = va_arg (*va, int);
  int i;
  for (i = 0; i < n; i++)
    {
      serialize_cstring (m, t[i].name);
    }
}

static void
unserialize_elog_track (serialize_main_t * m, va_list * va)
{
  elog_track_t *t = va_arg (*va, elog_track_t *);
  int n = va_arg (*va, int);
  int i;
  for (i = 0; i < n; i++)
    {
      unserialize_cstring (m, &t[i].name);
    }
}

static void
serialize_elog_time_stamp (serialize_main_t * m, va_list * va)
{
  elog_time_stamp_t *st = va_arg (*va, elog_time_stamp_t *);
  serialize (m, serialize_64, st->os_nsec);
  serialize (m, serialize_64, st->cpu);
}

static void
unserialize_elog_time_stamp (serialize_main_t * m, va_list * va)
{
  elog_time_stamp_t *st = va_arg (*va, elog_time_stamp_t *);
  unserialize (m, unserialize_64, &st->os_nsec);
  unserialize (m, unserialize_64, &st->cpu);
}

static char *elog_serialize_magic = "elog v0";

void
serialize_elog_main (serialize_main_t * m, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  int flush_ring = va_arg (*va, int);
  elog_event_t *e;

  serialize_magic (m, elog_serialize_magic, strlen (elog_serialize_magic));

  serialize_integer (m, em->event_ring_size, sizeof (u32));

  elog_time_now (&em->serialize_time);
  serialize (m, serialize_elog_time_stamp, &em->serialize_time);
  serialize (m, serialize_elog_time_stamp, &em->init_time);

  vec_serialize (m, em->event_types, serialize_elog_event_type);
  vec_serialize (m, em->tracks, serialize_elog_track);
  vec_serialize (m, em->string_table, serialize_vec_8);

  /* Free old events (cached) in case they have changed. */
  if (flush_ring)
    {
      vec_free (em->events);
      elog_get_events (em);
    }

  serialize_integer (m, vec_len (em->events), sizeof (u32));

  /* SMP logs can easily have local time paradoxes... */
  vec_sort_with_function (em->events, elog_cmp);

  vec_foreach (e, em->events) serialize (m, serialize_elog_event, em, e);
}

void
unserialize_elog_main (serialize_main_t * m, va_list * va)
{
  elog_main_t *em = va_arg (*va, elog_main_t *);
  uword i;
  u32 rs;

  unserialize_check_magic (m, elog_serialize_magic,
			   strlen (elog_serialize_magic));

  unserialize_integer (m, &rs, sizeof (u32));
  em->event_ring_size = rs;
  elog_init (em, em->event_ring_size);

  unserialize (m, unserialize_elog_time_stamp, &em->serialize_time);
  unserialize (m, unserialize_elog_time_stamp, &em->init_time);
  em->nsec_per_cpu_clock = elog_nsec_per_clock (em);

  vec_unserialize (m, &em->event_types, unserialize_elog_event_type);
  for (i = 0; i < vec_len (em->event_types); i++)
    new_event_type (em, i);

  vec_unserialize (m, &em->tracks, unserialize_elog_track);
  vec_unserialize (m, &em->string_table, unserialize_vec_8);

  {
    u32 ne;
    elog_event_t *e;

    unserialize_integer (m, &ne, sizeof (u32));
    vec_resize (em->events, ne);
    vec_foreach (e, em->events)
      unserialize (m, unserialize_elog_event, em, e);
  }
}

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
