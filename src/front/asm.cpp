#include <iostream>
#include <fstream>
#include <viua/support/string.h>
#include <viua/support/env.h>
#include <viua/version.h>
#include <viua/cg/assembler/assembler.h>
#include <viua/front/asm.h>
using namespace std;


// MISC FLAGS
bool SHOW_HELP = false;
bool SHOW_VERSION = false;

// are we assembling a library?
bool AS_LIB = false;

// are we just expanding the source to simple form?
bool EXPAND_ONLY = false;
// are we only verifying source code correctness?
bool EARLY_VERIFICATION_ONLY = false;

bool VERBOSE = false;
bool DEBUG = false;
bool SCREAM = false;

bool WARNING_ALL = false;
bool ERROR_ALL = false;

// WARNINGS
bool WARNING_MISSING_END = false;

// ERRORS
bool ERROR_MISSING_END = false;
bool ERROR_HALT_IS_LAST = false;


bool usage(const char* program, bool SHOW_HELP, bool SHOW_VERSION, bool VERBOSE) {
    if (SHOW_HELP or (SHOW_VERSION and VERBOSE)) {
        cout << "Viua VM assembler, version ";
    }
    if (SHOW_HELP or SHOW_VERSION) {
        cout << VERSION << '.' << MICRO << ' ' << COMMIT << endl;
    }
    if (SHOW_HELP) {
        cout << "\nUSAGE:\n";
        cout << "    " << program << " [option...] [-o <outfile>] <infile> [<linked-file>...]\n" << endl;
        cout << "OPTIONS:\n";
        cout << "    " << "-V, --version            - show version\n"
             << "    " << "-h, --help               - display this message\n"
             << "    " << "-v, --verbose            - show verbose output\n"
             << "    " << "-d, --debug              - show debugging output\n"
             << "    " << "    --scream             - show so much debugging output it becomes noisy\n"
             << "    " << "-W, --Wall               - warn about everything\n"
             << "    " << "    --Wmissin-end        - warn about missing 'end' instruction at the end of functions\n"
             << "    " << "    --Eall               - treat all warnings as errors\n"
             << "    " << "    --Emissing-end       - treat missing 'end' instruction at the end of function as error\n"
             << "    " << "    --Ehalt-is-last      - treat 'halt' being used as last instruction of 'main' function as error\n"
             << "    " << "-c, --lib                - assemble as a library\n"
             << "    " << "-E, --expand             - only expand the source code to simple form (one instruction per line)\n"
             << "    " << "                           with this option, assembler prints expanded source to standard output\n"
             << "    " << "-C, --verify             - verify source code correctness without actually compiling it\n"
             << "    " << "                         - verify source code correctness without actually compiling it\n"
             << "    " << "                           this option turns assembler into source level debugger and static code analyzer hybrid\n"
             ;
    }

    return (SHOW_HELP or SHOW_VERSION);
}

int main(int argc, char* argv[]) {
    // setup command line arguments vector
    vector<string> args;
    string option;

    string filename(""), compilename("");

    for (int i = 1; i < argc; ++i) {
        option = string(argv[i]);
        if (option == "--help" or option == "-h") {
            SHOW_HELP = true;
            continue;
        } else if (option == "--version" or option == "-V") {
            SHOW_VERSION = true;
            continue;
        } else if (option == "--verbose" or option == "-v") {
            VERBOSE = true;
            continue;
        } else if (option == "--debug" or option == "-d") {
            DEBUG = true;
            continue;
        } else if (option == "--scream") {
            SCREAM = true;
            continue;
        } else if (option == "--lib" or option == "-c") {
            AS_LIB = true;
            continue;
        } else if (option == "--Wall" or option == "-W") {
            WARNING_ALL = true;
            continue;
        } else if (option == "--Eall") {
            ERROR_ALL = true;
            continue;
        } else if (option == "--Wmissing-end") {
            WARNING_MISSING_END = true;
            continue;
        } else if (option == "--Emissing-end") {
            ERROR_MISSING_END = true;
            continue;
        } else if (option == "--Emissing-end") {
            ERROR_MISSING_END = true;
            continue;
        } else if (option == "--Ehalt-is-last") {
            ERROR_HALT_IS_LAST = true;
            continue;
        } else if (option == "--out" or option == "-o") {
            if (i < argc-1) {
                compilename = string(argv[++i]);
            } else {
                cout << "error: option '" << argv[i] << "' requires an argument: filename" << endl;
                exit(1);
            }
            continue;
        } else if (option == "--expand" or option == "-E") {
            EXPAND_ONLY = true;
            continue;
        } else if (option == "--verify" or option == "-C") {
            EARLY_VERIFICATION_ONLY = true;
            continue;
        }
        args.push_back(argv[i]);
    }

    if (usage(argv[0], SHOW_HELP, SHOW_VERSION, VERBOSE)) { return 0; }

    if (args.size() == 0) {
        cout << "fatal: no input file" << endl;
        return 1;
    }

    ////////////////////////////////
    // FIND FILENAME AND COMPILENAME
    filename = args[0];
    if (!filename.size()) {
        cout << "fatal: no file to assemble" << endl;
        return 1;
    }
    if (!support::env::isfile(filename)) {
        cout << "fatal: could not open file: " << filename << endl;
        return 1;
    }

    if (compilename == "") {
        if (AS_LIB) {
            compilename = (filename + ".wlib");
        } else {
            compilename = "a.out";
        }
    }

    if (VERBOSE or DEBUG) {
        cout << "message: assembling \"" << filename << "\" to \"" << compilename << "\"" << endl;
    }


    //////////////////////////////////////////
    // GATHER LINKS OBTAINED FROM COMMAND LINE
    vector<string> commandline_given_links;
    for (unsigned i = 1; i < args.size(); ++i) {
        commandline_given_links.push_back(args[i]);
    }


    ////////////////
    // READ LINES IN
    ifstream in(filename, ios::in | ios::binary);
    if (!in) {
        cout << "fatal: file could not be opened: " << filename << endl;
        return 1;
    }

    vector<string> lines;
    string line;
    while (getline(in, line)) { lines.push_back(line); }

    map<unsigned, unsigned> expanded_lines_to_source_lines;
    vector<string> expanded_lines = expandSource(lines, expanded_lines_to_source_lines);
    if (EXPAND_ONLY) {
        for (unsigned i = 0; i < expanded_lines.size(); ++i) {
            cout << expanded_lines[i] << endl;
        }
        return 0;
    }

    vector<string> ilines = assembler::ce::getilines(expanded_lines);
    invocables_t functions;
    if (gatherFunctions(&functions, expanded_lines, ilines)) {
        return 1;
    }
    invocables_t blocks;
    if (gatherBlocks(&blocks, expanded_lines, ilines)) {
        return 1;
    }

    ///////////////////////////////////////////
    // INITIAL VERIFICATION OF CODE CORRECTNESS
    string report;
    if ((report = assembler::verify::directives(expanded_lines, expanded_lines_to_source_lines)).size()) {
        cout << report << endl;
        return 1;
    }
    if ((report = assembler::verify::instructions(expanded_lines, expanded_lines_to_source_lines)).size()) {
        cout << report << endl;
        return 1;
    }
    if ((report = assembler::verify::ressInstructions(expanded_lines, expanded_lines_to_source_lines, AS_LIB)).size()) {
        cout << report << endl;
        return 1;
    }
    if ((report = assembler::verify::functionBodiesAreNonempty(expanded_lines, functions.bodies)).size()) {
        cout << report << endl;
        return 1;
    }
    if ((report = assembler::verify::blockTries(expanded_lines, expanded_lines_to_source_lines, blocks.names, blocks.signatures)).size()) {
        cout << report << endl;
        return 1;
    }
    if ((report = assembler::verify::frameBalance(expanded_lines, expanded_lines_to_source_lines)).size()) {
        cout << report << endl;
        exit(1);
    }
    if ((not AS_LIB) and (ERROR_HALT_IS_LAST or ERROR_ALL) and functions.bodies.count("main")) {
        if ((report = assembler::verify::mainFunctionDoesNotEndWithHalt(functions.bodies)).size()) {
            cout << report << endl;
            exit(1);
        }
    }

    ////////////////////////////
    // VERIFY FRAME INSTRUCTIONS
    for (unsigned i = 0; i < expanded_lines.size(); ++i) {
        line = str::lstrip(expanded_lines[i]);
        if (not str::startswith(line, "frame")) {
            continue;
        }

        line = str::lstrip(str::sub(line, str::chunk(line).size()));

        if (line.size() == 0) {
            cout << "fatal: frame instruction without operands at line " << i << " in " << filename;
            return 1;
        }
    }

    /////////////////////////
    // VERIFY FUNCTION BODIES
    for (auto function : functions.bodies) {
        vector<string> flines = function.second;
        if ((flines.size() == 0 or flines.back() != "end") and (function.first != "main" and flines.back() != "halt")) {
            if (ERROR_MISSING_END or ERROR_ALL) {
                cout << "fatal: missing 'end' at the end of function '" << function.first << "'" << endl;
                exit(1);
            } else if (WARNING_MISSING_END or WARNING_ALL) {
                cout << "warning: missing 'end' at the end of function '" << function.first << "'" << endl;
            }
        }
    }

    //////////////////////
    // VERIFY BLOCK BODIES
    for (auto block : blocks.bodies) {
        vector<string> flines = block.second;
        if (flines.size() == 0) {
            cout << "fatal: block '" << block.first << "' has empty body" << endl;
            exit(1);
        }
        string last_line = flines.back();
        if (not (last_line == "leave" or last_line == "end" or last_line == "halt")) {
            cout << "fatal: missing returning instruction ('leave', 'end' or 'halt') at the end of block '" << block.first << "'" << endl;
            exit(1);
        }
    }


    if (EARLY_VERIFICATION_ONLY) {
        return 0;
    }

    compilationflags_t flags;
    flags.as_lib = AS_LIB;
    flags.verbose = VERBOSE;
    flags.debug = DEBUG;
    flags.scream = SCREAM;

    int ret_code = 0;
    try {
        ret_code = generate(expanded_lines, expanded_lines_to_source_lines, ilines, functions, blocks, filename, compilename, commandline_given_links, flags);
    } catch (const string& e) {
        ret_code = 1;
        cout << "fatal: exception occured during assembling: " << e << endl;
    }

    return ret_code;
}
