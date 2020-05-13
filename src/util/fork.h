
#ifndef DOMPASCH_MALLOB_FORK_H
#define DOMPASCH_MALLOB_FORK_H

#include <set>

class Fork {

public:
    static std::set<int> _children;

    static void init();
    static int createChild();
    static void terminate(int childpid);
    static void suspend(int childpid);
    static void resume(int childpid);

};

#endif