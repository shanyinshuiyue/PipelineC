#include "uintN_t.h"
#include "intN_t.h"

// Support old single core using mem_map_out_t
#include "mem_map.h"
#ifdef riscv_mem_map_outputs_t
#define riscv_mmio_mod_out_t riscv_mem_map_mod_out_t(riscv_mem_map_outputs_t)
#else
#define riscv_mmio_mod_out_t mem_map_out_t
#endif

// Declare a combined instruction and data memory
// also includes memory mapped IO
#include "mem_decl.h"

// CPU top level
#define riscv_out_t PPCAT(riscv_name,_out_t)
typedef struct riscv_out_t{
  // Debug IO
  uint1_t halt;
  uint32_t return_value;
  uint32_t pc;
  uint1_t unknown_op;
  uint1_t mem_out_of_range;
  #ifdef riscv_mem_map_outputs_t
  riscv_mem_map_outputs_t mem_map_outputs;
  #endif
}riscv_out_t;
riscv_out_t riscv_name(
  // Optional inputs to memory map
  #ifdef riscv_mem_map_inputs_t
  riscv_mem_map_inputs_t mem_map_inputs
  #endif
)
{
  // Program counter
  static uint32_t pc = 0;
  uint32_t pc_plus4 = pc + 4;
  printf("PC = 0x%X\n", pc);

  // Shared instruction and data memory
  //  Data memory signals are not driven until later
  //  but are used now, requiring FEEDBACK pragma
  uint32_t mem_addr;
  uint32_t mem_wr_data;
  uint1_t mem_wr_byte_ens[4];
  uint1_t mem_rd_en;
  #pragma FEEDBACK mem_addr
  #pragma FEEDBACK mem_wr_data
  #pragma FEEDBACK mem_wr_byte_ens
  #pragma FEEDBACK mem_rd_en
  riscv_mem_out_t mem_out = riscv_mem(
    pc>>2, // Instruction word read address based on PC
    mem_addr, // Main memory read/write address
    mem_wr_data, // Main memory write data
    mem_wr_byte_ens, // Main memory write data byte enables
    mem_rd_en // Main memory read enable
    // Optional memory map inputs
    #ifdef riscv_mem_map_inputs_t
    , mem_map_inputs
    #endif
  );

  // Decode the instruction to control signals
  printf("Instruction: 0x%X\n", mem_out.inst);
  decoded_t decoded = decode(mem_out.inst);

  // Register file reads and writes
  //  Register file write signals are not driven until later
  //  but are used now, requiring FEEDBACK pragma
  uint5_t reg_wr_addr;
  uint32_t reg_wr_data;
  uint1_t reg_wr_en;
  #pragma FEEDBACK reg_wr_addr
  #pragma FEEDBACK reg_wr_data
  #pragma FEEDBACK reg_wr_en  
  reg_file_out_t reg_file_out = reg_file(
    decoded.src1, // First read port address
    decoded.src2, // Second read port address
    reg_wr_addr, // Write port address
    reg_wr_data, // Write port data
    reg_wr_en // Write enable
  );
  if(decoded.print_rs1_read){
    printf("Read RegFile[%d] = %d\n", decoded.src1, reg_file_out.rd_data1);
  }
  if(decoded.print_rs2_read){
    printf("Read RegFile[%d] = %d\n", decoded.src2, reg_file_out.rd_data2);
  }

  // Execute stage
  execute_t exe = execute(
    pc, pc_plus4, 
    decoded, 
    reg_file_out.rd_data1, reg_file_out.rd_data2
  );

  // Memory stage, drive inputs (FEEDBACK)
  mem_addr = exe.result; // addr always from execute module, not always used
  mem_wr_data = reg_file_out.rd_data2;
  mem_wr_byte_ens = decoded.mem_wr_byte_ens;
  mem_rd_en = decoded.mem_rd;
  if(decoded.mem_wr_byte_ens[0]){
    printf("Write Mem[0x%X] = %d\n", mem_addr, mem_wr_data);
  }
  if(decoded.mem_rd){
    printf("Read Mem[0x%X] = %d\n", mem_addr, mem_out.rd_data);
  }  

  // Reg file write back, drive inputs (FEEDBACK)
  reg_wr_en = decoded.reg_wr;
  reg_wr_addr = decoded.dest;
  reg_wr_data = exe.result; // Default needed for FEEDBACK
  // Determine data to write back
  if(decoded.mem_to_reg){
    printf("Write RegFile: MemRd->Reg...\n");
    reg_wr_data = mem_out.rd_data;
  }else if(decoded.pc_plus4_to_reg){
    printf("Write RegFile: PC+4->Reg...\n");
    reg_wr_data = pc_plus4;
  }else{
    if(decoded.reg_wr)
      printf("Write RegFile: Execute Result->Reg...\n");
  }
  if(decoded.reg_wr){
    printf("Write RegFile[%d] = %d\n", decoded.dest, reg_wr_data);
  }

  // Branch/Increment PC
  if(decoded.exe_to_pc){
    printf("Next PC: Execute Result = 0x%X...\n", exe.result);
    pc = exe.result;
  }else{
    // Default next pc
    printf("Next PC: Default = 0x%X...\n", pc_plus4);
    pc = pc_plus4;
  }

  // Debug outputs
  riscv_out_t o;
  o.pc = pc;
  o.unknown_op = decoded.unknown_op;
  o.mem_out_of_range = mem_out.mem_out_of_range;
  // Optional outputs from memory map
  #ifdef riscv_mem_map_outputs_t
  o.mem_map_outputs = mem_out.mem_map_outputs;
  #endif
  return o;
}


#undef riscv_name
#undef RISCV_MEM_SIZE_BYTES
#undef RISCV_MEM_NUM_WORDS
#undef RISCV_MEM_INIT
#undef riscv_mem_map
#undef riscv_mmio_mod_out_t
#undef riscv_mem_map_inputs_t
#undef riscv_mem_map_outputs_t
#undef riscv_mem_ram_out_t
#undef riscv_mem_ram
#undef riscv_mem_out_t
#undef riscv_mem