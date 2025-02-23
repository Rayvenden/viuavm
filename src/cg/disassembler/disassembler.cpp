#include <sstream>
#include <viua/bytecode/opcodes.h>
#include <viua/bytecode/maps.h>
#include <viua/support/string.h>
#include <viua/support/pointer.h>
#include <viua/cg/disassembler/disassembler.h>
using namespace std;


string disassembler::intop(byte* ptr) {
    ostringstream oss;

    oss << ((*(bool*)ptr) ? "@" : "");
    pointer::inc<bool, byte>(ptr);
    oss << *(int*)ptr;
    pointer::inc<int, byte>(ptr);

    return oss.str();
}

tuple<string, unsigned> disassembler::instruction(byte* ptr) {
    byte* bptr = ptr;

    OPCODE op = OPCODE(*bptr);
    string opname;
    try {
        opname = OP_NAMES.at(op);
    } catch (const std::out_of_range& e) {
        ostringstream emsg;
        emsg << "could not find name for opcode: " << op;
        throw emsg.str();
    }

    ++bptr; // instruction byte is not needed anymore

    ostringstream oss;
    oss << opname;
    if (in(OP_VARIABLE_LENGTH, op)) {
    }

    if (not in(OP_VARIABLE_LENGTH, op)) {
        bptr += (OP_SIZES.at(opname)-1); // -1 because OP_SIZES add one for instruction-storing byte
    } else if (op == STRSTORE) {
        oss << " " << intop(bptr);
        pointer::inc<bool, byte>(bptr);
        pointer::inc<int, byte>(bptr);

        string s = string(bptr);
        oss << " " << str::enquote(s);
        bptr += s.size();
        ++bptr; // for null character terminating the C-style string not included in std::string
    } else if ((op == CALL) or (op == CLOSURE) or (op == FUNCTION) or (op == CLASS) or (op == NEW) or (op == DERIVE) or (op == MSG)) {
        oss << " " << intop(bptr);
        pointer::inc<bool, byte>(bptr);
        pointer::inc<int, byte>(bptr);

        oss << " ";
        string fn_name = string(bptr);
        oss << fn_name;
        bptr += fn_name.size();
        ++bptr; // for null character terminating the C-style string not included in std::string
    } else if ((op == IMPORT) or (op == ENTER) or (op == LINK)) {
        oss << " ";
        string s = string(bptr);
        oss << (op == IMPORT ? str::enquote(s) : s);
        bptr += s.size();
        ++bptr; // for null character terminating the C-style string not included in std::string
    } else if (op == CATCH) {
        string s;

        oss << " ";
        s = string(bptr);
        oss << str::enquote(s);
        bptr += s.size();
        ++bptr; // for null character terminating the C-style string not included in std::string

        oss << " ";
        s = string(bptr);
        oss << s;
        bptr += s.size();
        ++bptr; // for null character terminating the C-style string not included in std::string
    } else if (op == ATTACH) {
        oss << " " << intop(bptr);
        pointer::inc<bool, byte>(bptr);
        pointer::inc<int, byte>(bptr);

        oss << " ";
        string fn_name = string(bptr);
        oss << fn_name;
        bptr += fn_name.size();
        ++bptr; // for null character terminating the C-style string not included in std::string

        oss << " ";
        string md_name = string(bptr);
        oss << md_name;
        bptr += md_name.size();
        ++bptr; // for null character terminating the C-style string not included in std::string
    }

    long increase = (bptr-ptr);
    if (increase < 0) {
        throw ("bytecode pointer increase less than zero: near " + OP_NAMES.at(op) + " instruction");
    }

    ++ptr;
    switch (op) {
        case IZERO:
        case IINC:
        case IDEC:
        case BINC:
        case BDEC:
        case PRINT:
        case ECHO:
        case BOOL:
        case NOT:
        case FREE:
        case EMPTY:
        case TMPRI:
        case TMPRO:
        case VEC:
        case CLBIND:
        case ARGC:
        case THROW:
        case PULL:
        case REGISTER:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            break;
        case ISTORE:
        case ITOF:
        case FTOI:
        case STOI:
        case STOF:
        case FRAME:
        case ARG:
        case PARAM:
        case PAREF:
        case MOVE:
        case COPY:
        case REF:
        case SWAP:
        case ISNULL:
        case VPUSH:
        case VLEN:
        case FCALL:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            break;
        case IADD:
        case ISUB:
        case IMUL:
        case IDIV:
        case ILT:
        case ILTE:
        case IGT:
        case IGTE:
        case IEQ:
        case FADD:
        case FSUB:
        case FMUL:
        case FDIV:
        case FLT:
        case FLTE:
        case FGT:
        case FGTE:
        case FEQ:
        case BADD:
        case BSUB:
        case BLT:
        case BLTE:
        case BGT:
        case BGTE:
        case BEQ:
        case STREQ:
        case AND:
        case OR:
        case VINSERT:
        case VPOP:
        case VAT:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            break;
        case JUMP:
            oss << " 0x";
            oss << hex;
            oss << *(int*)ptr;

            oss << dec;

            break;
        case BRANCH:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " 0x";
            oss << hex;
            oss << *(int*)ptr;
            pointer::inc<int, byte>(ptr);

            oss << " 0x";
            oss << hex;
            oss << *(int*)ptr;
            pointer::inc<int, byte>(ptr);

            oss << dec;

            break;
        case FSTORE:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " ";
            oss << *(float*)ptr;
            break;
        case BSTORE:
            oss << " " << intop(ptr);
            pointer::inc<bool, byte>(ptr);
            pointer::inc<int, byte>(ptr);

            oss << " ";
            pointer::inc<bool, byte>(ptr);
            oss << int(*ptr);
            break;
        case RESS:
            oss << " ";
            switch(int(*ptr)) {
                case 0:
                    oss << "global";
                    break;
                case 1:
                    oss << "local";
                    break;
                case 2:
                    oss << "static";
                    break;
                case 3:
                    oss << "temp";
                    break;
            }
            break;
        default:
            // if opcode was not covered here, it means it must have been a variable-length opcode
            oss << "";
    }

    // cast increase to unsigned as at this point it is safe to assume that it is greater than zero
    return tuple<string, unsigned>(oss.str(), unsigned(increase));
}
