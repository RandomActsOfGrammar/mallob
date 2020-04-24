/*
 * HordeLib.h
 *
 *  Created on: Mar 24, 2017
 *      Author: balyo
 */

#ifndef HORDELIB_H_
#define HORDELIB_H_

#include "utilities/ParameterProcessor.h"
#include "utilities/Threading.h"
#include "utilities/logging_interface.h"
#include "solvers/MiniSat.h"
#include "solvers/Lingeling.h"
#include "sharing/AllToAllSharingManager.h"
#include "solvers/solver_thread.h"
#include "solvers/solving_state.h"

#include <thread>
#include <vector>
#include <memory>
#include <set>
#include <map>

using namespace std;

class HordeLib {
private:
	int mpi_size;
	int mpi_rank;
	std::vector<std::thread> solverThreads;
	size_t sleepInt;
	int solversCount;
	SharingManagerInterface* sharingManager;
	
	volatile SolvingStates::SolvingState solvingState;
	
	vector<PortfolioSolverInterface*> solvers;
	vector<bool> solverThreadsInitialized;
	vector<long> solverTids;

	std::vector<std::shared_ptr<std::vector<int>>> formulae;
	std::shared_ptr<vector<int>> assumptions;

	SatResult finalResult;
	vector<int> truthValues;
	set<int> failedAssumptions;

    double startSolving;
    int maxSeconds;
	size_t maxRounds;
	size_t round;

	std::shared_ptr<LoggingInterface> logger;

	Mutex solutionLock;
	Mutex solvingStateLock;
	ConditionVariable stateChangeCond;
	
	// settings
	ParameterProcessor params;

	bool cleanedUp = false;

public:
	friend class SolverThread;

	// methods
	HordeLib(int argc, char** argv);
    HordeLib(const std::map<std::string, std::string>& params, std::shared_ptr<LoggingInterface> loggingInterface = NULL);
	~HordeLib();

	ParameterProcessor& getParams() {return params;}

    void beginSolving(const std::vector<std::shared_ptr<std::vector<int>>>& formulae, 
							const std::shared_ptr<std::vector<int>>& assumptions);
	void continueSolving(const std::vector<std::shared_ptr<std::vector<int>>>& formulae, 
							const std::shared_ptr<std::vector<int>>& assumptions);
	void updateRole(int rank, int numNodes);
	bool isFullyInitialized();
    int solveLoop();

    std::vector<int> prepareSharing(int maxSize);
    void digestSharing(const std::vector<int>& result);

    int finishSolving();
    void interrupt();
	void setSolvingState(SolvingStates::SolvingState state);
    void setPaused();
    void unsetPaused();
	void abort();

	void dumpStats();
	std::vector<long> getSolverTids() {
		std::vector<long> tids;
		for (int i = 0; i < solverThreadsInitialized.size(); i++) {
			if (solverThreadsInitialized[i]) tids.push_back(solverTids[i]);
		}
		return tids;
	}

	int value(int lit);
	int failed(int lit);
	std::vector<int>& getTruthValues() {
		return truthValues;
	}
	std::set<int>& getFailedAssumptions() {
		return failedAssumptions;
	}

	void hlog(int verbosityLevel, const char* fmt, ...);

	void cleanUp();
	bool isCleanedUp() {return cleanedUp;}
	

private:
    void init();	
	std::vector<int> clauseBufferToPlainClauses(const std::vector<int>& buffer);
};

#endif /* HORDELIB_H_ */
