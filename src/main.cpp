
#include <iostream>
#include <set>
#include <stdlib.h>
#include <unistd.h>

#include "comm/mympi.hpp"
#include "util/sys/timer.hpp"
#include "util/logger.hpp"
#include "util/random.hpp"
#include "util/params.hpp"
#include "util/sys/shared_memory.hpp"
#include "util/sys/process.hpp"
#include "util/sys/proc.hpp"
#include "worker.hpp"
#include "client.hpp"

#ifndef MALLOB_VERSION
#define MALLOB_VERSION "(dbg)"
#endif


void doExternalClientProgram(MPI_Comm& commClients, Parameters& params, const std::set<int>& clientRanks) {
    
    Client client(commClients, params, clientRanks);
    client.init();
    client.mainProgram();
}

void doWorkerNodeProgram(MPI_Comm& commWorkers, Parameters& params, const std::set<int>& clientRanks) {

    Worker worker(commWorkers, params, clientRanks);
    worker.init();
    worker.mainProgram();
}

int main(int argc, char *argv[]) {
    
    Timer::init();
    MyMpi::init(argc, argv);

    int numNodes = MyMpi::size(MPI_COMM_WORLD);
    int rank = MyMpi::rank(MPI_COMM_WORLD);

    Parameters params;
    params.init(argc, argv);
    Logger::init(rank, params.getIntParam("v"), params.isNotNull("colors"), 
            /*quiet=*/params.isNotNull("q"), /*cPrefix=*/params.isNotNull("mono"), params.getParam("log"));
    
    MyMpi::setOptions(params);

    if (rank == 0)
        params.printParams();
    if (params.isSet("h") || params.isSet("help")) {
        // Help requested or no job input provided
        if (rank == 0) {
            params.printUsage();
        }
        MPI_Finalize();
        exit(0);
    }

    char hostname[1024];
	gethostname(hostname, 1024);
    log(V3_VERB, "mallob %s pid=%lu on host %s\n", MALLOB_VERSION, Proc::getPid(), hostname);

    // Global and local seed, such that all nodes have access to a synchronized randomness
    // as well as to an individual randomness that differs among nodes
    Random::init(numNodes, rank);

    // Initialize bookkeeping of child processes
    Process::init(rank);

    // Find client ranks
    std::set<int> externalClientRanks;
    int numClients = params.getIntParam("c");
    int numWorkers = numNodes - numClients;
    assert(numWorkers > 0 || log_return_false("Need at least one worker node!"));
    for (int i = 1; i <= numClients; i++)
        externalClientRanks.insert(numNodes-i);
    bool isExternalClient = rank >= numWorkers;

    // Create two disjunct communicators: Clients and workers
    int color = -1;
    if (isExternalClient) {
        // External client node
        color = 1;
    } else {
        // Idle worker node
        color = 2;
    }
    MPI_Comm newComm;
    MPI_Comm_split(MPI_COMM_WORLD, color, rank, &newComm);

    //std::set_terminate(Logger::forceFlush);

    try {
        // Launch node's main program
        if (isExternalClient) {
            doExternalClientProgram(newComm, params, externalClientRanks);
        } else {
            doWorkerNodeProgram(newComm, params, externalClientRanks);
        }
    } catch (const std::exception &ex) {
        log(V0_CRIT, "Unexpected ERROR: \"%s\" - aborting\n", ex.what());
        Logger::getMainInstance().flush();
        exit(1);
    } catch (...) {
        log(V0_CRIT, "Unexpected ERROR - aborting\n");
        Logger::getMainInstance().flush();
        exit(1);
    }

    MPI_Finalize();
    log(V2_INFO, "Exiting happily\n");
    Logger::getMainInstance().flush();
}
