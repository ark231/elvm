#include <ir/ir.h>
#include <stdlib.h>
#include <target/util.h>

#define WORD_LENGTH 24
#define MEM_SIZE (1 << (WORD_LENGTH))

static void lan22_extended_init_state(Data *data) {
  int data_size = 0, data_size_in_smaller_half, data_size_in_larger_half;
  int i;
  Data *data_count = data;
  int *datas;
  for (; data_count; data_count = data_count->next) {
    data_size++;
  }
  if (data_size / (MEM_SIZE / 2) == 0) {
    data_size_in_smaller_half = data_size;
    data_size_in_larger_half = 0;
  } else {
    data_size_in_smaller_half = MEM_SIZE / 2;
    data_size_in_larger_half = data_size - (MEM_SIZE / 2);
  }
  emit_line("#stack 0,intarray,24,big,0,0,0,0,0,0,%d", MEM_SIZE - 1);
  /* smaller half */
  printf("#stack 1,intarray,24,little");
  for (i = 0; i < data_size_in_smaller_half; i++, data = data->next) {
    printf(",%d", data->v);
  }
  printf(",0:%d\n", (MEM_SIZE / 2) - data_size_in_smaller_half);
  /* larger half */
  printf("#stack 2,intarray,24,big");
  if (data_size_in_larger_half > 0) {
    datas = (int *)malloc(sizeof(int) * data_size_in_larger_half);
    for (i = 0; i < data_size_in_larger_half; i++, data = data->next) {
      datas[data_size_in_larger_half - 1 - i] = data->v;
    }
    for (i = 0; i < data_size_in_larger_half; i++) {
      printf(",%d", datas[i]);
    }
  }
  printf(",0:%d\n", (MEM_SIZE / 2) - data_size_in_larger_half);
}

static void lan22_extended_emit_func_prologue(const char *func_name) {
  emit_line(";\\autofunc", func_name);
  emit_line(";\\@%s none -> none", func_name);
  emit_line("startfunc");
  inc_indent();
  inc_indent();
}

static void lan22_extended_emit_func_epilogue(const char *func_name) {
  emit_line("@{%s}", func_name);
  dec_indent();
  dec_indent();
  emit_line("endfunc");
}

static char *lan22_extended_pc_labelname(int pc) { return format("pc%d", pc); }

static void lan22_extended_emit_pc_change(int pc) {
  dec_indent();
  emit_line(";label %s", lan22_extended_pc_labelname(pc));
  emit_line("${%d}", pc);
  emit_line("deflabel");
  inc_indent();
}
static void lan22_extended_emit_value(Value *value) {
  if (value->type == REG) {
    emit_line("\\pushl8 %d", value->reg);
    emit_line("\\call elvm_read_register");
  } else if (value->type == IMM) {
    emit_line("#elvm_pushl24 %d", value->imm);
  } else {
    error("invalid value");
  }
}
static void lan22_extended_emit_src(Inst *inst) {
  lan22_extended_emit_value(&inst->src);
}
static void lan22_extended_emit_read_dst_and_src(Inst *inst) {
  lan22_extended_emit_src(inst);
  emit_line("\\call elvm_ext_24_to_64");
  emit_line("\\pushl8 %d", inst->dst.reg);
  emit_line("\\call elvm_ext_24_to_64");
}
static void lan22_extended_emit_write_dst(Inst *inst) {
  emit_line("\\pushl8 %d", inst->dst.reg);
  emit_line("\\call elvm_write_register");
}

static void lan22_extended_emit_comparison(Inst *inst) {
  int op = normalize_cond(inst->op, 0);
  switch (op) {
  case JEQ:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call to_bool");
    emit_line("\\call bool_not");
    break;
  case JNE:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call to_bool");
    break;
  case JLT:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call isn");
    break;
  case JGT:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call isp");
    break;
  case JLE:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call isp");
    emit_line("\\call bool_not");
    break;
  case JGE:
    lan22_extended_emit_read_dst_and_src(inst);
    emit_line("\\call sub");
    emit_line("\\call isn");
    emit_line("\\call bool_not");
    break;
  case JMP:
    emit_line("\\pushl8 1");
    break;
  default:
    error("oops");
    break;
  }
}

static void lan22_extended_emit_inst(Inst *inst) {
  switch (inst->op) {
  case MOV:
    lan22_extended_emit_src(inst);
    lan22_extended_emit_write_dst(inst);
    break;

  case ADD:
    lan22_extended_emit_src(inst);
    emit_line("\\call elvm_ext_24_to_64");
    emit_line("\\pushl8 %d", inst->dst.reg);
    emit_line("\\call elvm_read_register");
    emit_line("\\call elvm_ext_24_to_64");
    emit_line("\\call add");
    emit_line("\\call elvm_trim_64_to_24");
    lan22_extended_emit_write_dst(inst);
    break;

  case SUB:
    lan22_extended_emit_src(inst);
    emit_line("\\call elvm_ext_24_to_64");
    emit_line("\\pushl8 %d", inst->dst.reg);
    emit_line("\\call elvm_read_register");
    emit_line("\\call elvm_ext_24_to_64");
    emit_line("\\call sub");
    emit_line("\\call elvm_trim_64_to_24");
    lan22_extended_emit_write_dst(inst);
    break;

  case LOAD:
    lan22_extended_emit_src(inst);
    emit_line("\\call elvm_access_memory");
    emit_line("\\call elvm_pop24s2");
    emit_line("\\call elvm_push24r2tos0");
    emit_line("\\call elvm_dup24s0");
    emit_line("\\call elvm_pop24s0");
    emit_line("#xchg 0,2");
    emit_line("\\call elvm_push24s2");
    lan22_extended_emit_write_dst(inst);
    break;

  case STORE:
    lan22_extended_emit_src(inst);
    emit_line("\\call elvm_access_memory");
    emit_line("\\call elvm_pop24s2");
    emit_line("\\pushl8 %d", inst->dst.reg);
    emit_line("\\call elvm_read_register");
    emit_line("\\call elvm_pop24s0");
    emit_line("#xchg 0,2");
    emit_line("\\call elvm_push24s2");
    break;

  case PUTC:
    lan22_extended_emit_src(inst);
    emit_line("pop8s0");
    emit_line("pop8s0");
    emit_line("pop8s0");
    emit_line("print");
    break;

  case GETC:
    emit_line("\\call elvm_read_stdin");
    lan22_extended_emit_write_dst(inst);
    break;

  case EXIT:
    emit_line("exit");
    break;

  case DUMP:
    break;

  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    lan22_extended_emit_comparison(inst);
    emit_line("\\call elvm_ext_8_to_24");
    lan22_extended_emit_write_dst(inst);
    break;

  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
  case JMP:
    lan22_extended_emit_comparison(inst);
    emit_line("\\call bool_not");
    lan22_extended_emit_value(&inst->jmp);
    emit_line("\\call elvm_pop24s0");
    emit_line("#xchg 0,1");
    emit_line("#clr0");
    emit_line("pop8s0");
    emit_line("#xchg 0,1");
    emit_line("ifz");
    break;

  default:
    error("oops");
  }
}

void target_22lan_extended(Module *module) {
  int prev_pc = -1;
  Inst *inst = module->text;
  lan22_extended_init_state(module->data);

  lan22_extended_emit_func_prologue("main");
  for (; inst; inst = inst->next) {
    if (inst->pc != prev_pc) {
      lan22_extended_emit_pc_change(inst->pc);
    }
    lan22_extended_emit_inst(inst);
    prev_pc = inst->pc;
  }
  lan22_extended_emit_func_epilogue("main");
}
