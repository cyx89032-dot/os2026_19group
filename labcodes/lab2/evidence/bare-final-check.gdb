set pagination off
set confirm off
set debuginfod enabled off
set logging file evidence/gdb-qemu-bare-complete.txt
set logging overwrite on
set logging enabled on
handle SIGPIPE nostop noprint
break get_physical_address if addr == 0x1000
run
printf "\n=== M-mode boot translation ===\n"
p/x addr
p/x env->satp
p mmu_idx
bt 6
set $bare_result = physical
finish
p/x *$bare_result
if *$bare_result != 0x1000
  error Unexpected boot translation result
end
tbreak /opt/riscv/qemu-4.1.1/target/riscv/cpu_helper.c:472
continue
printf "\n=== Boot TLB fill arguments ===\n"
p/x address
p/x pa
p mmu_idx
tbreak /opt/riscv/qemu-4.1.1/accel/tcg/cputlb.c:1036 if addr == 0x1000
continue
printf "\n=== Boot TLB after fill ===\n"
p/x addr
p/x entry->addr_code
p/x *entry
p (entry->addr_code == (addr & ~0xfffULL))
if entry->addr_code != (addr & ~0xfffULL)
  error Boot code tag does not match
end
printf "\nBARE_TLB_CHECK_PASSED\n"
set logging enabled off
kill
quit
