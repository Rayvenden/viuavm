#include <sstream>
#include <viua/support/env.h>
using namespace std;

namespace support {
    namespace env {
        string getvar(const string& var) {
            const char* VAR = getenv(var.c_str());
            return (VAR == nullptr ? string("") : string(VAR));
        }
        vector<string> getpaths(const string& var) {
            string PATH = getvar(var);
            vector<string> paths;

            string path;
            unsigned i = 0;
            while (i < PATH.size()) {
                if (PATH[i] == ':') {
                    if (path.size()) {
                        paths.push_back(path);
                        path = "";
                        ++i;
                    }
                }
                path += PATH[i];
                ++i;
            }
            if (path.size()) {
                paths.push_back(path);
            }

            return paths;
        }

        bool isfile(const string& path) {
            struct stat sf;

            // not a file if stat returned error
            if (stat(path.c_str(), &sf) == -1) return false;
            // not a file if S_ISREG() macro returned false
            if (not S_ISREG(sf.st_mode)) return false;

            // file otherwise
            return true;
        }

        namespace viua {
            string getmodpath(const string& module, const string& extension, const vector<string>& paths) {
                string path = "";
                bool found = false;

                ostringstream oss;
                for (unsigned i = 0; i < paths.size(); ++i) {
                    oss.str("");
                    oss << paths[i] << '/' << module << '.' << extension;
                    path = oss.str();
                    if (path[0] == '~') {
                        oss.str("");
                        oss << getenv("HOME") << path.substr(1);
                        path = oss.str();
                    }

                    if ((found = support::env::isfile(path))) break;
                }

                return (found ? path : "");
            }
        }
    }
}
