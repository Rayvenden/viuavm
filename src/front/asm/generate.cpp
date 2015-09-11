#include <cstdint>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <viua/bytecode/maps.h>
#include <viua/support/string.h>
#include <viua/support/env.h>
#include <viua/loader.h>
#include <viua/program.h>
#include <viua/cg/tokenizer.h>
#include <viua/cg/assembler/assembler.h>
#include <viua/front/asm.h>
using namespace std;


extern bool VERBOSE;
extern bool DEBUG;
extern bool SCREAM;


// ASSEMBLY CONSTANTS
const string ENTRY_FUNCTION_NAME = "__entry";


tuple<int, enum JUMPTYPE> resolvejump(string jmp, const map<string, int>& marks, int instruction_index = -1) {
    /*  This function is used to resolve jumps in `jump` and `branch` instructions.
     */
    int addr = 0;
    enum JUMPTYPE jump_type = JMP_RELATIVE;
    if (str::isnum(jmp, false)) {
        addr = stoi(jmp);
    } else if (jmp[0] == '.' and str::isnum(str::sub(jmp, 1))) {
        addr = stoi(str::sub(jmp, 1));
        jump_type = JMP_ABSOLUTE;
    } else if (jmp.substr(0, 2) == "0x") {
        stringstream ss;
        ss << hex << jmp;
        ss >> addr;
        jump_type = JMP_TO_BYTE;
    } else if (jmp[0] == '-') {
        if (instruction_index < 0) { throw ("invalid use of relative jump: " + jmp); }
        addr = (instruction_index + stoi(jmp));
        if (addr < 0) { throw "use of relative jump results in a jump to negative index"; }
    } else if (jmp[0] == '+') {
        if (instruction_index < 0) { throw ("invalid use of relative jump: " + jmp); }
        addr = (instruction_index + stoi(jmp.substr(1)));
    } else if (jmp[0] == '.') {
        // FIXME
        cout << "FIXME: global marker jumps (jumps to functions) are not implemented yet" << endl;
        exit(1);
    } else {
        try {
            addr = marks.at(jmp);
        } catch (const std::out_of_range& e) {
            throw ("jump to unrecognised marker: " + jmp);
        }
    }
    return tuple<int, enum JUMPTYPE>(addr, jump_type);
}

string resolveregister(string reg, const map<string, int>& names) {
    /*  This function is used to register numbers when a register is accessed, e.g.
     *  in `istore` instruction or in `branch` in condition operand.
     *
     *  This function MUST return string as teh result is further passed to assembler::operands::getint() function which *expects* string.
     */
    ostringstream out;
    if (str::isnum(reg)) {
        /*  Basic case - the register is accessed as real index, everything is nice and simple.
         */
        out.str(reg);
    } else if (reg[0] == '@' and str::isnum(str::sub(reg, 1))) {
        /*  Basic case - the register index is taken from another register, everything is still nice and simple.
         */
        out.str(reg);
    } else {
        /*  Case is no longer basic - it seems that a register is being accessed by name.
         *  Names must be checked to see if the one used was declared.
         */
        if (reg[0] == '@') {
            out << '@';
            reg = str::sub(reg, 1);
        }
        try {
            out << names.at(reg);
        } catch (const std::out_of_range& e) {
            // first, check if the name is non-empty
            if (reg != "") {
                // Jinkies! This name was not declared.
                throw ("undeclared name: " + reg);
            } else {
                throw "not enough operands";
            }
        }
    }
    return out.str();
}


/*  This is a mapping of instructions to their assembly functions.
 *  Used in the assembly() function.
 *
 *  It is suitable for all instructions which use three, simple register-index operands.
 *
 *  BE WARNED!
 *  This mapping (and the assemble_three_intop_instruction() function) *greatly* reduce the amount of code repetition
 *  in the assembler but are kinda black voodoo magic...
 *
 *  NOTE TO FUTURE SELF:
 *  If you feel comfortable with taking pointers of member functions and calling such things - go on.
 *  Otherwise, it may be better to leave this alone until your have refreshed your memory.
 *  Here is isocpp.org's FAQ about pointers to members (2015-01-17): https://isocpp.org/wiki/faq/pointers-to-members
 */
typedef Program& (Program::*ThreeIntopAssemblerFunction)(int_op, int_op, int_op);
const map<string, ThreeIntopAssemblerFunction> THREE_INTOP_ASM_FUNCTIONS = {
    { "iadd", &Program::iadd },
    { "isub", &Program::isub },
    { "imul", &Program::imul },
    { "idiv", &Program::idiv },
    { "ilt",  &Program::ilt },
    { "ilte", &Program::ilte },
    { "igt",  &Program::igt },
    { "igte", &Program::igte },
    { "ieq",  &Program::ieq },

    { "fadd", &Program::fadd },
    { "fsub", &Program::fsub },
    { "fmul", &Program::fmul },
    { "fdiv", &Program::fdiv },
    { "flt",  &Program::flt },
    { "flte", &Program::flte },
    { "fgt",  &Program::fgt },
    { "fgte", &Program::fgte },
    { "feq",  &Program::feq },

    { "and",  &Program::logand },
    { "or",   &Program::logor },
};

void assemble_three_intop_instruction(Program& program, map<string, int>& names, const string& instr, const string& operands) {
    string rega, regb, regr;
    tie(rega, regb, regr) = assembler::operands::get3(operands);
    rega = resolveregister(rega, names);
    regb = resolveregister(regb, names);
    regr = resolveregister(regr, names);

    // feed chunks into Bytecode Programming API
    try {
        (program.*THREE_INTOP_ASM_FUNCTIONS.at(instr))(assembler::operands::getint(rega), assembler::operands::getint(regb), assembler::operands::getint(regr));
    } catch (const std::out_of_range& e) {
        throw ("instruction is not present in THREE_INTOP_ASM_FUNCTIONS map but it should be: " + instr);
    }
}


vector<string> filter(const vector<string>& lines) {
    /** Return lines for current function.
     *
     *  Filters out all non-local (i.e. outside the scope of current function) and non-opcode lines.
     */
    vector<string> filtered;

    string line;
    for (unsigned i = 0; i < lines.size(); ++i) {
        line = lines[i];
        if (str::startswith(line, ".mark:") or str::startswith(line, ".name:") or str::startswith(line, ".main:") or str::startswith(line, ".link:") or str::startswith(line, ".signature:") or str::startswith(line, ".bsignature:") or str::startswith(line, ".type:")) {
            /*  Lines beginning with `.mark:` are just markers placed in code and
             *  are do not produce any bytecode.
             *  Lines beginning with `.name:` are asm directives that assign human-rememberable names to
             *  registers.
             *  Lines beginning with `.signature:` and `.bsignature:` covey information about functions and
             *  blocks that will be available at runtime but may not be available during compilation.
             *  Lines beginning with `.type:` inform the assembler that definition of this type will be
             *  supplied later (either by `.class:` block, or by dynamically defining the type during runtime).
             *
             *  Assembler directives are discarded by the assembler during the bytecode-generation phase
             *  so they can be skipped in this step as fast as possible
             *  to avoid complicating code that appears later and
             *  deals with assembling CPU instructions.
             */
            continue;
        }

        if (str::startswith(line, ".function:") or str::startswith(line, ".block:")) {
            // skip function and block definition blocks
            while (lines[++i] != ".end");
            continue;
        }
        if (str::startswith(line, ".class:")) {
            // skip class definition lines
            while (lines[++i] != ".end");
            continue;
        }

        filtered.push_back(line);
    }

    return filtered;
}

Program& compile(Program& program, const vector<string>& lines, map<string, int>& marks, map<string, int>& names) {
    /** Compile instructions into bytecode using bytecode generation API.
     *
     */
    vector<string> ilines = filter(lines);

    string line;
    int instruction = 0;  // instruction counter
    for (unsigned i = 0; i < ilines.size(); ++i) {
        /*  This is main assembly loop.
         *  It iterates over lines with instructions and
         *  uses bytecode generation API to fill the program with instructions and
         *  from them generate the bytecode.
         */
        line = ilines[i];

        string instr;
        string operands;
        istringstream iss(line);

        instr = str::chunk(line);
        operands = str::lstrip(str::sub(line, instr.size()));

        vector<string> tokens = tokenize(operands);

        if (DEBUG and SCREAM) {
            cout << "[asm] compiling line: `" << line << "`" << endl;
        }

        if (line == "nop") {
            program.nop();
        } else if (str::startswith(line, "izero")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.izero(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "istore")) {
            string regno_chnk, number_chnk;
            tie(regno_chnk, number_chnk) = assembler::operands::get2(operands);
            program.istore(assembler::operands::getint(resolveregister(regno_chnk, names)), assembler::operands::getint(resolveregister(number_chnk, names)));
        } else if (str::startswith(line, "iadd")) {
            assemble_three_intop_instruction(program, names, "iadd", operands);
        } else if (str::startswith(line, "isub")) {
            assemble_three_intop_instruction(program, names, "isub", operands);
        } else if (str::startswith(line, "imul")) {
            assemble_three_intop_instruction(program, names, "imul", operands);
        } else if (str::startswith(line, "idiv")) {
            assemble_three_intop_instruction(program, names, "idiv", operands);
        } else if (str::startswithchunk(line, "ilt")) {
            assemble_three_intop_instruction(program, names, "ilt", operands);
        } else if (str::startswithchunk(line, "ilte")) {
            assemble_three_intop_instruction(program, names, "ilte", operands);
        } else if (str::startswith(line, "igte")) {
            assemble_three_intop_instruction(program, names, "igte", operands);
        } else if (str::startswith(line, "igt")) {
            assemble_three_intop_instruction(program, names, "igt", operands);
        } else if (str::startswith(line, "ieq")) {
            assemble_three_intop_instruction(program, names, "ieq", operands);
        } else if (str::startswith(line, "iinc")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.iinc(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "idec")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.idec(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "fstore")) {
            string regno_chnk, float_chnk;
            tie(regno_chnk, float_chnk) = assembler::operands::get2(operands);
            program.fstore(assembler::operands::getint(resolveregister(regno_chnk, names)), stod(float_chnk));
        } else if (str::startswith(line, "fadd")) {
            assemble_three_intop_instruction(program, names, "fadd", operands);
        } else if (str::startswith(line, "fsub")) {
            assemble_three_intop_instruction(program, names, "fsub", operands);
        } else if (str::startswith(line, "fmul")) {
            assemble_three_intop_instruction(program, names, "fmul", operands);
        } else if (str::startswith(line, "fdiv")) {
            assemble_three_intop_instruction(program, names, "fdiv", operands);
        } else if (str::startswithchunk(line, "flt")) {
            assemble_three_intop_instruction(program, names, "flt", operands);
        } else if (str::startswithchunk(line, "flte")) {
            assemble_three_intop_instruction(program, names, "flte", operands);
        } else if (str::startswithchunk(line, "fgt")) {
            assemble_three_intop_instruction(program, names, "fgt", operands);
        } else if (str::startswithchunk(line, "fgte")) {
            assemble_three_intop_instruction(program, names, "fgte", operands);
        } else if (str::startswith(line, "feq")) {
            assemble_three_intop_instruction(program, names, "feq", operands);
        } else if (str::startswith(line, "bstore")) {
            string regno_chnk, byte_chnk;
            tie(regno_chnk, byte_chnk) = assembler::operands::get2(operands);
            program.bstore(assembler::operands::getint(resolveregister(regno_chnk, names)), assembler::operands::getbyte(resolveregister(byte_chnk, names)));
        } else if (str::startswith(line, "itof")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            if (b_chnk.size() == 0) { b_chnk = a_chnk; }
            program.itof(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "ftoi")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            if (b_chnk.size() == 0) { b_chnk = a_chnk; }
            program.ftoi(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "stoi")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            if (b_chnk.size() == 0) { b_chnk = a_chnk; }
            program.stoi(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "stof")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            if (b_chnk.size() == 0) { b_chnk = a_chnk; }
            program.stof(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "strstore")) {
            string reg_chnk, str_chnk;
            reg_chnk = str::chunk(operands);
            operands = str::lstrip(str::sub(operands, reg_chnk.size()));
            str_chnk = str::extract(operands);
            program.strstore(assembler::operands::getint(resolveregister(reg_chnk, names)), str_chnk);
        } else if (str::startswith(line, "vec")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.vec(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "vinsert")) {
            string vec, src, pos;
            tie(vec, src, pos) = assembler::operands::get3(operands, false);
            if (pos == "") { pos = "0"; }
            program.vinsert(assembler::operands::getint(resolveregister(vec, names)), assembler::operands::getint(resolveregister(src, names)), assembler::operands::getint(resolveregister(pos, names)));
        } else if (str::startswith(line, "vpush")) {
            string regno_chnk, number_chnk;
            tie(regno_chnk, number_chnk) = assembler::operands::get2(operands);
            program.vpush(assembler::operands::getint(resolveregister(regno_chnk, names)), assembler::operands::getint(resolveregister(number_chnk, names)));
        } else if (str::startswith(line, "vpop")) {
            string vec, dst, pos;
            tie(vec, dst, pos) = assembler::operands::get3(operands, false);
            if (dst == "") { dst = "0"; }
            if (pos == "") { pos = "-1"; }
            program.vpop(assembler::operands::getint(resolveregister(vec, names)), assembler::operands::getint(resolveregister(dst, names)), assembler::operands::getint(resolveregister(pos, names)));
        } else if (str::startswith(line, "vat")) {
            string vec, dst, pos;
            tie(vec, dst, pos) = assembler::operands::get3(operands, false);
            if (pos == "") { pos = "-1"; }
            program.vat(assembler::operands::getint(resolveregister(vec, names)), assembler::operands::getint(resolveregister(dst, names)), assembler::operands::getint(resolveregister(pos, names)));
        } else if (str::startswith(line, "vlen")) {
            string regno_chnk, number_chnk;
            tie(regno_chnk, number_chnk) = assembler::operands::get2(operands);
            program.vlen(assembler::operands::getint(resolveregister(regno_chnk, names)), assembler::operands::getint(resolveregister(number_chnk, names)));
        } else if (str::startswith(line, "not")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.lognot(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "and")) {
            assemble_three_intop_instruction(program, names, "and", operands);
        } else if (str::startswith(line, "or")) {
            assemble_three_intop_instruction(program, names, "or", operands);
        } else if (str::startswith(line, "move")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.move(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "copy")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.copy(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "ref")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.ref(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "ptr")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.ptr(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "deptr")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.deptr(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "swap")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.swap(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "free")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.free(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "empty")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.empty(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "isnull")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.isnull(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "isptr")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.isptr(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "ress")) {
            program.ress(operands);
        } else if (str::startswith(line, "tmpri")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.tmpri(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "tmpro")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.tmpro(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "print")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.print(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "echo")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.echo(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "clbind")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.clbind(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "closure")) {
            string fn_name, reg;
            tie(reg, fn_name) = assembler::operands::get2(operands);
            program.closure(assembler::operands::getint(resolveregister(reg, names)), fn_name);
        } else if (str::startswith(line, "function")) {
            string fn_name, reg;
            tie(reg, fn_name) = assembler::operands::get2(operands);
            program.function(assembler::operands::getint(resolveregister(reg, names)), fn_name);
        } else if (str::startswith(line, "fcall")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.fcall(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "frame")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            if (a_chnk.size() == 0) { a_chnk = "0"; }
            if (b_chnk.size() == 0) { b_chnk = "16"; }  // default number of local registers
            program.frame(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "param")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.param(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "paref")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.paref(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "paptr")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.paptr(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswithchunk(line, "arg")) {
            string a_chnk, b_chnk;
            tie(a_chnk, b_chnk) = assembler::operands::get2(operands);
            program.arg(assembler::operands::getint(resolveregister(a_chnk, names)), assembler::operands::getint(resolveregister(b_chnk, names)));
        } else if (str::startswith(line, "argc")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.argc(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "call")) {
            /** Full form of call instruction has two operands: function name and return value register index.
             *  If call is given only one operand - it means it is the instruction index and returned value is discarded.
             *  To explicitly state that return value should be discarderd 0 can be supplied as second operand.
             */
            /** Why is the function supplied as a *string* and not direct instruction pointer?
             *  That would be faster - c'mon couldn't assembler just calculate offsets and insert them?
             *
             *  Nope.
             *
             *  Yes, it *would* be faster if calls were just precalculated jumps.
             *  However, by them being strings we get plenty of flexibility, good-quality stack traces, and
             *  a place to put plenty of debugging info.
             *  All that at a cost of just one map lookup; the overhead is minimal and gains are big.
             *  What's not to love?
             *
             *  Of course, you, my dear reader, are free to take this code (it's GPL after all!) and
             *  modify it to suit your particular needs - in that case that would be calculating call jumps
             *  at compile time and exchanging CALL instructions with JUMP instructions.
             *
             *  Good luck with debugging your code, then.
             */
            string fn_name, reg;
            tie(reg, fn_name) = assembler::operands::get2(operands);

            // if second operand is empty, fill it with zero
            // which means that return value will be discarded
            if (fn_name == "") {
                fn_name = reg;
                reg = "0";
            }

            program.call(assembler::operands::getint(resolveregister(reg, names)), fn_name);
        } else if (str::startswith(line, "branch")) {
            /*  If branch is given three operands, it means its full, three-operands form is being used.
             *  Otherwise, it is short, two-operands form instruction and assembler should fill third operand accordingly.
             *
             *  In case of short-form `branch` instruction:
             *
             *      * first operand is index of the register to check,
             *      * second operand is the address to which to jump if register is true,
             *      * third operand is assumed to be the *next instruction*, i.e. instruction after the branch instruction,
             *
             *  In full (with three operands) form of `branch` instruction:
             *
             *      * third operands is the address to which to jump if register is false,
             */
            string condition, if_true, if_false;
            tie(condition, if_true, if_false) = assembler::operands::get3(operands, false);

            int addrt_target, addrf_target;
            enum JUMPTYPE addrt_jump_type, addrf_jump_type;
            tie(addrt_target, addrt_jump_type) = resolvejump(if_true, marks, i);
            if (if_false != "") {
                tie(addrf_target, addrf_jump_type) = resolvejump(if_false, marks, i);
            } else {
                addrf_jump_type = JMP_RELATIVE;
                addrf_target = instruction+1;
            }

            if (DEBUG) {
                if (addrt_jump_type == JMP_TO_BYTE) {
                    cout << line << " => truth jump to byte";
                } else if (addrt_jump_type == JMP_ABSOLUTE) {
                    cout << line << " => truth absolute jump";
                } else {
                    cout << line << " => truth relative jump";
                }
                cout << ": " << addrt_target << endl;

                if (addrf_jump_type == JMP_TO_BYTE) {
                    cout << line << " => false jump to byte";
                } else if (addrf_jump_type == JMP_ABSOLUTE) {
                    cout << line << " => false absolute jump";
                } else {
                    cout << line << " => false relative jump";
                }
                cout << ": " << addrf_target << endl;
            }

            program.branch(assembler::operands::getint(resolveregister(condition, names)), addrt_target, addrt_jump_type, addrf_target, addrf_jump_type);
        } else if (str::startswith(line, "jump")) {
            /*  Jump instruction can be written in two forms:
             *
             *      * `jump <index>`
             *      * `jump :<marker>`
             *
             *  Assembler must distinguish between these two forms, and so it does.
             *  Here, we use a function from string support lib to determine
             *  if the jump is numeric, and thus an index, or
             *  a string - in which case we consider it a marker jump.
             *
             *  If it is a marker jump, assembler will look the marker up in a map and
             *  if it is not found throw an exception about unrecognised marker being used.
             */
            int jump_target;
            enum JUMPTYPE jump_type;
            tie(jump_target, jump_type) = resolvejump(operands, marks, i);

            if (DEBUG) {
                if (jump_type == JMP_TO_BYTE) {
                    cout << line << " => false jump to byte";
                } else if (jump_type == JMP_ABSOLUTE) {
                    cout << line << " => false absolute jump";
                } else {
                    cout << line << " => false relative jump";
                }
                cout << ": " << jump_target << endl;
            }

            program.jump(jump_target, jump_type);
        } else if (str::startswith(line, "tryframe")) {
            program.tryframe();
        } else if (str::startswith(line, "catch")) {
            string type_chnk, catcher_chnk;
            type_chnk = str::extract(operands);
            operands = str::lstrip(str::sub(operands, type_chnk.size()));
            catcher_chnk = str::chunk(operands);
            program.vmcatch(type_chnk, catcher_chnk);
        } else if (str::startswith(line, "pull")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.pull(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "try")) {
            string block_name = str::chunk(operands);
            program.vmtry(block_name);
        } else if (str::startswith(line, "throw")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.vmthrow(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "leave")) {
            program.leave();
        } else if (str::startswith(line, "import")) {
            string str_chnk;
            str_chnk = str::extract(operands);
            program.import(str_chnk);
        } else if (str::startswith(line, "link")) {
            string str_chnk;
            str_chnk = str::chunk(operands);
            program.link(str_chnk);
        } else if (str::startswith(line, "class")) {
            string class_name, reg;
            tie(reg, class_name) = assembler::operands::get2(operands);
            program.vmclass(assembler::operands::getint(resolveregister(reg, names)), class_name);
        } else if (str::startswith(line, "derive")) {
            string base_class_name, reg;
            tie(reg, base_class_name) = assembler::operands::get2(operands);
            program.vmderive(assembler::operands::getint(resolveregister(reg, names)), base_class_name);
        } else if (str::startswith(line, "attach")) {
            string function_name, method_name, reg;
            tie(reg, function_name, method_name) = assembler::operands::get3(operands);
            program.vmattach(assembler::operands::getint(resolveregister(reg, names)), function_name, method_name);
        } else if (str::startswith(line, "register")) {
            string regno_chnk;
            regno_chnk = str::chunk(operands);
            program.vmregister(assembler::operands::getint(resolveregister(regno_chnk, names)));
        } else if (str::startswith(line, "new")) {
            string class_name, reg;
            tie(reg, class_name) = assembler::operands::get2(operands);
            program.vmnew(assembler::operands::getint(resolveregister(reg, names)), class_name);
        } else if (str::startswith(line, "msg")) {
            string reg, mtd;
            tie(reg, mtd) = assembler::operands::get2(operands);
            program.vmmsg(assembler::operands::getint(resolveregister(reg, names)), mtd);
        } else if (str::startswith(line, "end")) {
            program.end();
        } else if (str::startswith(line, "halt")) {
            program.halt();
        } else {
            throw ("unimplemented instruction: " + instr);
        }
        ++instruction;
    }

    return program;
}


void assemble(Program& program, const vector<string>& lines) {
    /** Assemble instructions in lines into a program.
     *  This function first garthers required information about markers, named registers and functions.
     *  Then, it passes all gathered data into compilation function.
     *
     *  :params:
     *
     *  program         - Program object which will be used for assembling
     *  lines           - lines with instructions
     */
    map<string, int> marks = assembler::ce::getmarks(lines);
    map<string, int> names = assembler::ce::getnames(lines);
    compile(program, lines, marks, names);
}


map<string, uint16_t> mapInvokableAddresses(uint16_t& starting_instruction, const vector<string>& names, const map<string, vector<string> >& sources) {
    map<string, uint16_t> addresses;
    for (string name : names) {
        addresses[name] = starting_instruction;
        try {
            starting_instruction += Program::countBytes(sources.at(name));
        } catch (const std::out_of_range& e) {
            throw ("could not find block '" + name + "'");
        }
    }
    return addresses;
}

vector<string> expandSource(const vector<string>& lines, map<unsigned, unsigned>& expanded_lines_to_source_lines) {
    vector<string> stripped_lines;

    for (unsigned i = 0; i < lines.size(); ++i) {
        stripped_lines.push_back(str::lstrip(lines[i]));
    }

    vector<string> asm_lines;
    for (unsigned i = 0; i < stripped_lines.size(); ++i) {
        if (stripped_lines[i] == "") {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (str::startswith(stripped_lines[i], ".signature")) {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (str::startswith(stripped_lines[i], ".bsignature")) {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (str::startswith(stripped_lines[i], ".function")) {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (str::startswith(stripped_lines[i], ".end")) {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (stripped_lines[i][0] == ';') {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else if (not str::contains(stripped_lines[i], '(')) {
            expanded_lines_to_source_lines[asm_lines.size()] = i;
            asm_lines.push_back(lines[i]);
        } else {
            vector<vector<string>> decoded_lines = decode_line(stripped_lines[i]);
            unsigned indent = (lines[i].size() - stripped_lines[i].size());
            for (unsigned j = 0; j < decoded_lines.size(); ++j) {
                expanded_lines_to_source_lines[asm_lines.size()] = i;
                asm_lines.push_back(str::strmul<char>(' ', indent) + str::join<char>(decoded_lines[j], ' '));
            }
        }
    }

    return asm_lines;
}

int generate(const vector<string>& expanded_lines, const map<unsigned, unsigned>& expanded_lines_to_source_lines, vector<string>& ilines, invocables_t& functions, invocables_t& blocks, string& filename, string& compilename, const vector<string>& commandline_given_links, const compilationflags_t& flags) {
    //////////////////////////////
    // SETUP INITIAL BYTECODE SIZE
    uint16_t bytes = 0;


    /////////////////////////
    // GET MAIN FUNCTION NAME
    string main_function = "";
    for (string line : ilines) {
        if (str::startswith(line, ".main:")) {
            if (DEBUG) {
                cout << "setting main function to: ";
            }
            main_function = str::lstrip(str::sub(line, 6));
            cout << main_function << endl;
            break;
        }
    }
    if (main_function == "" and not flags.as_lib) {
        main_function = "main";
    }
    if (((VERBOSE and main_function != "main" and main_function != "") or DEBUG) and not flags.as_lib) {
        cout << "debug (notice): main function set to: '" << main_function << "'" << endl;
    }


    /////////////////////////////////////////
    // CHECK IF MAIN FUNCTION RETURNS A VALUE
    // FIXME: this is just a crude check - it does not acctually checks if these instructions set 0 register
    // this must be better implemented or we will receive "function did not set return register" exceptions at runtime
    bool main_is_defined = (find(functions.names.begin(), functions.names.end(), main_function) != functions.names.end());
    if (not flags.as_lib and main_is_defined) {
        string main_second_but_last;
        try {
            main_second_but_last = *(functions.bodies.at(main_function).end()-2);
        } catch (const std::out_of_range& e) {
            cout << "[asm] fatal: could not find main function (during return value check)" << endl;
            exit(1);
        }
        if (!str::startswith(main_second_but_last, "copy") and
            !str::startswith(main_second_but_last, "move") and
            !str::startswith(main_second_but_last, "swap") and
            !str::startswith(main_second_but_last, "izero")
            ) {
            cout << "fatal: main function does not return a value" << endl;
            return 1;
        }
    }
    if (not main_is_defined and (DEBUG or VERBOSE) and not flags.as_lib) {
        cout << "notice: main function (" << main_function << ") is not defined, deferring main function check to post-link phase" << endl;
    }


    /////////////////////////////////
    // MAP FUNCTIONS TO ADDRESSES AND
    // MAP blocks.bodies TO ADDRESSES AND
    // SET STARTING INSTRUCTION
    uint16_t starting_instruction = 0;  // the bytecode offset to first executable instruction
    map<string, uint16_t> function_addresses;
    map<string, uint16_t> block_addresses;
    try {
        block_addresses = mapInvokableAddresses(starting_instruction, blocks.names, blocks.bodies);
        function_addresses = mapInvokableAddresses(starting_instruction, functions.names, functions.bodies);
        bytes = Program::countBytes(ilines);
    } catch (const string& e) {
        cout << "error: bytecode size calculation failed: " << e << endl;
        return 1;
    }


    //////////////////////////
    // GENERATE ENTRY FUNCTION
    if (not flags.as_lib) {
        if (DEBUG) {
            cout << "generating __entry function" << endl;
        }
        functions.names.push_back(ENTRY_FUNCTION_NAME);
        function_addresses[ENTRY_FUNCTION_NAME] = starting_instruction;
        // entry function sets global stuff (FIXME: not really)
        ilines.insert(ilines.begin(), "ress local");
        // append entry function instructions...
        ilines.push_back("frame 1");
        ilines.push_back("param 0 1");
        // this must not be hardcoded because we have '.main:' assembler instruction
        // we also save return value in 1 register since 0 means "drop return value"
        ilines.push_back("call 1 " + main_function);
        // then, register 1 is moved to register 0 so it counts as a return code
        ilines.push_back("move 0 1");
        ilines.push_back("halt");
        functions.bodies[ENTRY_FUNCTION_NAME] = ilines;
        // instructions were added so bytecode size must be inreased
        bytes += OP_SIZES.at("ress");
        bytes += OP_SIZES.at("frame");
        bytes += OP_SIZES.at("param");
        bytes += OP_SIZES.at("call");
        bytes += main_function.size()+1;
        bytes += OP_SIZES.at("move");
        bytes += OP_SIZES.at("halt");
    }


    /////////////////////////////////////////////////////////
    // GATHER LINKS, GET THEIR SIZES AND ADJUST BYTECODE SIZE
    vector<string> links = assembler::ce::getlinks(ilines);
    vector<tuple<string, uint16_t, char*> > linked_libs_bytecode;
    vector<string> linked_function_names;
    vector<string> linked_block_names;
    map<string, vector<unsigned> > linked_libs_jumptables;
    uint16_t current_link_offset = bytes;

    for (string lnk : commandline_given_links) {
        if (find(links.begin(), links.end(), lnk) == links.end()) {
            links.push_back(lnk);
        }
    }

    for (string lnk : links) {
        if (DEBUG or VERBOSE) {
            cout << "[loader] message: linking with: '" << lnk << "\'" << endl;
        }

        Loader loader(lnk);
        loader.load();

        vector<unsigned> lib_jumps = loader.getJumps();
        if (DEBUG) {
            cout << "[loader] entries in jump table: " << lib_jumps.size() << endl;
            for (unsigned i = 0; i < lib_jumps.size(); ++i) {
                cout << "  jump at byte: " << lib_jumps[i] << endl;
            }
        }

        linked_libs_jumptables[lnk] = lib_jumps;

        map<string, uint16_t> fn_addresses = loader.getFunctionAddresses();
        vector<string> fn_names = loader.getFunctions();
        for (string fn : fn_names) {
            function_addresses[fn] = fn_addresses.at(fn) + current_link_offset;
            linked_function_names.push_back(fn);
            if (DEBUG) {
                cout << "  \"" << fn << "\": entry point at byte: " << current_link_offset << '+' << fn_addresses.at(fn) << endl;
            }
        }

        linked_libs_bytecode.push_back( tuple<string, uint16_t, char*>(lnk, loader.getBytecodeSize(), loader.getBytecode()) );
        bytes += loader.getBytecodeSize();
    }


    //////////////////////////////////////////////////////////////
    // EXTEND FUNCTION NAMES VECTOR WITH NAMES OF LINKED FUNCTIONS
    for (string name : linked_function_names) { functions.names.push_back(name); }


    /////////////////////////////////////////////////////////////////////////
    // AFTER HAVING OBTAINED LINKED NAMES, IT IS POSSIBLE TO VERIFY CALLS AND
    // CALLABLE (FUNCTIONS, CLOSURES, ETC.) CREATIONS
    string report;
    if ((report = assembler::verify::functionCallsAreDefined(expanded_lines, expanded_lines_to_source_lines, functions.names, functions.signatures)).size()) {
        cout << report << endl;
        exit(1);
    }
    if ((report = assembler::verify::callableCreations(expanded_lines, expanded_lines_to_source_lines, functions.names, functions.signatures)).size()) {
        cout << report << endl;
        exit(1);
    }


    /////////////////////////////
    // REPORT TOTAL BYTECODE SIZE
    if ((VERBOSE or DEBUG) and linked_function_names.size() != 0) {
        cout << "message: total required bytes: " << bytes << " bytes" << endl;
    }
    if (DEBUG) {
        cout << "debug: required bytes: " << (bytes-(bytes-current_link_offset)) << " local" << endl;
        cout << "debug: required bytes: " << (bytes-current_link_offset) << " linked" << endl;
    }


    ///////////////////////////
    // REPORT FIRST INSTRUCTION
    if ((VERBOSE or DEBUG) and not flags.as_lib) {
        cout << "message: first instruction pointer: " << starting_instruction << endl;
    }


    ////////////////////////////////////////
    // CREATE OFSTREAM TO WRITE BYTECODE OUT
    ofstream out(compilename, ios::out | ios::binary);


    ////////////////////
    // CREATE JUMP TABLE
    vector<unsigned> jump_table;


    /////////////////////////////////////////////////////////
    // GENERATE BYTECODE OF LOCAL FUNCTIONS AND blocks.bodies
    //
    // BYTECODE IS GENERATED HERE BUT NOT YET WRITTEN TO FILE
    // THIS MUST BE GENERATED HERE TO OBTAIN FILL JUMP TABLE
    map<string, tuple<int, byte*> > functions_bytecode;
    map<string, tuple<int, byte*> > block_bodies_bytecode;
    int functions_section_size = 0;
    int block_bodies_section_size = 0;

    vector<tuple<int, int> > jump_positions;

    for (string name : blocks.names) {
        // do not generate bytecode for blocks.bodies that were linked
        if (find(linked_block_names.begin(), linked_block_names.end(), name) != linked_block_names.end()) { continue; }

        if (VERBOSE or DEBUG) {
            cout << "[asm] message: generating bytecode for block \"" << name << '"';
        }
        uint16_t fun_bytes = 0;
        try {
            fun_bytes = Program::countBytes(blocks.bodies.at(name));
            if (VERBOSE or DEBUG) {
                cout << " (" << fun_bytes << " bytes at byte " << block_bodies_section_size << ')' << endl;
            }
        } catch (const string& e) {
            cout << "fatal: error during block size count (pre-assembling): " << e << endl;
            exit(1);
        } catch (const std::out_of_range& e) {
            cout << e.what() << endl;
            exit(1);
        }

        Program func(fun_bytes);
        func.setdebug(DEBUG).setscream(SCREAM);
        try {
            assemble(func, blocks.bodies.at(name));
        } catch (const string& e) {
            cout << (DEBUG ? "\n" : "") << "fatal: error during assembling: " << e << endl;
            exit(1);
        } catch (const char*& e) {
            cout << (DEBUG ? "\n" : "") << "fatal: error during assembling: " << e << endl;
            exit(1);
        } catch (const std::out_of_range& e) {
            cout << (DEBUG ? "\n" : "") << "[asm] fatal: could not assemble block '" << name << "' (" << e.what() << ')' << endl;
            exit(1);
        }

        vector<unsigned> jumps = func.jumps();
        vector<unsigned> jumps_absolute = func.jumpsAbsolute();

        vector<tuple<int, int> > local_jumps;
        for (unsigned i = 0; i < jumps.size(); ++i) {
            unsigned jmp = jumps[i];
            local_jumps.push_back(tuple<int, int>(jmp, block_bodies_section_size));
        }
        func.calculateJumps(local_jumps);

        byte* btcode = func.bytecode();

        // store generated bytecode fragment for future use (we must not yet write it to the file to conform to bytecode format)
        block_bodies_bytecode[name] = tuple<int, byte*>(func.size(), btcode);

        // extend jump table with jumps from current block
        for (unsigned i = 0; i < jumps.size(); ++i) {
            unsigned jmp = jumps[i];
            if (DEBUG) {
                cout << "[asm] debug: pushed relative jump to jump table: " << jmp << '+' << block_bodies_section_size << endl;
            }
            jump_table.push_back(jmp+block_bodies_section_size);
        }

        for (unsigned i = 0; i < jumps_absolute.size(); ++i) {
            if (DEBUG) {
                cout << "[asm] debug: pushed absolute jump to jump table: " << jumps_absolute[i] << "+0" << endl;
            }
            jump_positions.push_back(tuple<int, int>(jumps_absolute[i]+block_bodies_section_size, 0));
        }

        block_bodies_section_size += func.size();
    }

    // functions section size, must be offset by the size of block section
    functions_section_size = block_bodies_section_size;

    for (string name : functions.names) {
        // do not generate bytecode for functions that were linked
        if (find(linked_function_names.begin(), linked_function_names.end(), name) != linked_function_names.end()) { continue; }

        if (VERBOSE or DEBUG) {
            cout << "[asm] message: generating bytecode for function \"" << name << '"';
        }
        uint16_t fun_bytes = 0;
        try {
            fun_bytes = Program::countBytes(name == ENTRY_FUNCTION_NAME ? filter(functions.bodies.at(name)) : functions.bodies.at(name));
            if (VERBOSE or DEBUG) {
                cout << " (" << fun_bytes << " bytes at byte " << functions_section_size << ')' << endl;
            }
        } catch (const string& e) {
            cout << "fatal: error during function size count (pre-assembling): " << e << endl;
            exit(1);
        } catch (const std::out_of_range& e) {
            cout << e.what() << endl;
            exit(1);
        }

        Program func(fun_bytes);
        func.setdebug(DEBUG).setscream(SCREAM);
        try {
            assemble(func, functions.bodies.at(name));
        } catch (const string& e) {
            cout << (DEBUG ? "\n" : "") << "fatal: error during assembling: " << e << endl;
            exit(1);
        } catch (const char*& e) {
            cout << (DEBUG ? "\n" : "") << "fatal: error during assembling: " << e << endl;
            exit(1);
        } catch (const std::out_of_range& e) {
            cout << (DEBUG ? "\n" : "") << "[asm] fatal: could not assemble function '" << name << "' (" << e.what() << ')' << endl;
            exit(1);
        }

        vector<unsigned> jumps = func.jumps();
        vector<unsigned> jumps_absolute = func.jumpsAbsolute();

        vector<tuple<int, int> > local_jumps;
        for (unsigned i = 0; i < jumps.size(); ++i) {
            unsigned jmp = jumps[i];
            local_jumps.push_back(tuple<int, int>(jmp, functions_section_size));
        }
        func.calculateJumps(local_jumps);

        byte* btcode = func.bytecode();

        // store generated bytecode fragment for future use (we must not yet write it to the file to conform to bytecode format)
        functions_bytecode[name] = tuple<int, byte*>(func.size(), btcode);

        // extend jump table with jumps from current function
        for (unsigned i = 0; i < jumps.size(); ++i) {
            unsigned jmp = jumps[i];
            if (DEBUG) {
                cout << "[asm] debug: pushed relative jump to jump table: " << jmp << '+' << functions_section_size << endl;
            }
            jump_table.push_back(jmp+functions_section_size);
        }

        for (unsigned i = 0; i < jumps_absolute.size(); ++i) {
            if (DEBUG) {
                cout << "[asm] debug: pushed absolute jump to jump table: " << jumps_absolute[i] << "+0" << endl;
            }
            jump_positions.push_back(tuple<int, int>(jumps_absolute[i]+functions_section_size, 0));
        }

        functions_section_size += func.size();
    }


    //////////////////////////
    // IF ASSEMBLING A LIBRARY
    // WRITE OUT JUMP TABLE
    if (flags.as_lib) {
        if (DEBUG) {
            cout << "debug: jump table has " << jump_table.size() << " entries" << endl;
        }
        unsigned total_jumps = jump_table.size();
        out.write((const char*)&total_jumps, sizeof(unsigned));

        unsigned jmp;
        for (unsigned i = 0; i < total_jumps; ++i) {
            jmp = jump_table[i];
            out.write((const char*)&jmp, sizeof(unsigned));
        }
    } else {
        if (DEBUG) {
            cout << "debug: skipping jump table write (not a library)" << endl;
        }
    }


    ///////////////////////////////////////////////
    // CHECK IF THE FUNCTION SET AS MAIN IS DEFINED
    // AS ALL THE FUNCTIONS (LOCAL OR LINKED) ARE
    // NOW AVAILABLE
    if (find(functions.names.begin(), functions.names.end(), main_function) == functions.names.end() and not flags.as_lib) {
        cout << "[asm:pre] fatal: main function is undefined: " << main_function << endl;
        return 1;
    }


    ////////////////////////////
    // PREPARE BLOCK IDS SECTION
    uint16_t block_ids_section_size = 0;
    for (string name : blocks.names) { block_ids_section_size += name.size(); }
    // we need to insert address (uint16_t) after every block
    block_ids_section_size += sizeof(uint16_t) * blocks.names.size();
    // for null characters after block names
    block_ids_section_size += blocks.names.size();

    /////////////////////////////////////////////
    // WRITE OUT BLOCK IDS SECTION
    // THIS ALSO INCLUDES IDS OF LINKED blocks.bodies
    out.write((const char*)&block_ids_section_size, sizeof(uint16_t));
    uint16_t block_bodies_size_so_far = 0;
    for (string name : blocks.names) {
        if (DEBUG) {
            cout << "[asm:write] writing block '" << name << "' to block address table";
        }
        if (find(linked_block_names.begin(), linked_block_names.end(), name) != linked_block_names.end()) {
            if (DEBUG) {
                cout << ": delayed" << endl;
            }
            continue;
        }
        if (DEBUG) {
            cout << endl;
        }

        // block name...
        out.write((const char*)name.c_str(), name.size());
        // ...requires terminating null character
        out.put('\0');
        // mapped address must come after name
        out.write((const char*)&block_bodies_size_so_far, sizeof(uint16_t));
        // blocks.bodies size must be incremented by the actual size of block's bytecode size
        // to give correct offset for next block
        try {
            block_bodies_size_so_far += Program::countBytes(blocks.bodies.at(name));
        } catch (const std::out_of_range& e) {
            cout << "fatal: could not find block '" << name << "' during address table write" << endl;
            exit(1);
        }
    }


    ///////////////////////////////
    // PREPARE FUNCTION IDS SECTION
    uint16_t function_ids_section_size = 0;
    for (string name : functions.names) { function_ids_section_size += name.size(); }
    // we need to insert address (uint16_t) after every function
    function_ids_section_size += sizeof(uint16_t) * functions.names.size();
    // for null characters after function names
    function_ids_section_size += functions.names.size();


    /////////////////////////////////////////////
    // WRITE OUT FUNCTION IDS SECTION
    // THIS ALSO INCLUDES IDS OF LINKED FUNCTIONS
    out.write((const char*)&function_ids_section_size, sizeof(uint16_t));
    uint16_t functions_size_so_far = block_bodies_size_so_far;
    if (DEBUG) {
        cout << "[asm:write] function addresses are offset by " << functions_size_so_far << " bytes (size of the block address table)" << endl;
    }
    for (string name : functions.names) {
        if (DEBUG) {
            cout << "[asm:write] writing function '" << name << "' to function address table";
        }
        if (find(linked_function_names.begin(), linked_function_names.end(), name) != linked_function_names.end()) {
            if (DEBUG) {
                cout << ": delayed" << endl;
            }
            continue;
        }
        if (DEBUG) {
            cout << endl;
        }

        // function name...
        out.write((const char*)name.c_str(), name.size());
        // ...requires terminating null character
        out.put('\0');
        // mapped address must come after name
        out.write((const char*)&functions_size_so_far, sizeof(uint16_t));
        // functions size must be incremented by the actual size of function's bytecode size
        // to give correct offset for next function
        try {
            functions_size_so_far += Program::countBytes(functions.bodies.at(name));
        } catch (const std::out_of_range& e) {
            cout << "fatal: could not find function '" << name << "' during address table write" << endl;
            exit(1);
        }
    }
    // FIXME: iteration over linked functions to put them to the address table
    //        should be done in the loop above (for local functions)
    for (string name : linked_function_names) {
        // function name...
        out.write((const char*)name.c_str(), name.size());
        // ...requires terminating null character
        out.put('\0');
        // mapped address must come after name
        uint16_t address = function_addresses[name];
        out.write((const char*)&address, sizeof(uint16_t));
    }


    //////////////////////
    // WRITE BYTECODE SIZE
    out.write((const char*)&bytes, 16);

    byte* program_bytecode = new byte[bytes];
    int program_bytecode_used = 0;

    ////////////////////////////////////////////////////
    // WRITE BYTECODE OF LOCAL BLOCKS TO BYTECODE BUFFER
    for (string name : blocks.names) {
        // linked blocks.bodies are to be inserted later
        if (find(linked_block_names.begin(), linked_block_names.end(), name) != linked_block_names.end()) { continue; }

        if (DEBUG) {
            cout << "[asm] pushing bytecode of local block '" << name << "' to final byte array" << endl;
        }
        int fun_size = 0;
        byte* fun_bytecode = 0;
        tie(fun_size, fun_bytecode) = block_bodies_bytecode[name];

        for (int i = 0; i < fun_size; ++i) {
            program_bytecode[program_bytecode_used+i] = fun_bytecode[i];
        }
        program_bytecode_used += fun_size;
    }


    ///////////////////////////////////////////////////////
    // WRITE BYTECODE OF LOCAL FUNCTIONS TO BYTECODE BUFFER
    for (string name : functions.names) {
        // linked functions are to be inserted later
        if (find(linked_function_names.begin(), linked_function_names.end(), name) != linked_function_names.end()) { continue; }

        if (DEBUG) {
            cout << "[asm] pushing bytecode of local function '" << name << "' to final byte array" << endl;
        }
        int fun_size = 0;
        byte* fun_bytecode = 0;
        tie(fun_size, fun_bytecode) = functions_bytecode[name];

        for (int i = 0; i < fun_size; ++i) {
            program_bytecode[program_bytecode_used+i] = fun_bytecode[i];
        }
        program_bytecode_used += fun_size;
    }

    // free memory allocated for bytecode of local functions
    for (pair<string, tuple<int, byte*>> fun : functions_bytecode) {
        delete[] get<1>(fun.second);
    }

    Program calculator(bytes);
    calculator.setdebug(DEBUG).setscream(SCREAM);
    if (DEBUG) {
        cout << "[asm:post] calculating absolute jumps..." << endl;
    }
    calculator.fill(program_bytecode).calculateJumps(jump_positions);


    ////////////////////////////////////
    // WRITE STATICALLY LINKED LIBRARIES
    uint16_t bytes_offset = current_link_offset;
    for (tuple<string, uint16_t, char*> lnk : linked_libs_bytecode) {
        string lib_name;
        byte* linked_bytecode;
        uint16_t linked_size;
        tie(lib_name, linked_size, linked_bytecode) = lnk;

        if (VERBOSE or DEBUG) {
            cout << "[linker] message: linked module \"" << lib_name <<  "\" written at offset " << bytes_offset << endl;
        }

        vector<unsigned> linked_jumptable;
        try {
            linked_jumptable = linked_libs_jumptables[lib_name];
        } catch (const std::out_of_range& e) {
            cout << "[linker] fatal: could not find jumptable for '" << lib_name << "' (maybe not loaded?)" << endl;
            exit(1);
        }

        unsigned jmp, jmp_target;
        for (unsigned i = 0; i < linked_jumptable.size(); ++i) {
            jmp = linked_jumptable[i];
            jmp_target = *((unsigned*)(linked_bytecode+jmp));
            if (DEBUG) {
                cout << "[linker] adjusting jump: at position " << jmp << ", " << jmp_target << '+' << bytes_offset << " -> " << (jmp_target+bytes_offset) << endl;
            }
            *((int*)(linked_bytecode+jmp)) += bytes_offset;
        }

        for (int i = 0; i < linked_size; ++i) {
            program_bytecode[program_bytecode_used+i] = linked_bytecode[i];
        }
        program_bytecode_used += linked_size;
    }

    out.write((const char*)program_bytecode, bytes);
    out.close();

    return 0;
}
