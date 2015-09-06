#ifndef VIUA_CPU_H
#define VIUA_CPU_H

#pragma once

#include <dlfcn.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <viua/bytecode/bytetypedef.h>
#include <viua/types/type.h>
#include <viua/types/prototype.h>
#include <viua/cpu/registerset.h>
#include <viua/cpu/frame.h>
#include <viua/cpu/tryframe.h>
#include <viua/include/module.h>


const unsigned DEFAULT_REGISTER_SIZE = 256;
const unsigned MAX_STACK_SIZE = 8192;


class HaltException : public std::runtime_error {
    public:
        HaltException(): std::runtime_error("execution halted") {}
};


class CPU {
#ifdef AS_DEBUG_HEADER
    public:
#endif
    /*  Bytecode pointer is a pointer to program's code.
     *  Size and executable offset are metadata exported from bytecode dump.
     */
    byte* bytecode;
    uint16_t bytecode_size;
    uint16_t executable_offset;

    // Global register set
    RegisterSet* regset;
    // Currently used register set
    RegisterSet* uregset;

    // Temporary register
    Type* tmp;

    // Static registers
    std::map<std::string, RegisterSet*> static_registers;

    // Map of the typesystem currently existing inside the VM.
    std::map<std::string, Prototype*> typesystem;

    /*  Call stack.
     */
    std::vector<Frame*> frames;
    Frame* frame_new;

    /*  Block stack.
     */
    std::vector<TryFrame*> tryframes;
    TryFrame* try_frame_new;

    /*  Function and block names mapped to bytecode addresses.
     */
    byte* jump_base;
    std::map<std::string, unsigned> function_addresses;
    std::map<std::string, unsigned> block_addresses;

    std::map<std::string, std::pair<std::string, byte*>> linked_functions;
    std::map<std::string, std::pair<std::string, byte*>> linked_blocks;
    std::map<std::string, std::pair<unsigned, byte*> > linked_modules;

    /*  Slot for thrown objects (typically exceptions).
     *  Can be set by user code and the CPU.
     */
    Type* thrown;
    Type* caught;

    /*  Variables set after CPU executed bytecode.
     *  They describe exit conditions of the bytecode that just stopped running.
     */
    int return_code;                // always set
    std::string return_exception;   // set if CPU stopped because of an exception
    std::string return_message;     // message set by exception

    unsigned instruction_counter;
    byte* instruction_pointer;

    /*  This is the interface between programs compiled to VM bytecode and
     *  extension libraries written in C++.
     */
    std::map<std::string, ExternalFunction*> foreign_functions;

    /** This is the mapping Viua uses to dispatch methods on pure-C++ classes.
     */
    std::map<std::string, ForeignMethod> foreign_methods;

    /*  Methods to deal with registers.
     */
    void updaterefs(Type* before, Type* now);
    bool hasrefs(unsigned);
    Type* fetch(unsigned) const;
    void place(unsigned, Type*);
    void ensureStaticRegisters(std::string);

    /*  Methods dealing with stack and frame manipulation, and
     *  function calls.
     */
    Frame* requestNewFrame(int arguments_size = 0, int registers_size = 0);
    TryFrame* requestNewTryFrame();
    void pushFrame();
    void dropFrame();
    // call native (i.e. written in Viua) function
    byte* callNative(byte*, const std::string&, const bool&, const int&, const std::string&);
    // call foreign (i.e. from a C++ extension) function
    byte* callForeign(byte*, const std::string&, const bool&, const int&, const std::string&);
    // call foreign method (i.e. method of a pure-C++ class loaded into machine's typesystem)
    byte* callForeignMethod(byte*, Type*, const std::string&, const bool&, const int&, const std::string&);

    /*  Methods dealing with dynamic library loading.
     */
    std::vector<void*> cxx_dynamic_lib_handles;
    void loadNativeLibrary(const std::string&);
    void loadForeignLibrary(const std::string&);

    /*  Methods dealing with typesystem related tasks.
     */
    std::vector<std::string> inheritanceChainOf(const std::string&);

    /*  Methods implementing CPU instructions.
     */
    byte* izero(byte*);
    byte* istore(byte*);
    byte* iadd(byte*);
    byte* isub(byte*);
    byte* imul(byte*);
    byte* idiv(byte*);

    byte* ilt(byte*);
    byte* ilte(byte*);
    byte* igt(byte*);
    byte* igte(byte*);
    byte* ieq(byte*);

    byte* iinc(byte*);
    byte* idec(byte*);

    byte* fstore(byte*);
    byte* fadd(byte*);
    byte* fsub(byte*);
    byte* fmul(byte*);
    byte* fdiv(byte*);

    byte* flt(byte*);
    byte* flte(byte*);
    byte* fgt(byte*);
    byte* fgte(byte*);
    byte* feq(byte*);

    byte* bstore(byte*);

    byte* itof(byte*);
    byte* ftoi(byte*);
    byte* stoi(byte*);
    byte* stof(byte*);

    byte* strstore(byte*);

    byte* vec(byte*);
    byte* vinsert(byte*);
    byte* vpush(byte*);
    byte* vpop(byte*);
    byte* vat(byte*);
    byte* vlen(byte*);

    byte* boolean(byte*);
    byte* lognot(byte*);
    byte* logand(byte*);
    byte* logor(byte*);

    byte* move(byte*);
    byte* copy(byte*);
    byte* ref(byte*);
    byte* ptr(byte*);
    byte* deptr(byte*);
    byte* swap(byte*);
    byte* free(byte*);
    byte* empty(byte*);
    byte* isnull(byte*);

    byte* ress(byte*);
    byte* tmpri(byte*);
    byte* tmpro(byte*);

    byte* print(byte*);
    byte* echo(byte*);

    byte* clbind(byte*);
    byte* closure(byte*);

    byte* function(byte*);
    byte* fcall(byte*);

    byte* frame(byte*);
    byte* param(byte*);
    byte* paref(byte*);
    byte* arg(byte*);
    byte* argc(byte*);

    byte* call(byte*);
    byte* end(byte*);

    byte* jump(byte*);
    byte* branch(byte*);

    byte* tryframe(byte*);
    byte* vmcatch(byte*);
    byte* pull(byte*);
    byte* vmtry(byte*);
    byte* vmthrow(byte*);
    byte* leave(byte*);

    byte* vmclass(byte*);
    byte* prototype(byte*);
    byte* vmderive(byte*);
    byte* vmattach(byte*);
    byte* vmregister(byte*);

    byte* vmnew(byte*);
    byte* vmmsg(byte*);

    byte* import(byte*);
    byte* link(byte*);

    public:
        // debug and error reporting flags
        bool debug, errors;

        std::vector<std::string> commandline_arguments;

        /*  Public API of the CPU provides basic actions:
         *
         *      * load bytecode,
         *      * set its size,
         *      * tell the CPU where to start execution,
         *      * kick the CPU so it starts running,
         */
        CPU& load(byte*);
        CPU& bytes(uint16_t);
        CPU& eoffset(uint16_t);
        CPU& preload();

        CPU& mapfunction(const std::string&, unsigned);
        CPU& mapblock(const std::string&, unsigned);

        CPU& registerExternalFunction(const std::string&, ExternalFunction*);
        CPU& removeExternalFunction(std::string);

        /// These two methods are used to inject pure-C++ classes into machine's typesystem.
        CPU& registerForeignPrototype(const std::string&, Prototype*);
        CPU& registerForeignMethod(const std::string&, ForeignMethod);

        byte* begin();
        inline byte* end() { return 0; }

        CPU& iframe(Frame* frm = 0, unsigned r = DEFAULT_REGISTER_SIZE);

        byte* dispatch(byte*);
        byte* tick();

        int run();
        inline unsigned counter() { return instruction_counter; }

        inline std::tuple<int, std::string, std::string> exitcondition() {
            return std::tuple<int, std::string, std::string>(return_code, return_exception, return_message);
        }
        inline std::vector<Frame*> trace() { return frames; }

        CPU():
            bytecode(0), bytecode_size(0), executable_offset(0),
            regset(0), uregset(0),
            tmp(0),
            static_registers({}),
            frame_new(0),
            try_frame_new(0),
            jump_base(0),
            thrown(0), caught(0),
            return_code(0), return_exception(""), return_message(""),
            instruction_counter(0), instruction_pointer(0),
            debug(false), errors(false)
        {}

        ~CPU() {
            /*  Destructor frees memory at bytecode pointer so make sure you passed a copy of the bytecode to the constructor
             *  if you want to keep it around after the CPU is finished.
             */
            if (bytecode) { delete[] bytecode; }

            std::map<std::string, RegisterSet*>::iterator sr = static_registers.begin();
            while (sr != static_registers.end()) {
                std::string  rkey = sr->first;
                RegisterSet* rset = sr->second;

                ++sr;

                static_registers.erase(rkey);
                delete rset;
            }

            std::map<std::string, std::pair<unsigned, byte*> >::iterator lm = linked_modules.begin();
            while (lm != linked_modules.end()) {
                std::string lkey = lm->first;
                byte *ptr = lm->second.second;

                ++lm;

                linked_modules.erase(lkey);
                delete[] ptr;
            }

            std::map<std::string, Prototype*>::iterator pr = typesystem.begin();
            while (pr != typesystem.end()) {
                std::string proto_name = pr->first;
                Prototype* proto_ptr = pr->second;

                ++pr;

                typesystem.erase(proto_name);
                delete proto_ptr;
            }

            for (unsigned i = 0; i < cxx_dynamic_lib_handles.size(); ++i) {
                dlclose(cxx_dynamic_lib_handles[i]);
            }
        }
};

#endif
