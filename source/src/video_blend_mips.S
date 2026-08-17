.set noreorder
.set noat

.align 2
.globl expand_blend_mips

# a0 = screen_src_ptr (u32*)
# a1 = screen_dest_ptr (u16*)
# a2 = start
# a3 = end
expand_blend_mips:
  subu   $t0, $a3, $a2
  blez   $t0, blend_return
  nop

  sll    $t1, $a2, 2
  addu   $a0, $a0, $t1
  sll    $t1, $a2, 1
  addu   $a1, $a1, $t1

  lui    $t2, %hi(io_registers)
  addiu  $t2, $t2, %lo(io_registers)
  lhu    $t3, 0x52($t2)          # REG_BLDALPHA

  andi   $t4, $t3, 0x1F          # blend_a
  srl    $t5, $t3, 8
  andi   $t5, $t5, 0x1F          # blend_b

  slti   $t6, $t4, 17
  bne    $t6, $zero, 1f
  nop
  li     $t4, 16
1:
  slti   $t6, $t5, 17
  bne    $t6, $zero, 2f
  nop
  li     $t5, 16
2:
  addu   $t6, $t4, $t5
  slti   $t7, $t6, 17            # 1 if no saturation path

  lui    $t8, %hi(palette_ram)
  addiu  $t8, $t8, %lo(palette_ram)
  li     $t9, 0x04000200         # combine mask
  li     $v0, 0x03E07C1F         # clamp mask (matches C path)
  li     $v1, 0x04008020         # saturation bits mask (matches C path)

blend_loop:
  lw     $t1, 0($a0)
  addiu  $a0, $a0, 4

  and    $t2, $t1, $t9
  bne    $t2, $t9, blend_no_blend
  nop

  andi   $t2, $t1, 0x1FF
  sll    $t2, $t2, 1
  addu   $t2, $t2, $t8
  lhu    $t2, 0($t2)             # pixel_top

  srl    $t3, $t1, 16
  andi   $t3, $t3, 0x1FF
  sll    $t3, $t3, 1
  addu   $t3, $t3, $t8
  lhu    $t3, 0($t3)             # pixel_bottom

  ins    $t2, $t2, 16, 16         # top dilated (0G0R0B)
  and    $t2, $t2, $v0

  ins    $t3, $t3, 16, 16         # bottom dilated
  and    $t3, $t3, $v0

  multu  $t3, $t5                 # LO = bottom * blend_b
  maddu  $t2, $t4                 # LO += top * blend_a
  mflo   $t2

  # Always >> 4 (matches C blend_pixel / blend_saturate_pixel).
  srl    $t2, $t2, 4

  # Saturation only when blend_a + blend_b > 16 (matches expand_blend_c).
  bne    $t7, $zero, blend_pack
  nop

  and    $t3, $t2, $v1
  beq    $t3, $zero, blend_pack
  nop
  # Same order as C blend_saturate_pixel: green, red, blue
  lui    $t6, 0x0400             # 0x04000000
  and    $t6, $t3, $t6
  beq    $t6, $zero, 3f
  nop
  lui    $t6, 0x03E0             # 0x03E00000
  or     $t2, $t2, $t6
3:
  li     $t6, 0x00008000
  and    $t6, $t3, $t6
  beq    $t6, $zero, 4f
  nop
  li     $t6, 0x00007C00
  or     $t2, $t2, $t6
4:
  andi   $t6, $t3, 0x0020
  beq    $t6, $zero, blend_pack
  nop
  ori    $t2, $t2, 0x001F

blend_pack:
  and    $t2, $t2, $v0
  srl    $t3, $t2, 16
  or     $t2, $t2, $t3
  sh     $t2, 0($a1)
  addiu  $a1, $a1, 2
  b      blend_next
  nop

blend_no_blend:
  andi   $t2, $t1, 0x1FF
  sll    $t2, $t2, 1
  addu   $t2, $t2, $t8
  lhu    $t2, 0($t2)
  sh     $t2, 0($a1)
  addiu  $a1, $a1, 2

blend_next:
  addiu  $t0, $t0, -1
  bgtz   $t0, blend_loop
  nop

blend_return:
  jr     $ra
  nop
