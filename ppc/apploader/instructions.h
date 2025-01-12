#pragma once

#define R0  0
#define R1  1
#define R2  2
#define R3  3

#define NEW_PPC_INSTR() 0

#define PPC_OPCODE_ADDI        14
#define PPC_OPCODE_B           18

// fields
#define PPC_OPCODE_MASK  0x3F
#define PPC_OPCODE_SHIFT 26
#define PPC_GET_OPCODE(instr)       ((instr           >> PPC_OPCODE_SHIFT) & PPC_OPCODE_MASK)
#define PPC_SET_OPCODE(instr,opcode) (instr |= (opcode & PPC_OPCODE_MASK) << PPC_OPCODE_SHIFT)

#define PPC_REG_MASK     0x1F

#define PPC_RD_MASK      PPC_REG_MASK
#define PPC_RD_SHIFT     21
#define PPC_SET_RD(instr,rd)         instr |= (rd&PPC_RD_MASK)         << PPC_RD_SHIFT

#define PPC_RA_MASK      PPC_REG_MASK
#define PPC_RA_SHIFT     16
#define PPC_SET_RA(instr,ra)         instr |= (ra&PPC_RA_MASK)         << PPC_RA_SHIFT

#define PPC_IMMED_MASK   0xFFFF
#define PPC_IMMED_SHIFT  0
#define PPC_SET_IMMED(instr,immed)   instr |= (immed&PPC_IMMED_MASK)   << PPC_IMMED_SHIFT

#define PPC_LI_MASK      0xFFFFFF
#define PPC_LI_SHIFT     2
#define PPC_GET_LI(instr)           ((instr       >> PPC_LI_SHIFT) & PPC_LI_MASK)
#define PPC_SET_LI(instr,li)         (instr |= (li & PPC_LI_MASK) << PPC_LI_SHIFT)

#define PPC_AA_MASK      0x1
#define PPC_AA_SHIFT     1
#define PPC_GET_AA(instr)           ((instr       >> PPC_AA_SHIFT) & PPC_AA_MASK)
#define PPC_SET_AA(instr,aa)         (instr |= (aa & PPC_AA_MASK) << PPC_AA_SHIFT)

#define PPC_LK_MASK      0x1
#define PPC_LK_SHIFT     0
#define PPC_GET_LK(instr)           ((instr       >> PPC_LK_SHIFT) & PPC_LK_MASK)
#define PPC_SET_LK(instr,lk)         (instr |= (lk & PPC_LK_MASK) << PPC_LK_SHIFT)

#define GEN_B(ppc,dst,aa,lk) \
	{ ppc = NEW_PPC_INSTR(); \
	  PPC_SET_OPCODE(ppc, PPC_OPCODE_B); \
	  PPC_SET_LI    (ppc, (dst)); \
	  PPC_SET_AA    (ppc, (aa)); \
	  PPC_SET_LK    (ppc, (lk)); }

#define GEN_ADDI(ppc,rd,ra,immed) \
	{ ppc = NEW_PPC_INSTR(); \
	  PPC_SET_OPCODE(ppc, PPC_OPCODE_ADDI); \
	  PPC_SET_RD    (ppc, (rd)); \
	  PPC_SET_RA    (ppc, (ra)); \
	  PPC_SET_IMMED (ppc, (immed)); }

#define GEN_LI(ppc,rd,immed) \
	GEN_ADDI(ppc,rd,0,immed)
