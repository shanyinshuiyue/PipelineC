#pragma once
// Common verilator test includes and helpers

#ifndef __PIPELINEC__
// Software C++ includes
// Generated by Verilator
#include "verilated.h"
#include "Vtop.h" 
// Names and helper macro generated by PipelineC tool
#include "pipelinec_verilator.h"

#include <cfloat>

// Limited to an order of magnitude of min and max
bool good_float(float f)
{
  if(f>(FLT_MAX/10.0)) return false;
  if(f > 0.0 && f<(FLT_MIN*10.0)) return false;
  if(fabs(f)<(FLT_MIN*10.0)) return false;
  return true;
}

float rand_float(bool include_neg=true)
{
  float f = FLT_MAX;
  while(!good_float(f))
  {
    int i = rand();
    f = *((float*)(&i));
    if(include_neg && (rand()%2)) f *= -1;
  }
  return f;
}

void rand_two_floats(float* out1, float* out2, bool include_neg=true)
{
  float f1, f2;
  bool invalid_op_exists = true;
  while(invalid_op_exists)
  {
    f1 = rand_float(include_neg);
    f2 = rand_float(include_neg);

    // Todo list of ops to check two floats can do
    if(!good_float(f1+f2)) continue;
    if(!good_float(f1-f2)) continue;
    if(!good_float(f1*f2)) continue;
    if(!good_float(f1/f2)) continue;
    
    invalid_op_exists = false;
  }
  *out1 = f1;
  *out2 = f2;
}

int64_t rand_int_range(int64_t min, int64_t max)
{
  int64_t r64 = ((long long)rand() << 32) | rand();
  return min + r64 % (( max + 1 ) - min);
}

inline uint32_t float_31_0(float a) { union _noname { float f; uint32_t i;} conv; conv.f = a; return conv.i; }
inline float float_uint32(uint32_t a) { union _noname { float f; uint32_t i;} conv; conv.i = a; return conv.f; }

template <typename T, unsigned B>
inline T signextend(const T x)
{
  struct {T x:B;} s;
  return s.x = x;
}
#endif

#define DUT_SET_INPUT(top, input_name) \
top->input_name = input_name;

#define DUT_SET_FLOAT_INPUT(top, input_name) \
top->input_name = float_31_0(input_name);

#define DUT_GET_OUTPUT(top, output_name) \
output_name = top->output_name;

#define DUT_GET_SIGNED_OUTPUT(top, output_name, bitwidth) \
output_name = signextend<int64_t,bitwidth>(top->output_name);

#define DUT_GET_FLOAT_OUTPUT(top, output_name) \
output_name = float_uint32(top->output_name);

#define DUT_PRINT_INT(i_val)\
printf(#i_val": integer %lld, uint64 0x%016llX ", i_val, (uint64_t)i_val);

#define DUT_PRINT_FLOAT(f_val)\
printf(#f_val": float %e, uint32 0x%08X ", f_val, float_31_0(f_val));

#define DUT_RISING_EDGE(top) \
top->clk = 0;\
top->eval();\
top->clk = 1;\
top->eval();