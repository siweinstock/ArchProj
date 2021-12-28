	lw $r2, $zero, $zero, 0       # load the content of address 0 to R[2]
	add $r2, $r2, $imm, 1         # add 1 to what we loaded
	add $r3, $r3, $imm, 1         # increase R[3] by 1
	bne $zero, $r3, $imm, 128     # if R[3] != 128 go back to line 0
	sw $r2, $zero, $zero, 0       # store the increased value back in address 0 (delay slot)
	halt $zero, $zero, $zero, 0	  # halt because we did 128 iterations and flushed the content
	halt $zero, $zero, $zero, 0	
	halt $zero, $zero, $zero, 0 
	halt $zero, $zero, $zero, 0 
	halt $zero, $zero, $zero, 0 