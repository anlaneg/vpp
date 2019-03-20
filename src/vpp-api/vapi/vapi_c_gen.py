#!/usr/bin/env python2
#encoding:utf-8

import argparse
import os
import sys
import logging
from vapi_json_parser import Field, Struct, Enum, Union, Message, JsonParser,\
    SimpleType, StructType, Alias


class CField(Field):
    def get_c_name(self):
        return "vapi_type_%s" % self.name

    def get_c_def(self):
        if self.len is not None:
            #数组类型
            return "%s %s[%d];" % (self.type.get_c_name(), self.name, self.len)
        else:
            #普通变量类型
            return "%s %s;" % (self.type.get_c_name(), self.name)

    def get_swap_to_be_code(self, struct, var):
        if self.len is not None:
            if self.len > 0:
                #当前属于数组变量，故需要针对每个元素进行主机序至大端序转换
                return "do { unsigned i; for (i = 0; i < %d; ++i) { %s } }"\
                    " while(0);" % (
                        self.len,
                        #生成此类型执行hton转换时代码
                        self.type.get_swap_to_be_code(struct, "%s[i]" % var))
            else:
                #当前属普通变量，仅需要针对变量自身完成转换即可（需要考虑nelem_field成员的数目）
                if self.nelem_field.needs_byte_swap():
                    nelem_field = "%s(%s%s)" % (
                        self.nelem_field.type.get_swap_to_host_func_name(),
                        struct, self.nelem_field.name)
                else:
                    nelem_field = "%s%s" % (struct, self.nelem_field.name)
                return (
                    "do { unsigned i; for (i = 0; i < %s; ++i) { %s } }"
                    " while(0);" %
                    (nelem_field, self.type.get_swap_to_be_code(
                        struct, "%s[i]" % var)))
        return self.type.get_swap_to_be_code(struct, "%s" % var)

    def get_swap_to_host_code(self, struct, var):
        if self.len is not None:
            if self.len > 0:
                return "do { unsigned i; for (i = 0; i < %d; ++i) { %s } }"\
                    " while(0);" % (
                        self.len,
                        self.type.get_swap_to_host_code(struct, "%s[i]" % var))
            else:
                # nelem_field already swapped to host here...
                return (
                    "do { unsigned i; for (i = 0; i < %s%s; ++i) { %s } }"
                    " while(0);" %
                    (struct, self.nelem_field.name,
                     self.type.get_swap_to_host_code(
                         struct, "%s[i]" % var)))
        return self.type.get_swap_to_host_code(struct, "%s" % var)

    def needs_byte_swap(self):
        return self.type.needs_byte_swap()

    #返回成员length名称
    def get_vla_field_length_name(self, path):
        return "%s_%s_array_size" % ("_".join(path), self.name)

    def get_alloc_vla_param_names(self, path):
        if self.is_vla():
            #如果是可变长数组，则返回成员length名称
            result = [self.get_vla_field_length_name(path)]
        else:
            #返回空
            result = []
        
        #如果此类型有可变长数组，则继续向下层获取
        if self.type.has_vla():
            t = self.type.get_alloc_vla_param_names(path + [self.name])
            result.extend(t)
        return result

    def get_vla_calc_size_code(self, prefix, path):
        if self.is_vla():
            #取出可变长数组大小及其每个元素的大小
            result = ["sizeof(%s.%s[0]) * %s" % (
                ".".join([prefix] + path),
                self.name,
                self.get_vla_field_length_name(path))]
        else:
            result = []
        if self.type.has_vla():
            #如果其类型包含可变长数组，则递归计算
            t = self.type.get_vla_calc_size_code(prefix, path + [self.name])
            result.extend(t)
        return result

    def get_vla_assign_code(self, prefix, path):
        result = []
        if self.is_vla():
            result.append("%s.%s = %s" % (
                ".".join([prefix] + path),
                self.nelem_field.name,
                self.get_vla_field_length_name(path)))
        if self.type.has_vla():
            t = self.type.get_vla_assign_code(prefix, path + [self.name])
            result.extend(t)
        return result


class CAlias(CField):
    def get_c_name(self):
        return "vapi_type_%s" % self.name

    def get_c_def(self):
        if self.len is not None:
            return "typedef %s vapi_type_%s[%d];" % (
                self.type.get_c_name(), self.name, self.len)
        else:
            return "typedef %s vapi_type_%s;" % (
                self.type.get_c_name(), self.name)


class CStruct(Struct):
    def get_c_def(self):
        return "\n".join([
            "typedef struct __attribute__((__packed__)) {\n%s" % (
                "\n".join(["  %s" % x.get_c_def()
                           for x in self.fields])),
            "} %s;" % self.get_c_name()])

    def get_vla_assign_code(self, prefix, path):
        return [x for f in self.fields if f.has_vla()
                for x in f.get_vla_assign_code(prefix, path)]

    #alloc空间申请参数列表收集
    def get_alloc_vla_param_names(self, path):
        return [x for f in self.fields
                if f.has_vla()
                for x in f.get_alloc_vla_param_names(path)]

    def get_vla_calc_size_code(self, prefix, path):
        return [x for f in self.fields if f.has_vla()
                for x in f.get_vla_calc_size_code(prefix, path)]


#实现C语言的基础类型映射
class CSimpleType (SimpleType):

    swap_to_be_dict = {
        'i16': 'htobe16', 'u16': 'htobe16',
        'i32': 'htobe32', 'u32': 'htobe32',
        'i64': 'htobe64', 'u64': 'htobe64',
    }

    swap_to_host_dict = {
        'i16': 'be16toh', 'u16': 'be16toh',
        'i32': 'be32toh', 'u32': 'be32toh',
        'i64': 'be64toh', 'u64': 'be64toh',
    }

    #类型名称
    def get_c_name(self):
        return self.name

    #转换到大端需要的函数
    def get_swap_to_be_func_name(self):
        return self.swap_to_be_dict[self.name]

    #由大端转换到local需要的函数
    def get_swap_to_host_func_name(self):
        return self.swap_to_host_dict[self.name]

    #将结构体struct中的成员var由主机序转换为大端所需要的代码
    def get_swap_to_be_code(self, struct, var, cast=None):
        x = "%s%s" % (struct, var)
        return "%s = %s%s(%s);" % (x,
                                   #如果需要进行类型转换，则添加强制转换
                                   "(%s)" % cast if cast else "",
                                   #转换为大端对应的函数
                                   self.get_swap_to_be_func_name(), x)

    #将结构体struct中的成员var由大端转换为主机序所需要的代码
    def get_swap_to_host_code(self, struct, var, cast=None):
        x = "%s%s" % (struct, var)
        return "%s = %s%s(%s);" % (x,
                                   "(%s)" % cast if cast else "",
                                   self.get_swap_to_host_func_name(), x)

    #检查指定类型是否支持字节序转换
    def needs_byte_swap(self):
        try:
            self.get_swap_to_host_func_name()
            return True
        except KeyError:
            pass
        return False


class CEnum(Enum):
    #定义枚举结构体名称
    def get_c_name(self):
        return "vapi_enum_%s" % self.name

    #定义枚举的C定义（生成C语言对应的枚举定义）
    def get_c_def(self):
        return "typedef enum {\n%s\n} %s;" % (
            #定义各成员及其取值
            "\n".join(["  %s = %s," % (i, j) for i, j in self.value_pairs]),
            self.get_c_name()
        )

    #检查此类型是否支持字节序变换
    def needs_byte_swap(self):
        return self.type.needs_byte_swap()

    def get_swap_to_be_code(self, struct, var):
        return self.type.get_swap_to_be_code(struct, var, self.get_c_name())

    def get_swap_to_host_code(self, struct, var):
        return self.type.get_swap_to_host_code(struct, var, self.get_c_name())


class CUnion(Union):
    #返回c对应的union结构体名称
    def get_c_name(self):
        return "vapi_union_%s" % self.name

    #定义union结构体
    def get_c_def(self):
        return "typedef union {\n%s\n} %s;" % (
            #定义union的类型及名称
            "\n".join(["  %s %s;" % (i.get_c_name(), j)
                       for i, j in self.type_pairs]),
            self.get_c_name()
        )

    #不支持字节序变换
    def needs_byte_swap(self):
        return False


class CStructType (StructType, CStruct):
    #结构体名称
    def get_c_name(self):
        return "vapi_type_%s" % self.name

    #结构体主机序转网络序
    def get_swap_to_be_func_name(self):
        return "%s_hton" % self.get_c_name()

    #结构体网络序转主机序
    def get_swap_to_host_func_name(self):
        return "%s_ntoh" % self.get_c_name()

    #结构体转网络序函数声明
    def get_swap_to_be_func_decl(self):
        return "void %s(%s *msg)" % (
            self.get_swap_to_be_func_name(), self.get_c_name())

    #结构体整体转网络序代码
    def get_swap_to_be_func_def(self):
        return "%s\n{\n%s\n}" % (
            self.get_swap_to_be_func_decl(),
            "\n".join([
                "  %s" % p.get_swap_to_be_code("msg->", "%s" % p.name)
                for p in self.fields if p.needs_byte_swap()]),
        )

    def get_swap_to_host_func_decl(self):
        return "void %s(%s *msg)" % (
            self.get_swap_to_host_func_name(), self.get_c_name())

    def get_swap_to_host_func_def(self):
        return "%s\n{\n%s\n}" % (
            self.get_swap_to_host_func_decl(),
            "\n".join([
                "  %s" % p.get_swap_to_host_code("msg->", "%s" % p.name)
                for p in self.fields if p.needs_byte_swap()]),
        )

    def get_swap_to_be_code(self, struct, var):
        return "%s(&%s%s);" % (self.get_swap_to_be_func_name(), struct, var)

    def get_swap_to_host_code(self, struct, var):
        return "%s(&%s%s);" % (self.get_swap_to_host_func_name(), struct, var)

    #如果所有fields支持字节序转换，则结构体支持字节序转换
    def needs_byte_swap(self):
        for f in self.fields:
            if f.needs_byte_swap():
                return True
        return False


class CMessage (Message):
    def __init__(self, logger, definition, json_parser):
        super(CMessage, self).__init__(logger, definition, json_parser)
        self.payload_members = [
            "  %s" % p.get_c_def()
            for p in self.fields
            if p.type != self.header
        ]

    def has_payload(self):
        return len(self.payload_members) > 0

    #获取消息id号名称（加上message名称即可）
    def get_msg_id_name(self):
        return "vapi_msg_id_%s" % self.name

    def get_c_name(self):
        return "vapi_msg_%s" % self.name

    def get_payload_struct_name(self):
        return "vapi_payload_%s" % self.name

    #定义alloc函数名称
    def get_alloc_func_name(self):
        return "vapi_alloc_%s" % self.name

    #alloc空间申请参数列表收集
    def get_alloc_vla_param_names(self):
        #针对每个f检查其是否为可变长度，如果f为可变长度，则收集其可变长度参数名称
        return [x for f in self.fields
                if f.has_vla() #如果字段是可变长度数组
                for x in f.get_alloc_vla_param_names([])]

    #生成alloc函数声明
    def get_alloc_func_decl(self):
        return "%s* %s(struct vapi_ctx_s *ctx%s)" % (
            self.get_c_name(),
            self.get_alloc_func_name(),
            "".join([", size_t %s" % n for n in
                     #收集alloc所需要的函数参数
                     self.get_alloc_vla_param_names()]))

    def get_alloc_func_def(self):
        extra = []
        if self.header.has_field('client_index'):
            #消息体头部含client_index字段,则返回client index
            extra.append(
                "  msg->header.client_index = vapi_get_client_index(ctx);")
            
        #如果有context字段，则置context字段为0
        if self.header.has_field('context'):
            extra.append("  msg->header.context = 0;")
        return "\n".join([
            "%s" % self.get_alloc_func_decl(),
            "{",
            "  %s *msg = NULL;" % self.get_c_name(),
            #为了为此message申请空间，这里需要先计算出总空间，先计算结构体大小
            "  const size_t size = sizeof(%s)%s;" % (
                self.get_c_name(),
                #再计算其它可变成员需要的大小
                "".join([" + %s" % x for f in self.fields if f.has_vla()
                         for x in f.get_vla_calc_size_code("msg->payload",
                                                           [])])),
            "  /* cast here required to play nicely with C++ world ... */",
            "  msg = (%s*)vapi_msg_alloc(ctx, size);" % self.get_c_name(),
            "  if (!msg) {",
            "    return NULL;",
            "  }",
        ] + extra + [
            "  msg->header._vl_msg_id = vapi_lookup_vl_msg_id(ctx, %s);" %
            self.get_msg_id_name(),
            "".join(["  %s;\n" % line
                     for f in self.fields if f.has_vla()
                     for line in f.get_vla_assign_code("msg->payload", [])]),
            "  return msg;",
            "}"])

    def get_calc_msg_size_func_name(self):
        return "vapi_calc_%s_msg_size" % self.name

    def get_calc_msg_size_func_decl(self):
        return "uword %s(%s *msg)" % (
            self.get_calc_msg_size_func_name(),
            self.get_c_name())

    def get_calc_msg_size_func_def(self):
        return "\n".join([
            "%s" % self.get_calc_msg_size_func_decl(),
            "{",
            "  return sizeof(*msg)%s;" %
            "".join(["+ msg->payload.%s * sizeof(msg->payload.%s[0])" % (
                    f.nelem_field.name,
                    f.name)
                for f in self.fields
                if f.nelem_field is not None
            ]),
            "}",
        ])

    def get_c_def(self):
        if self.has_payload():
            return "\n".join([
                "typedef struct __attribute__ ((__packed__)) {",
                "%s " %
                "\n".join(self.payload_members),
                "} %s;" % self.get_payload_struct_name(),
                "",
                "typedef struct __attribute__ ((__packed__)) {",
                ("  %s %s;" % (self.header.get_c_name(),
                               self.fields[0].name)
                    if self.header is not None else ""),
                "  %s payload;" % self.get_payload_struct_name(),
                "} %s;" % self.get_c_name(), ])
        else:
            return "\n".join([
                "typedef struct __attribute__ ((__packed__)) {",
                ("  %s %s;" % (self.header.get_c_name(),
                               self.fields[0].name)
                    if self.header is not None else ""),
                "} %s;" % self.get_c_name(), ])

    #生成payload转主机序函数名
    def get_swap_payload_to_host_func_name(self):
        return "%s_payload_ntoh" % self.get_c_name()

    #生成payload转网络序函数名
    def get_swap_payload_to_be_func_name(self):
        return "%s_payload_hton" % self.get_c_name()

    def get_swap_payload_to_host_func_decl(self):
        return "void %s(%s *payload)" % (
            self.get_swap_payload_to_host_func_name(),
            self.get_payload_struct_name())

    #针对payload结构体，生成转网络序
    def get_swap_payload_to_be_func_decl(self):
        return "void %s(%s *payload)" % (
            self.get_swap_payload_to_be_func_name(),
            self.get_payload_struct_name())

    #生成payload结构体转网络序代码
    def get_swap_payload_to_be_func_def(self):
        return "%s\n{\n%s\n}" % (
            self.get_swap_payload_to_be_func_decl(),
            "\n".join([
                "  %s" % p.get_swap_to_be_code("payload->", "%s" % p.name)
                for p in self.fields
                if p.needs_byte_swap() and p.type != self.header]),
        )

    def get_swap_payload_to_host_func_def(self):
        return "%s\n{\n%s\n}" % (
            self.get_swap_payload_to_host_func_decl(),
            "\n".join([
                "  %s" % p.get_swap_to_host_code("payload->", "%s" % p.name)
                for p in self.fields
                if p.needs_byte_swap() and p.type != self.header]),
        )

    def get_swap_to_host_func_name(self):
        return "%s_ntoh" % self.get_c_name()

    def get_swap_to_be_func_name(self):
        return "%s_hton" % self.get_c_name()

    #生成message转换为主机序函数
    def get_swap_to_host_func_decl(self):
        return "void %s(%s *msg)" % (
            self.get_swap_to_host_func_name(), self.get_c_name())

    #生成消息结构体转网络序函数名
    def get_swap_to_be_func_decl(self):
        return "void %s(%s *msg)" % (
            self.get_swap_to_be_func_name(), self.get_c_name())

    #生成消息结构体转网络序函数代码（先debug,再转header,再转payload)
    def get_swap_to_be_func_def(self):
        return "\n".join([
            "%s" % self.get_swap_to_be_func_decl(),
            "{",
            ("  VAPI_DBG(\"Swapping `%s'@%%p to big endian\", msg);" %
                self.get_c_name()),
            "  %s(&msg->header);" % self.header.get_swap_to_be_func_name()
            if self.header is not None else "",
            "  %s(&msg->payload);" % self.get_swap_payload_to_be_func_name()
            if self.has_payload() else "",
            "}",
        ])

    #生成针对message的转主机序函数
    def get_swap_to_host_func_def(self):
        return "\n".join([
            "%s" % self.get_swap_to_host_func_decl(),
            "{",
            ("  VAPI_DBG(\"Swapping `%s'@%%p to host byte order\", msg);" %
                self.get_c_name()),
            "  %s(&msg->header);" % self.header.get_swap_to_host_func_name()
            if self.header is not None else "",
            "  %s(&msg->payload);" % self.get_swap_payload_to_host_func_name()
            if self.has_payload() else "",
            "}",
        ])

    #定义操作函数名称
    def get_op_func_name(self):
        return "vapi_%s" % self.name

    #生成操作函数声明
    def get_op_func_decl(self):
        if self.reply.has_payload():
            #有payload情况
            return "vapi_error_e %s(%s)" % (
                self.get_op_func_name(),
                ",\n  ".join([
                    'struct vapi_ctx_s *ctx',
                    '%s *msg' % self.get_c_name(),
                    #生成回调函数
                    'vapi_error_e (*callback)(struct vapi_ctx_s *ctx',
                    '                         void *callback_ctx',
                    '                         vapi_error_e rv',
                    '                         bool is_last',
                    '                         %s *reply)' %
                    self.reply.get_payload_struct_name(),
                    #用户参数
                    'void *callback_ctx',
                ])
            )
        else:
            #无payload情况
            return "vapi_error_e %s(%s)" % (
                self.get_op_func_name(),
                ",\n  ".join([
                    'struct vapi_ctx_s *ctx',
                    '%s *msg' % self.get_c_name(),
                    'vapi_error_e (*callback)(struct vapi_ctx_s *ctx',
                    '                         void *callback_ctx',
                    '                         vapi_error_e rv',
                    '                         bool is_last)',
                    'void *callback_ctx',
                ])
            )

    def get_op_func_def(self):
        return "\n".join([
            "%s" % self.get_op_func_decl(),
            "{",
            #如果消息未给出，则参数有误
            "  if (!msg || !callback) {",
            "    return VAPI_EINVAL;",
            "  }",
            #非阻塞，则当前request已满，则提供客户端重试
            "  if (vapi_is_nonblocking(ctx) && vapi_requests_full(ctx)) {",
            "    return VAPI_EAGAIN;",
            "  }",
            #添加锁，准备进行请求添加
            "  vapi_error_e rv;",
            "  if (VAPI_OK != (rv = vapi_producer_lock (ctx))) {",
            "    return rv;",
            "  }",
            #生成seq
            "  u32 req_context = vapi_gen_req_context(ctx);",
            "  msg->header.context = req_context;",
            #将消息转换为网络序
            "  %s(msg);" % self.get_swap_to_be_func_name(),
            #区分消息是stream可非stream情况，调用不同的vapi_send函数，如果为
            #stream方式响应，则由vapi_send2函数来发送报文
            ("  if (VAPI_OK == (rv = vapi_send_with_control_ping "
                "(ctx, msg, req_context))) {"
                if self.reply_is_stream else
                #调用vapi_send将msg放入发送队列
                "  if (VAPI_OK == (rv = vapi_send (ctx, msg))) {"
             ),
            #完成request构造
            ("    vapi_store_request(ctx, req_context, %s, "
             "(vapi_cb_t)callback, callback_ctx);" %
             ("true" if self.reply_is_stream else "false")),
            #执行request解锁
            "    if (VAPI_OK != vapi_producer_unlock (ctx)) {",
            "      abort (); /* this really shouldn't happen */",
            "    }",
            #如果非阻塞情况，则请求发送成功，返回成功
            "    if (vapi_is_nonblocking(ctx)) {",
            "      rv = VAPI_OK;",
            "    } else {",
            #针对阻塞情况，执行调度，完成api响应处理
            "      rv = vapi_dispatch(ctx);",
            "    }",
            "  } else {",
            #发送不成功，还原为host序，并abort
            "    %s(msg);" % self.get_swap_to_host_func_name(),
            "    if (VAPI_OK != vapi_producer_unlock (ctx)) {",
            "      abort (); /* this really shouldn't happen */",
            "    }",
            "  }",
            "  return rv;",
            "}",
            "",
        ])

    #生成event callback函数的申明
    def get_event_cb_func_decl(self):
        if not self.is_reply and not self.is_event:
            raise Exception(
                "Cannot register event callback for non-reply message")
        if self.has_payload():
            #有payload情况下，设置回调
            return "\n".join([
                "void vapi_set_%s_event_cb (" %
                self.get_c_name(),
                "  struct vapi_ctx_s *ctx, ",
                ("  vapi_error_e (*callback)(struct vapi_ctx_s *ctx, "
                 "void *callback_ctx, %s *payload)," %
                 self.get_payload_struct_name()),
                "  void *callback_ctx)",
            ])
        else:
            #无payload情况下设置回调
            return "\n".join([
                "void vapi_set_%s_event_cb (" %
                self.get_c_name(),
                "  struct vapi_ctx_s *ctx, ",
                "  vapi_error_e (*callback)(struct vapi_ctx_s *ctx, "
                "void *callback_ctx),",
                "  void *callback_ctx)",
            ])

    def get_event_cb_func_def(self):
        if not self.is_reply and not self.is_event:
            #不支持reply,event类型message的生成
            raise Exception(
                "Cannot register event callback for non-reply function")
        return "\n".join([
            "%s" % self.get_event_cb_func_decl(),
            "{",
            #直接调用vapi_set_event_cb完成回调设置
            ("  vapi_set_event_cb(ctx, %s, (vapi_event_cb)callback, "
             "callback_ctx);" %
             self.get_msg_id_name()),
            "}"])

    #消息结构体原数据生成
    def get_c_metadata_struct_name(self):
        return "__vapi_metadata_%s" % self.name

    def get_c_constructor(self):
        has_context = False
        if self.header is not None:
            has_context = self.header.has_field('context')
        return '\n'.join([
            #定义api的初始化函数
            'static void __attribute__((constructor)) __vapi_constructor_%s()'
            % self.name,
            '{',
            '  static const char name[] = "%s";' % self.name,
            '  static const char name_with_crc[] = "%s_%s";'
            % (self.name, self.crc[2:]),
            #准备message描述信息
            '  static vapi_message_desc_t %s = {' %
            self.get_c_metadata_struct_name(),
            '    name,',
            '    sizeof(name) - 1,',
            '    name_with_crc,',
            '    sizeof(name_with_crc) - 1,',
            '    true,' if has_context else '    false,',
            #由于存在多种header类型，故需要知道如何偏移到context
            '    offsetof(%s, context),' % self.header.get_c_name()
            if has_context else '    0,',
            #取到payload的偏移量
            ('    offsetof(%s, payload),' % self.get_c_name())
            if self.has_payload() else '    VAPI_INVALID_MSG_ID,',
            #结构体大小
            '    sizeof(%s),' % self.get_c_name(),
            #主机序，网络序转换
            '    (generic_swap_fn_t)%s,' % self.get_swap_to_be_func_name(),
            '    (generic_swap_fn_t)%s,' % self.get_swap_to_host_func_name(),
            #将消息id设置为无效，等注册后再分配
            '    VAPI_INVALID_MSG_ID,',
            '  };',
            '',
            #实现消息体注册（返回msg_id)
            '  %s = vapi_register_msg(&%s);' %
            (self.get_msg_id_name(), self.get_c_metadata_struct_name()),
            '  VAPI_DBG("Assigned msg id %%d to %s", %s);' %
            (self.name, self.get_msg_id_name()),
            '}',
        ])


vapi_send_with_control_ping = """
static inline vapi_error_e
vapi_send_with_control_ping (vapi_ctx_t ctx, void *msg, u32 context)
{
  vapi_msg_control_ping *ping = vapi_alloc_control_ping (ctx);
  if (!ping)
    {
      return VAPI_ENOMEM;
    }
  ping->header.context = context;
  vapi_msg_control_ping_hton (ping);
  return vapi_send2 (ctx, msg, ping);
}
"""


def emit_definition(parser, json_file, emitted, o):
    if o in emitted:
        #跳过已生成定义的o
        return
    
    if o.name in ("msg_header1_t", "msg_header2_t"):
        #不对msg_header1_t,msg_header2_t生成定义（外部提供）
        return
    
    if hasattr(o, "depends"):
        #如果o依赖于其它类型，则先生成依赖的定义
        for x in o.depends:
            emit_definition(parser, json_file, emitted, x)
            
    if hasattr(o, "reply"):
        #先定义reply类型
        emit_definition(parser, json_file, emitted, o.reply)
        
    #防止结构体及其相关函数重复定义，采用ifndef,define来保护生成（这个实现解决了C语言结构体间
    #相互依赖问题，这一点上比我在sangfor时实现的要好）
    if hasattr(o, "get_c_def"):
        if (o not in parser.enums_by_json[json_file] and
                o not in parser.types_by_json[json_file] and
                o not in parser.unions_by_json[json_file] and
                o.name not in parser.messages_by_json[json_file] and
                o not in parser.aliases_by_json[json_file]):
            return
        guard = "defined_%s" % o.get_c_name()
        print("#ifndef %s" % guard)
        print("#define %s" % guard)
        print("%s" % o.get_c_def())
        print("")
        function_attrs = "static inline "
        if o.name in parser.messages_by_json[json_file]:
            #生成payload结构体转大端，转主机序孙女烽
            if o.has_payload():
                print("%s%s" % (function_attrs,
                                o.get_swap_payload_to_be_func_def()))
                print("")
                print("%s%s" % (function_attrs,
                                o.get_swap_payload_to_host_func_def()))
                print("")
            
            #消息转网络序
            print("%s%s" % (function_attrs, o.get_swap_to_be_func_def()))
            print("")
            #消息转主机序
            print("%s%s" % (function_attrs, o.get_swap_to_host_func_def()))
            print("")
            #消息体占用空间大小
            print("%s%s" % (function_attrs, o.get_calc_msg_size_func_def()))
            
            #如果o是响应消息不是event,则生成alloc_function,get_op_function
            if not o.is_reply and not o.is_event:
                print("")
                #生成message空间申请函数
                print("%s%s" % (function_attrs, o.get_alloc_func_def()))
                print("")
                #生成op相关的api,用于发送请求，执行reply处理
                print("%s%s" % (function_attrs, o.get_op_func_def()))
            print("")
            
            #生成metadata注册函数,完成message注册
            print("%s" % o.get_c_constructor())
            
            #如果属于响应消息或者收到event消息，则为其生成事件回调设置函数（无context相关类的回调）
            if o.is_reply or o.is_event:
                print("")
                print("%s%s;" % (function_attrs, o.get_event_cb_func_def()))
        elif hasattr(o, "get_swap_to_be_func_def"):
            #结构体主机序转网络序
            print("%s%s" % (function_attrs, o.get_swap_to_be_func_def()))
            print("")
            #结构体网络序转主机序
            print("%s%s" % (function_attrs, o.get_swap_to_host_func_def()))
        print("#endif")
        print("")
    #记录已针对o生成了定义
    emitted.append(o)


def gen_json_unified_header(parser, logger, j, io, name):
    d, f = os.path.split(j)
    logger.info("Generating header `%s'" % name)
    #准备输出到文件io
    orig_stdout = sys.stdout
    sys.stdout = io
    include_guard = "__included_%s" % (
        j.replace(".", "_").replace("/", "_").replace("-", "_"))
    #生成守护宏
    print("#ifndef %s" % include_guard)
    print("#define %s" % include_guard)
    print("")
    print("#include <stdlib.h>")
    print("#include <stddef.h>")
    print("#include <arpa/inet.h>")
    print("#include <vapi/vapi_internal.h>")
    print("#include <vapi/vapi.h>")
    print("#include <vapi/vapi_dbg.h>")
    print("")
    print("#ifdef __cplusplus")
    print("extern \"C\" {")
    print("#endif")
    if name == "vpe.api.vapi.h":
        print("")
        print("static inline vapi_error_e vapi_send_with_control_ping "
              "(vapi_ctx_t ctx, void * msg, u32 context);")
    else:
        print("#include <vapi/vpe.api.vapi.h>")
    print("")
    
    #遍历json文件j中所有的message，生成msg_id
    for m in parser.messages_by_json[j].values():
        print("extern vapi_msg_id_t %s;" % m.get_msg_id_name())
    print("")
    #生成define_msg_id的宏
    print("#define DEFINE_VAPI_MSG_IDS_%s\\" %
          f.replace(".", "_").replace("/", "_").replace("-", "_").upper())
    print("\\\n".join([
        "  vapi_msg_id_t %s;" % m.get_msg_id_name()
        for m in parser.messages_by_json[j].values()
    ]))
    print("")
    print("")
    emitted = []
    for e in parser.enums_by_json[j]:
        emit_definition(parser, j, emitted, e)
    for a in parser.aliases_by_json[j]:
        emit_definition(parser, j, emitted, a)
    for u in parser.unions_by_json[j]:
        emit_definition(parser, j, emitted, u)
    for t in parser.types_by_json[j]:
        emit_definition(parser, j, emitted, t)
    for m in parser.messages_by_json[j].values():
        emit_definition(parser, j, emitted, m)

    print("")

    if name == "vpe.api.vapi.h":
        print("%s" % vapi_send_with_control_ping)
        print("")

    print("#ifdef __cplusplus")
    print("}")
    print("#endif")
    print("")
    print("#endif")
    sys.stdout = orig_stdout


#由json文件映射%s.vapi.h的头文件
def json_to_c_header_name(json_name):
    if json_name.endswith(".json"):
        return "%s.vapi.h" % os.path.splitext(json_name)[0]
    raise Exception("Unexpected json name `%s'!" % json_name)


def gen_c_unified_headers(parser, logger, prefix, remove_path):
    if prefix == "" or prefix is None:
        prefix = ""
    else:
        prefix = "%s/" % prefix
    
    #遍历解析的所有json文件
    for j in parser.json_files:
        if remove_path:
            d, f = os.path.split(j)
        else:
            f = j
        
        #产生要生成的头文件
        with open('%s%s' % (prefix, json_to_c_header_name(f)), "w") as io:
            gen_json_unified_header(
                parser, logger, j, io, json_to_c_header_name(f))


if __name__ == '__main__':
    try:
        verbose = int(os.getenv("V", 0))
    except:
        verbose = 0

    #设置log_level
    if verbose >= 2:
        log_level = 10
    elif verbose == 1:
        log_level = 20
    else:
        log_level = 40

    logging.basicConfig(stream=sys.stdout, level=log_level)
    logger = logging.getLogger("VAPI C GEN")
    logger.setLevel(log_level)

    #定义命令行参数
    argparser = argparse.ArgumentParser(description="VPP C API generator")
    argparser.add_argument('files', metavar='api-file', action='append',
                           type=str, help='json api file'
                           '(may be specified multiple times)')
    argparser.add_argument('--prefix', action='store', default=None,
                           help='path prefix')
    argparser.add_argument('--remove-path', action='store_true',
                           help='remove path from filename')
    args = argparser.parse_args()

    #定义jsonParser，设备基础数据类型的辅助类，设置枚举，union,struct等的映射，完成json的信息收集
    jsonparser = JsonParser(logger, args.files,
                            simple_type_class=CSimpleType,
                            enum_class=CEnum,
                            union_class=CUnion,
                            struct_type_class=CStructType,
                            field_class=CField,
                            message_class=CMessage,
                            alias_class=CAlias)

    # not using the model of having separate generated header and code files
    # with generated symbols present in shared library (per discussion with
    # Damjan), to avoid symbol version issues in .so
    # gen_c_headers_and_code(jsonparser, logger, args.prefix)

    gen_c_unified_headers(jsonparser, logger, args.prefix, args.remove_path)

    for e in jsonparser.exceptions:
        logger.warning(e)
