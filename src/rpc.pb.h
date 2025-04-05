/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.9.1 */

#ifndef PB_RPC_PB_H_INCLUDED
#define PB_RPC_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Struct definitions */
typedef struct _RPCRequest {
    pb_callback_t method;
    pb_callback_t func_name;
    bool has_num1;
    int32_t num1;
    bool has_num2;
    int32_t num2;
} RPCRequest;

typedef struct _RPCResponse {
    pb_size_t which_result;
    union {
        int32_t value;
        pb_callback_t error;
    } result;
} RPCResponse;


#ifdef __cplusplus
extern "C" {
#endif

/* Initializer values for message structs */
#define RPCRequest_init_default                  {{{NULL}, NULL}, {{NULL}, NULL}, false, 0, false, 0}
#define RPCResponse_init_default                 {0, {0}}
#define RPCRequest_init_zero                     {{{NULL}, NULL}, {{NULL}, NULL}, false, 0, false, 0}
#define RPCResponse_init_zero                    {0, {0}}

/* Field tags (for use in manual encoding/decoding) */
#define RPCRequest_method_tag                    1
#define RPCRequest_func_name_tag                 2
#define RPCRequest_num1_tag                      3
#define RPCRequest_num2_tag                      4
#define RPCResponse_value_tag                    1
#define RPCResponse_error_tag                    2

/* Struct field encoding specification for nanopb */
#define RPCRequest_FIELDLIST(X, a) \
X(a, CALLBACK, REQUIRED, STRING,   method,            1) \
X(a, CALLBACK, OPTIONAL, STRING,   func_name,         2) \
X(a, STATIC,   OPTIONAL, INT32,    num1,              3) \
X(a, STATIC,   OPTIONAL, INT32,    num2,              4)
#define RPCRequest_CALLBACK pb_default_field_callback
#define RPCRequest_DEFAULT NULL

#define RPCResponse_FIELDLIST(X, a) \
X(a, STATIC,   ONEOF,    INT32,    (result,value,result.value),   1) \
X(a, CALLBACK, ONEOF,    STRING,   (result,error,result.error),   2)
#define RPCResponse_CALLBACK pb_default_field_callback
#define RPCResponse_DEFAULT NULL

extern const pb_msgdesc_t RPCRequest_msg;
extern const pb_msgdesc_t RPCResponse_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define RPCRequest_fields &RPCRequest_msg
#define RPCResponse_fields &RPCResponse_msg

/* Maximum encoded size of messages (where known) */
/* RPCRequest_size depends on runtime parameters */
/* RPCResponse_size depends on runtime parameters */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
