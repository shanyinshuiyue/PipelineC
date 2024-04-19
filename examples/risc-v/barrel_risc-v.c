// Copy of single cycle CPU rewritten ~netlist/multi MAIN graph style
// Each node in the graph instantiates N copies of barrel components
// For now has no flow control

// Need more ports since feedback to mems?
#pragma PART "xc7a100tcsg324-1"

// RISC-V components
#include "risc-v.h"

// Include test gcc compiled program
#include "gcc_test/mem_init.h" // MEM_INIT,MEM_INIT_SIZE

// Declare memory map information
// Starts with shared with software memory map info
#include "gcc_test/mem_map.h" 
// Define inputs and outputs
typedef struct my_mmio_in_t{
  uint8_t core_id;
}my_mmio_in_t;
typedef struct my_mmio_out_t{
  uint32_t return_value;
  uint1_t halt;
  uint1_t led;
}my_mmio_out_t;
// Define the hardware memory for those IO
RISCV_DECL_MEM_MAP_MOD_OUT_T(my_mmio_out_t)
riscv_mem_map_mod_out_t(my_mmio_out_t) my_mem_map_module(
  RISCV_MEM_MAP_MOD_INPUTS(my_mmio_in_t)
){
  // Outputs
  static riscv_mem_map_mod_out_t(my_mmio_out_t) o;
  o.addr_is_mapped = 0; // since o is static regs
  // Memory muxing/select logic
  // Uses helper comparing word address and driving a variable
  o.outputs.return_value = inputs.core_id; // Override typical reg read behavior
  WORD_MM_ENTRY(o, CORE_ID_RETURN_OUTPUT_ADDR, o.outputs.return_value)
  o.outputs.halt = wr_byte_ens[0] & (addr==CORE_ID_RETURN_OUTPUT_ADDR);
  WORD_MM_ENTRY(o, LED_ADDR, o.outputs.led)
  return o;
}

// Declare a combined instruction and data memory
// also includes memory mapped IO
#define riscv_name              my_riscv
#define RISCV_MEM_INIT          MEM_INIT // from gcc_test
#define RISCV_MEM_SIZE_BYTES    MEM_INIT_SIZE // from gcc_test
#define riscv_mem_map           my_mem_map_module
#define riscv_mem_map_inputs_t  my_mmio_in_t
#define riscv_mem_map_outputs_t my_mmio_out_t
// Support old single core using mem_map_out_t
#include "mem_map.h"
#ifdef riscv_mem_map_outputs_t
#define riscv_mmio_mod_out_t riscv_mem_map_mod_out_t(riscv_mem_map_outputs_t)
#else
#define riscv_mmio_mod_out_t mem_map_out_t
#endif
#include "mem_decl.h" // declare my_riscv_mem_out my_riscv_mem() func

// Config threads
// Mhz               | ~40  ~55-60 ~100  ~158  ~164  (comb->  150  160  ...~fmax 400Mhz?
// Threads(~#stages) | 1    3      4     5     6              9    16      64?
// max ops/sec       |      ~1.6G  
#define CPU_CLK_MHZ 150.0
#define N_THREADS_PER_BARREL 6
#define N_BARRELS 20
//TODO determine next best pipeline stage to enable
//what does that fmax+resources scale ot ops per sec?
// Interconnect wires between stages
typedef struct thread_context_t{
  uint8_t thread_id;
  uint1_t thread_valid;
  uint32_t pc;
  uint32_t next_pc;
  uint32_t instruction;
  decoded_t decoded;
  reg_file_out_t reg_file_rd_datas;
  execute_t exe;
  uint32_t mem_rd_data;
}thread_context_t;
thread_context_t pc_inputs[N_BARRELS];
thread_context_t pc_outputs[N_BARRELS];
thread_context_t imem_inputs[N_BARRELS];
thread_context_t imem_outputs[N_BARRELS];
thread_context_t decode_inputs[N_BARRELS];
thread_context_t decode_outputs[N_BARRELS];
thread_context_t reg_rd_inputs[N_BARRELS];
thread_context_t reg_rd_outputs[N_BARRELS];
thread_context_t exe_inputs[N_BARRELS];
thread_context_t exe_outputs[N_BARRELS];
thread_context_t dmem_inputs[N_BARRELS];
thread_context_t dmem_outputs[N_BARRELS];
thread_context_t reg_wr_inputs[N_BARRELS];
thread_context_t reg_wr_outputs[N_BARRELS];
thread_context_t branch_inputs[N_BARRELS];
thread_context_t branch_outputs[N_BARRELS];
#pragma MAIN inter_stage_connections
#pragma FUNC_WIRES inter_stage_connections
void inter_stage_connections(){
  // Each inter stage connection is configurable
  // PC stage is 1 cycle delay +
  // All 8 interconnect delay regs
  // = baseline 9 threads from handpipelining alone
  #ifdef pc_to_imem_REG
  static thread_context_t pc_to_imem[N_BARRELS];
  imem_inputs = pc_to_imem;
  pc_to_imem = pc_outputs;
  #else
  imem_inputs = pc_outputs;
  #endif
  #if N_THREADS_PER_BARREL >= 6
  #define imem_to_decode_REG
  #endif
  #ifdef imem_to_decode_REG
  static thread_context_t imem_to_decode[N_BARRELS];
  decode_inputs = imem_to_decode;
  imem_to_decode = imem_outputs;
  #else
  decode_inputs = imem_outputs;
  #endif
  #ifdef decode_to_reg_rd_REG
  static thread_context_t decode_to_reg_rd[N_BARRELS];
  reg_rd_inputs = decode_to_reg_rd;
  decode_to_reg_rd = decode_outputs;
  #else
  reg_rd_inputs = decode_outputs;
  #endif
  #if N_THREADS_PER_BARREL >= 4
  #define reg_rd_to_exe_REG
  #endif
  #ifdef reg_rd_to_exe_REG
  static thread_context_t reg_rd_to_exe[N_BARRELS];
  exe_inputs = reg_rd_to_exe;
  reg_rd_to_exe = reg_rd_outputs;
  #else
  exe_inputs = reg_rd_outputs;
  #endif
  #if N_THREADS_PER_BARREL >= 5
  #define exe_to_dmem_REG
  #endif
  #ifdef exe_to_dmem_REG
  static thread_context_t exe_to_dmem[N_BARRELS];
  dmem_inputs = exe_to_dmem;
  exe_to_dmem = exe_outputs;
  #else
  dmem_inputs = exe_outputs;
  #endif
  #ifdef dmem_to_reg_wr_REG
  static thread_context_t dmem_to_reg_wr[N_BARRELS];
  reg_wr_inputs = dmem_to_reg_wr;
  dmem_to_reg_wr = dmem_outputs;
  #else
   reg_wr_inputs = dmem_outputs;
  #endif
  #ifdef reg_wr_to_branch_REG
  static thread_context_t reg_wr_to_branch[N_BARRELS];
  branch_inputs = reg_wr_to_branch;
  reg_wr_to_branch = reg_wr_outputs;
  #else
  branch_inputs = reg_wr_outputs;
  #endif
  #ifdef branch_to_pc_REG
  static thread_context_t branch_to_pc[N_BARRELS];
  pc_inputs = branch_to_pc;
  branch_to_pc = branch_outputs;
  #else
  pc_inputs = branch_outputs;
  #endif
}
// Per thread IO
uint1_t mem_out_of_range[N_BARRELS][N_THREADS_PER_BARREL]; // Exception, stop sim
uint1_t unknown_op[N_BARRELS][N_THREADS_PER_BARREL]; // Exception, stop sim
riscv_mem_map_inputs_t  mem_map_inputs[N_BARRELS][N_THREADS_PER_BARREL];
riscv_mem_map_outputs_t mem_map_outputs[N_BARRELS][N_THREADS_PER_BARREL];


// Temp control fsm trying to start up to max threads
uint1_t barrel_start_ready[N_BARRELS];
uint32_t barrel_start_pc[N_BARRELS];
uint8_t barrel_start_thread_id[N_BARRELS];
uint1_t barrel_start_valid[N_BARRELS];
#pragma MAIN thread_starter_fsm
void thread_starter_fsm(){
  static uint8_t running_threads[N_BARRELS];
  uint32_t i;
  for (i = 0; i < N_BARRELS; i+=1)
  {
    barrel_start_valid[i] = running_threads[i] < N_THREADS_PER_BARREL;
    barrel_start_thread_id[i] = running_threads[i];
    barrel_start_pc[i] = 0;
    if(barrel_start_valid[i] & barrel_start_ready[i]){
      printf("Barrel %d Thread %d: Started!\n", i, barrel_start_thread_id[i]);
      running_threads[i] += 1;
    }
  }
}


// Current PC reg + thread ID + valid bit
typedef struct pc_stage_out_t{
  thread_context_t outputs;
  uint1_t start_ready;
}pc_stage_out_t;
pc_stage_out_t pc_stage(
  uint8_t bid, // Barrel ID for sim printing
  thread_context_t inputs,
  uint32_t start_thread_id,
  uint32_t start_pc,
  uint1_t start_valid
){
  // These regs give this pc stage 1 cycle of delay as baseline (not comb pass through)
  static uint32_t pc_reg;
  static uint8_t thread_id_reg;
  static uint1_t thread_valid_reg;
  pc_stage_out_t o;
  o.outputs.pc = pc_reg;
  o.outputs.thread_id = thread_id_reg;
  o.outputs.thread_valid = thread_valid_reg;
  if(o.outputs.thread_valid)
  {
    printf("Barrel %d Thread %d: PC = 0x%X\n", bid, o.outputs.thread_id, o.outputs.pc);
  }
  pc_reg = 0;
  thread_id_reg = 0;
  thread_valid_reg = 0;
  o.start_ready = 0;
  if(inputs.thread_valid){
    pc_reg = inputs.next_pc;
    thread_id_reg = inputs.thread_id;
    thread_valid_reg = 1;
  }else{
    // No next thread input to stage, accept stage
    o.start_ready = 1;
    if(start_valid){
      pc_reg = start_pc;
      thread_id_reg = start_thread_id;
      thread_valid_reg = 1;
    }
  }
  return o;
}
#pragma MAIN pc_thread_start_stage
#pragma FUNC_LATENCY pc_thread_start_stage 1
void pc_thread_start_stage(){
  uint32_t i;
  for (i = 0; i < N_BARRELS; i+=1)
  {
    pc_stage_out_t pc_stage_out = pc_stage(
      i,
      pc_inputs[i],
      barrel_start_thread_id[i],
      barrel_start_pc[i],
      barrel_start_valid[i]);
    pc_outputs[i] = pc_stage_out.outputs;
    barrel_start_ready[i] = pc_stage_out.start_ready;
  }
}


// Main memory  instance
// IMEM, 1 rd port
// DMEM, 1 RW port
//  Data memory signals are not driven until later
//  but are used now, requiring FEEDBACK pragma
//  + memory mapped IO signals
typedef struct multi_thread_mem_stage_out_t
{
  thread_context_t imem_out;
  thread_context_t dmem_out;
  riscv_mem_map_outputs_t mem_map_out[N_THREADS_PER_BARREL];
  uint1_t mem_out_of_range[N_THREADS_PER_BARREL];
}multi_thread_mem_stage_out_t;
multi_thread_mem_stage_out_t multi_thread_mem_2_stages(
  uint8_t bid, // For sim print
  thread_context_t imem_in,
  thread_context_t dmem_in,
  riscv_mem_map_inputs_t mem_map_in[N_THREADS_PER_BARREL]
){
  multi_thread_mem_stage_out_t o;
  // Stage thread context, input from prev stages
  o.imem_out = imem_in;
  o.dmem_out = dmem_in;
  // Each thread has own instances of a shared instruction and data memory
  // Only current thread mem is enabled
  // Output from multiple threads is muxed
  uint32_t instructions[N_THREADS_PER_BARREL];
  uint32_t mem_rd_datas[N_THREADS_PER_BARREL];
  uint8_t tid;
  for(tid = 0; tid < N_THREADS_PER_BARREL; tid+=1)
  {
    uint32_t mem_addr;
    uint32_t mem_wr_data;
    uint1_t mem_wr_byte_ens[4];
    uint1_t mem_rd_en;
    mem_addr = dmem_in.exe.result; // addr always from execute module, not always used
    mem_wr_data = dmem_in.reg_file_rd_datas.rd_data2;
    mem_wr_byte_ens = dmem_in.decoded.mem_wr_byte_ens;
    mem_rd_en = dmem_in.decoded.mem_rd;
    // gate enables with thread id compare and valid
    if(!(dmem_in.thread_valid & (dmem_in.thread_id==tid))){
      mem_wr_byte_ens[0] = 0;
      mem_wr_byte_ens[1] = 0;
      mem_wr_byte_ens[2] = 0;
      mem_wr_byte_ens[3] = 0;
      mem_rd_en = 0;
    }
    if(mem_wr_byte_ens[0]){
      printf("Barrel %d Thread %d: Write Mem[0x%X] = %d\n", bid, tid, mem_addr, mem_wr_data);
    }
    my_riscv_mem_out_t mem_out = my_riscv_mem(
      imem_in.pc>>2, // Instruction word read address based on PC
      mem_addr, // Main memory read/write address
      mem_wr_data, // Main memory write data
      mem_wr_byte_ens, // Main memory write data byte enables
      mem_rd_en // Main memory read enable
      // Optional memory map inputs
      #ifdef riscv_mem_map_inputs_t
      , mem_map_in[tid]
      #endif
    );
    instructions[tid] = mem_out.inst;
    mem_rd_datas[tid] = mem_out.rd_data;
    if(mem_rd_en){
      printf("Barrel %d Thread %d: Read Mem[0x%X] = %d\n", bid, tid, mem_addr, mem_out.rd_data);
    }
    o.mem_out_of_range[tid] = mem_out.mem_out_of_range;
    o.mem_map_out[tid] = mem_out.mem_map_outputs;
  }
  // Mux output based on current thread
  o.imem_out.instruction = instructions[imem_in.thread_id];
  o.dmem_out.mem_rd_data = mem_rd_datas[dmem_in.thread_id];
  return o;
}
#pragma MAIN mem_2_stages
void mem_2_stages()
{
  uint32_t bid;
  for (bid = 0; bid < N_BARRELS; bid+=1)
  {
    multi_thread_mem_stage_out_t multi_thread_mem_out = multi_thread_mem_2_stages(
      bid,
      imem_inputs[bid],
      dmem_inputs[bid],
      mem_map_inputs[bid]
    );
    imem_outputs[bid] = multi_thread_mem_out.imem_out;
    dmem_outputs[bid] = multi_thread_mem_out.dmem_out;
    mem_map_outputs[bid] = multi_thread_mem_out.mem_map_out;
  }
}
#ifdef riscv_mem_map_outputs_t
#ifdef riscv_mem_map_inputs_t
// LEDs for demo
#include "leds/leds_port.c"
MAIN_MHZ(mm_io_connections, CPU_CLK_MHZ)
#pragma FUNC_WIRES mm_io_connections
void mm_io_connections()
{
  // Output LEDs for hardware debug
  leds = 0;

  // temp dumb AND together
  static uint1_t led_reg;
  leds = led_reg;
  uint1_t led = 1;
  uint32_t bid;
  for(bid = 0; bid < N_BARRELS; bid+=1)
  {
    uint8_t tid;
    for(tid = 0; tid < N_THREADS_PER_BARREL; tid+=1)
    {
      mem_map_inputs[bid][tid].core_id = tid;
      led &= mem_map_outputs[bid][tid].led;
      //leds |= (uint4_t)mem_map_outputs[tid].led << tid;
    }
  }
  led_reg = led;
}
#endif
#endif


#pragma MAIN decode_stages
void decode_stages()
{
  // Stage thread context, input from prev stage
  thread_context_t inputs[N_BARRELS] = decode_inputs;
  thread_context_t outputs[N_BARRELS] = inputs;

  uint32_t bid;
  for(bid = 0; bid < N_BARRELS; bid+=1)
  {
    if(inputs[bid].thread_valid){
      printf("Barrel %d Thread %d: Instruction: 0x%X\n", bid, inputs[bid].thread_id, inputs[bid].instruction);
      outputs[bid].decoded = decode(inputs[bid].instruction);
    }
    // Multiple unknown op outputs per thread
    uint8_t tid;
    for(tid = 0; tid < N_THREADS_PER_BARREL; tid+=1)
    {
      unknown_op[bid][tid] = 0;
      if(inputs[bid].thread_valid & (inputs[bid].thread_id==tid)){
        unknown_op[bid][tid] = outputs[bid].decoded.unknown_op;
      }
    }
  }
  // Output to next stage
  decode_outputs = outputs;
}


// Multiple copies of N thread register files
typedef struct multi_thread_reg_files_out_t{
  thread_context_t rd_outputs;
  thread_context_t wr_outputs;
}multi_thread_reg_files_out_t;
multi_thread_reg_files_out_t multi_thread_reg_files(
  uint8_t bid, // Barrel ID for sim printing
  thread_context_t rd_inputs,
  thread_context_t wr_inputs
){
  multi_thread_reg_files_out_t o;
  // Stage thread context, input from prev stages
  o.rd_outputs = rd_inputs;
  o.wr_outputs = wr_inputs;
  // Each thread has own instances of a shared instruction and data memory
  uint8_t tid;
  // Only current thread mem is enabled
  // Output from multiple threads is muxed
  reg_file_out_t reg_file_rd_datas[N_THREADS_PER_BARREL];
  for(tid = 0; tid < N_THREADS_PER_BARREL; tid+=1)
  {
    uint1_t wr_thread_sel = wr_inputs.thread_valid & (wr_inputs.thread_id==tid);
    // Register file reads and writes
    uint5_t reg_wr_addr;
    uint32_t reg_wr_data;
    uint1_t reg_wr_en;
    if(wr_thread_sel){
      // Reg file write back, drive inputs 
      reg_wr_en = wr_inputs.decoded.reg_wr;
      reg_wr_addr = wr_inputs.decoded.dest;
      reg_wr_data = wr_inputs.exe.result;
      // Determine data to write back
      if(wr_inputs.decoded.mem_to_reg){
        printf("Barrel %d Thread %d: Write RegFile: MemRd->Reg...\n", bid, tid);
        reg_wr_data = wr_inputs.mem_rd_data;
      }else if(wr_inputs.decoded.pc_plus4_to_reg){
        printf("Barrel %d Thread %d: Write RegFile: PC+4->Reg...\n", bid, tid);
        reg_wr_data = wr_inputs.pc + 4;
      }else{
        if(reg_wr_en)
          printf("Barrel %d Thread %d: Write RegFile: Execute Result->Reg...\n", bid, tid);
      }
      if(reg_wr_en){
        printf("Barrel %d Thread %d: Write RegFile[%d] = %d\n", bid, tid, wr_inputs.decoded.dest, reg_wr_data);
      }
    }
    reg_file_rd_datas[tid] = reg_file(
      rd_inputs.decoded.src1, // First read port address
      rd_inputs.decoded.src2, // Second read port address
      reg_wr_addr, // Write port address
      reg_wr_data, // Write port data
      reg_wr_en // Write enable
    );
    // Reg read
    uint1_t rd_thread_sel = rd_inputs.thread_valid & (rd_inputs.thread_id==tid);
    if(rd_thread_sel){
      if(rd_inputs.decoded.print_rs1_read){
        printf("Barrel %d Thread %d: Read RegFile[%d] = %d\n", bid, tid, rd_inputs.decoded.src1, reg_file_rd_datas[tid].rd_data1);
      }
      if(rd_inputs.decoded.print_rs2_read){
        printf("Barrel %d Thread %d: Read RegFile[%d] = %d\n", bid, tid, rd_inputs.decoded.src2, reg_file_rd_datas[tid].rd_data2);
      }
    }
  }
  // Mux output for selected thread
  o.rd_outputs.reg_file_rd_datas = reg_file_rd_datas[rd_inputs.thread_id];

  return o;
}
#pragma MAIN reg_file_2_stages
void reg_file_2_stages()
{
  uint32_t bid;
  for(bid = 0; bid < N_BARRELS; bid+=1)
  {
    multi_thread_reg_files_out_t reg_file_out = multi_thread_reg_files(
      bid, // Barrel ID for sim printing
      reg_rd_inputs[bid],
      reg_wr_inputs[bid]
    );
    reg_rd_outputs[bid] = reg_file_out.rd_outputs;
    reg_wr_outputs[bid] = reg_file_out.wr_outputs;
  }
}


#pragma MAIN execute_stages
void execute_stages()
{
  // Stage thread context, input from prev stage
  thread_context_t inputs[N_BARRELS] = exe_inputs;
  thread_context_t outputs[N_BARRELS] = inputs;
  uint32_t i;
  for(i = 0; i < N_BARRELS; i+=1)
  {
    // Execute stage
    uint32_t pc_plus4 = inputs[i].pc + 4;
    if(inputs[i].thread_valid){
      printf("Barrel %d Thread %d: Execute stage...\n", i, inputs[i].thread_id);
      outputs[i].exe = execute(
        inputs[i].pc, pc_plus4, 
        inputs[i].decoded, 
        inputs[i].reg_file_rd_datas.rd_data1, inputs[i].reg_file_rd_datas.rd_data2
      );
    }
  }
  // Output to next stage
  exe_outputs = outputs;
}


// Calculate the next PC
#pragma MAIN branch_next_pc_stages
void branch_next_pc_stages()
{
  // Stage thread context, input from prev stage
  thread_context_t inputs[N_BARRELS] = branch_inputs;
  thread_context_t outputs[N_BARRELS] = inputs;
  uint32_t i;
  for(i = 0; i < N_BARRELS; i+=1)
  {
    outputs[i].next_pc = 0;
    if(inputs[i].thread_valid){
      // Branch/Increment PC
      if(inputs[i].decoded.exe_to_pc){
        printf("Barrel %d Thread %d: Next PC: Execute Result = 0x%X...\n", i, inputs[i].thread_id, inputs[i].exe.result);
        outputs[i].next_pc = inputs[i].exe.result;
      }else{
        // Default next pc
        uint32_t pc_plus4 = inputs[i].pc + 4;
        printf("Barrel %d Thread %d: Next PC: Default = 0x%X...\n", i, inputs[i].thread_id, pc_plus4);
        outputs[i].next_pc = pc_plus4;
      }
    }
  }
  // Output to next stage
  branch_outputs = outputs;
}
