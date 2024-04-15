#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// read in little endian byte order
int32_t read32(const uint8_t *ptr) {
    return (*ptr) + (*(ptr + 1) << 8) + (*(ptr + 2) << 16) + (*(ptr + 3) << 24);
}

// --------------------------------------------------
// VM CODE
// --------------------------------------------------

#define NUM_REGS 16

#define FLAG_N 1
#define FLAG_Z 2
#define FLAG_V 4

struct state {
    // Registers
    uint32_t regfile[NUM_REGS]; // general purpose registers: r0, r1 ... r15
    uint32_t flags; // like x86 EFLAGS, ARM CPSR
    uint32_t pc; // program counter

    // Memory
    uint8_t *data; // ".data" section (data segment)
    const uint8_t *code; // ".text" section (code segment)
    // (these could be omitted, and the address space of the VM could be the same as the process)
};

// --------------------------------------------------

void store(struct state *st, uint8_t rptr, uint8_t rval) {
    int8_t ptr = st->regfile[rptr];
    uint8_t val = st->regfile[rval];
    *(st->data + ptr) = val;
}

void load(struct state *st, uint8_t rptr, uint8_t rdst) {
    int8_t ptr = st->regfile[rptr];
    st->regfile[rdst] = *(st->data + ptr);
}

// arithmetic
void setflags(struct state *st, uint64_t res) {
    st->flags = 0;
    if (res == 0) {
        st->flags |= FLAG_Z;
    }
    if ((int32_t) res < 0) {
        st->flags |= FLAG_N;
    }
    // check for overflow
    if (res & ~((uint64_t)((uint32_t) - 1))) {
        st->flags |= FLAG_V;
    }
}

void add(struct state *st, uint8_t rdst, uint8_t rsrc) {
    uint64_t res = st->regfile[rdst] + st->regfile[rsrc];
    st->regfile[rdst] = (uint32_t) res;
    setflags(st, res);
}

void sub(struct state *st, uint8_t rdst, uint8_t rsrc) {
    uint64_t res = st->regfile[rdst] - st->regfile[rsrc];
    st->regfile[rdst] = (uint32_t) res;
    setflags(st, res);
}

void movr(struct state *st, uint8_t rdst, uint8_t rsrc) {
    st->regfile[rdst] = st->regfile[rsrc];
}

void movi(struct state *st, uint8_t rdst, uint8_t immu8) {
    st->regfile[rdst] = immu8;
}

// branching
void beq(struct state *st, int32_t imms32) {
    if (st->flags & FLAG_Z) {
        st->pc += imms32;
    }
}

void bne(struct state *st, int32_t imms32) {
    if (!(st->flags & FLAG_Z)) {
        st->pc += imms32;
    }
}

void blt(struct state *st, int32_t imms32) {
    int n = !!(st->flags & FLAG_N);
    int v = !!(st->flags & FLAG_V);
    if (n != v) {
        st->pc += imms32;
    }
}

// --------------------------------------------------
// VM BYTECODE
// --------------------------------------------------

// Compute the nth fibonacci number.
// r0 is both the parameter and result uint8_t.
//
// f(0) = 1
// f(1) = 1
// f(2) = 2
// f(3) = 3
// f(4) = 5
// f(5) = 8
// ...
//
constexpr uint8_t
fib[] =
"M\x03\x00" // r3 := r0
"I\x01\x01" // r1 := 1
"I\x02\x01" // r2 := 1

// if r3 < 2 -> halt
"I\x04\x02" // r4 := 2
"M\x05\x03" // r5 := r3
"U\x05\x04" // r5 := r5 - r4
"BL\x1b\x00\x00\x00"

// loop begin

"I\x05\x01" // r5 := 1
"U\x03\x05" // r3 := r3 - r5
"M\x04\x02" // r4 := r2
"A\x02\x01" // r2 := r2 + r1
"M\x01\x04" // r1 := r4
"I\x06\x00" // r6 := 0
"A\x06\x03" // r6 := r6 + r3
"BN\xe5\xff\xff\xff" // if r3 != 0 -> loop entry

// loop end (+0x1b bytes)

"I\x00\x00" // r0 := 0
"S\x00\x02" // *r0 := r2
"L\x00\x00" // r0 := *r0
"H" // halt
;

// Specialization,
// 0 - the regular VM interpreter
// 1 - the VM interpreter dispatch specialized to the PC
// 2 - the VM interpreter dispatch specialized to PC "transitions"

#ifndef SPEC
#define SPEC 0
#endif

// --------------------------------------------------
// VM INTERPRETER
// --------------------------------------------------

#if (SPEC == 0)

void interp(struct state *st) {
    while (1) {
        const uint8_t *code = st->code;
        uint32_t pc = st->pc;
        char opcode = code[pc];
#ifdef DEBUG
        printf("pc %02x: %c (%02x)\n", pc, opcode, opcode);
#endif
        const uint8_t *op1 = &code[pc + 1];
        const uint8_t *op2 = &code[pc + 2];
        switch (opcode) {
            case 'S':
                store(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'L':
                load(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'A':
                add(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'U':
                sub(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'B': {
                // assume no oob
                char cc = code[pc + 1];
                int32_t off = read32(&code[pc + 2]);
                switch (cc) {
                    case 'E':
                        // beq
                        beq(st, off);
                        break;
                    case 'N':
                        // bne
                        bne(st, off);
                        break;
                    case 'L':
                        // blt
                        blt(st, off);
                        break;
                    default:
                        goto illegal;
                }
                st->pc += 6;
                break;
            }
            case 'M':
                movr(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'I':
                movi(st, *op1, *op2);
                st->pc += 3;
                break;
            case 'H':
                puts("halt");
                return;
            default:
                goto illegal;
        }
    }
    illegal:
    puts("illegal instruction");
    exit(1);
}

#endif

// https://ctrpeach.io/posts/cpp20-string-literal-template-parameters/
template<size_t N>
struct U8Array {
    constexpr U8Array(const uint8_t (&str)[N]) {
        for (size_t i = 0; i < N; ++i) {
            value[i] = str[i];
        }
    }

    uint8_t value[N];
};

// --------------------------------------------------
// VM INTERPRETER
// interpreter is specialized to code and program counter
// --------------------------------------------------
template<uint32_t pc, U8Array ccode>
__attribute__ ((always_inline))
static int interp_body(struct state *st) {
    const uint8_t *code = ccode.value;
    char opcode = code[pc];
#ifdef DEBUG
    printf("pc %02x: %c (%02x)\n", pc, opcode, opcode);
#endif
    const uint8_t *op1 = &code[pc + 1];
    const uint8_t *op2 = &code[pc + 2];
    switch (opcode) {
        case 'S':
            store(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'L':
            load(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'A':
            add(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'U':
            sub(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'B': {
            // assume no oob
            char cc = code[pc + 1];
            int32_t off = read32(&code[pc + 2]);
            switch (cc) {
                case 'E':
                    // beq
                    beq(st, off);
                    break;
                case 'N':
                    // bne
                    bne(st, off);
                    break;
                case 'L':
                    // blt
                    blt(st, off);
                    break;
                default:
                    goto illegal;
            }
            st->pc += 6;
            break;
        }
        case 'M':
            movr(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'I':
            movi(st, *op1, *op2);
            st->pc += 3;
            break;
        case 'H':
            return 0;
        default:
            goto illegal;
    }
    return 2;
    illegal:
    return 1;
}

#if (SPEC == 1)

// --------------------------------------------------
// VM INTERPRETER
// dispatch is specialized to PC
// --------------------------------------------------

#define DISPATCHSPEC(X) case X:             \
  st->pc = X;                               \
  res = interp_body<X, U8Array(fib)>(st);   \
  if (res == 0) {                           \
    goto halt;                              \
  }                                         \
  if (res == 1) {                           \
    goto illegal;                           \
  }                                         \
  break;

__attribute__ ((noinline))
void interp(struct state *st) {
    while (1) {
        int res;
        switch (st->pc) {
            DISPATCHSPEC(0);
            DISPATCHSPEC(1);
            DISPATCHSPEC(2);
            DISPATCHSPEC(3);
            DISPATCHSPEC(4);
            DISPATCHSPEC(5);
            DISPATCHSPEC(6);
            DISPATCHSPEC(7);
            DISPATCHSPEC(8);
            DISPATCHSPEC(9);
            DISPATCHSPEC(10);
            DISPATCHSPEC(11);
            DISPATCHSPEC(12);
            DISPATCHSPEC(13);
            DISPATCHSPEC(14);
            DISPATCHSPEC(15);
            DISPATCHSPEC(16);
            DISPATCHSPEC(17);
            DISPATCHSPEC(18);
            DISPATCHSPEC(19);
            DISPATCHSPEC(20);
            DISPATCHSPEC(21);
            DISPATCHSPEC(22);
            DISPATCHSPEC(23);
            DISPATCHSPEC(24);
            DISPATCHSPEC(25);
            DISPATCHSPEC(26);
            DISPATCHSPEC(27);
            DISPATCHSPEC(28);
            DISPATCHSPEC(29);
            DISPATCHSPEC(30);
            DISPATCHSPEC(31);
            DISPATCHSPEC(32);
            DISPATCHSPEC(33);
            DISPATCHSPEC(34);
            DISPATCHSPEC(35);
            DISPATCHSPEC(36);
            DISPATCHSPEC(37);
            DISPATCHSPEC(38);
            DISPATCHSPEC(39);
            DISPATCHSPEC(40);
            DISPATCHSPEC(41);
            DISPATCHSPEC(42);
            DISPATCHSPEC(43);
            DISPATCHSPEC(44);
            DISPATCHSPEC(45);
            DISPATCHSPEC(46);
            DISPATCHSPEC(47);
            DISPATCHSPEC(48);
            DISPATCHSPEC(49);
            DISPATCHSPEC(50);
            DISPATCHSPEC(51);
            DISPATCHSPEC(52);
            DISPATCHSPEC(53);
            DISPATCHSPEC(54);
            DISPATCHSPEC(55);
            DISPATCHSPEC(56);
            DISPATCHSPEC(57);
            DISPATCHSPEC(58);
            DISPATCHSPEC(59);
            DISPATCHSPEC(60);
            DISPATCHSPEC(61);
            DISPATCHSPEC(62);
            DISPATCHSPEC(63);
        default:
            goto large_pc;
        }
    }
halt:
    puts("halt");
    return;
illegal:
    puts("illegal instruction");
    exit(1);
large_pc:
    puts("pc was too large at runtime");
    exit(1);
}

#endif

#if (SPEC == 2)

// --------------------------------------------------
// VM INTERPRETER
// dispatch is specialized to PC "transitions"
// --------------------------------------------------

extern void dummy(uint32_t X, uint32_t Y);

// "dummy" is a hack to prevent LLVM from merging the "goto lab_##Y" basic blocks in each DISPATCHSPECPOST.
// we want those basic blocks to remain seperate to get better results with specialization
#define DISPATCHSPECPOST(X, Y) case Y: { dummy(X, Y); goto lab_##Y; }
#define DISPATCHSPEC(X) case X: lab_##X:    \
  st->pc = X;                               \
  res = interp_body<X, U8Array(fib)>(st);   \
  if (res == 0) {                           \
    goto halt;                              \
  }                                         \
  if (res == 1) {                           \
    goto illegal;                           \
  }                                         \
  switch (st->pc) {                         \
    DISPATCHSPECPOST(X, 0);                 \
    DISPATCHSPECPOST(X, 1);                 \
    DISPATCHSPECPOST(X, 2);                 \
    DISPATCHSPECPOST(X, 3);                 \
    DISPATCHSPECPOST(X, 4);                 \
    DISPATCHSPECPOST(X, 5);                 \
    DISPATCHSPECPOST(X, 6);                 \
    DISPATCHSPECPOST(X, 7);                 \
    DISPATCHSPECPOST(X, 8);                 \
    DISPATCHSPECPOST(X, 9);                 \
    DISPATCHSPECPOST(X, 10);                \
    DISPATCHSPECPOST(X, 11);                \
    DISPATCHSPECPOST(X, 12);                \
    DISPATCHSPECPOST(X, 13);                \
    DISPATCHSPECPOST(X, 14);                \
    DISPATCHSPECPOST(X, 15);                \
    DISPATCHSPECPOST(X, 16);                \
    DISPATCHSPECPOST(X, 17);                \
    DISPATCHSPECPOST(X, 18);                \
    DISPATCHSPECPOST(X, 19);                \
    DISPATCHSPECPOST(X, 20);                \
    DISPATCHSPECPOST(X, 21);                \
    DISPATCHSPECPOST(X, 22);                \
    DISPATCHSPECPOST(X, 23);                \
    DISPATCHSPECPOST(X, 24);                \
    DISPATCHSPECPOST(X, 25);                \
    DISPATCHSPECPOST(X, 26);                \
    DISPATCHSPECPOST(X, 27);                \
    DISPATCHSPECPOST(X, 28);                \
    DISPATCHSPECPOST(X, 29);                \
    DISPATCHSPECPOST(X, 30);                \
    DISPATCHSPECPOST(X, 31);                \
    DISPATCHSPECPOST(X, 32);                \
    DISPATCHSPECPOST(X, 33);                \
    DISPATCHSPECPOST(X, 34);                \
    DISPATCHSPECPOST(X, 35);                \
    DISPATCHSPECPOST(X, 36);                \
    DISPATCHSPECPOST(X, 37);                \
    DISPATCHSPECPOST(X, 38);                \
    DISPATCHSPECPOST(X, 39);                \
    DISPATCHSPECPOST(X, 40);                \
    DISPATCHSPECPOST(X, 41);                \
    DISPATCHSPECPOST(X, 42);                \
    DISPATCHSPECPOST(X, 43);                \
    DISPATCHSPECPOST(X, 44);                \
    DISPATCHSPECPOST(X, 45);                \
    DISPATCHSPECPOST(X, 46);                \
    DISPATCHSPECPOST(X, 47);                \
    DISPATCHSPECPOST(X, 48);                \
    DISPATCHSPECPOST(X, 49);                \
    DISPATCHSPECPOST(X, 50);                \
    DISPATCHSPECPOST(X, 51);                \
    DISPATCHSPECPOST(X, 52);                \
    DISPATCHSPECPOST(X, 53);                \
    DISPATCHSPECPOST(X, 54);                \
    DISPATCHSPECPOST(X, 55);                \
    DISPATCHSPECPOST(X, 56);                \
    DISPATCHSPECPOST(X, 57);                \
    DISPATCHSPECPOST(X, 58);                \
    DISPATCHSPECPOST(X, 59);                \
    DISPATCHSPECPOST(X, 60);                \
    DISPATCHSPECPOST(X, 61);                \
    DISPATCHSPECPOST(X, 62);                \
    DISPATCHSPECPOST(X, 63);                \
  default:                                  \
    goto large_pc;                          \
  }

void interp(struct state *st) {
    int res;
    switch (st->pc) {
        DISPATCHSPEC(0);
        DISPATCHSPEC(1);
        DISPATCHSPEC(2);
        DISPATCHSPEC(3);
        DISPATCHSPEC(4);
        DISPATCHSPEC(5);
        DISPATCHSPEC(6);
        DISPATCHSPEC(7);
        DISPATCHSPEC(8);
        DISPATCHSPEC(9);
        DISPATCHSPEC(10);
        DISPATCHSPEC(11);
        DISPATCHSPEC(12);
        DISPATCHSPEC(13);
        DISPATCHSPEC(14);
        DISPATCHSPEC(15);
        DISPATCHSPEC(16);
        DISPATCHSPEC(17);
        DISPATCHSPEC(18);
        DISPATCHSPEC(19);
        DISPATCHSPEC(20);
        DISPATCHSPEC(21);
        DISPATCHSPEC(22);
        DISPATCHSPEC(23);
        DISPATCHSPEC(24);
        DISPATCHSPEC(25);
        DISPATCHSPEC(26);
        DISPATCHSPEC(27);
        DISPATCHSPEC(28);
        DISPATCHSPEC(29);
        DISPATCHSPEC(30);
        DISPATCHSPEC(31);
        DISPATCHSPEC(32);
        DISPATCHSPEC(33);
        DISPATCHSPEC(34);
        DISPATCHSPEC(35);
        DISPATCHSPEC(36);
        DISPATCHSPEC(37);
        DISPATCHSPEC(38);
        DISPATCHSPEC(39);
        DISPATCHSPEC(40);
        DISPATCHSPEC(41);
        DISPATCHSPEC(42);
        DISPATCHSPEC(43);
        DISPATCHSPEC(44);
        DISPATCHSPEC(45);
        DISPATCHSPEC(46);
        DISPATCHSPEC(47);
        DISPATCHSPEC(48);
        DISPATCHSPEC(49);
        DISPATCHSPEC(50);
        DISPATCHSPEC(51);
        DISPATCHSPEC(52);
        DISPATCHSPEC(53);
        DISPATCHSPEC(54);
        DISPATCHSPEC(55);
        DISPATCHSPEC(56);
        DISPATCHSPEC(57);
        DISPATCHSPEC(58);
        DISPATCHSPEC(59);
        DISPATCHSPEC(60);
        DISPATCHSPEC(61);
        DISPATCHSPEC(62);
        DISPATCHSPEC(63);
    default:
        goto large_pc;
    }
halt:
    puts("halt");
    return;
illegal:
    puts("illegal instruction");
    exit(1);
large_pc:
    puts("pc was too large at runtime");
    exit(1);
}

#endif

uint8_t vmdata[0x100];

int main(int argc, char **argv) {
    unsigned long long input;
    if (argc < 2 || ((input = strtoull(argv[1], NULL, 10), errno == ERANGE))) {
        puts("invalid usage");
        exit(1);
    }
    struct state st = {
            .data = vmdata,
            .code = fib
    };

    // Set r0 to the integer provided in argv
    st.regfile[0] = input;
    printf("register r0 input is: %u\n", st.regfile[0]);
    interp(&st);
    printf("register r0 output is: %u\n", st.regfile[0]);
}
