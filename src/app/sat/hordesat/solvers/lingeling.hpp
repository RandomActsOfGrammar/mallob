/*
 * Lingeling.h
 *
 *  Created on: Nov 11, 2014
 *      Author: balyo
 */

#ifndef LINGELING_H_
#define LINGELING_H_

#include "portfolio_solver_interface.hpp"
#include "util/sys/threading.hpp"
#include "app/sat/hordesat/utilities/logging_interface.hpp"

#include "util/ringbuffer.hpp"

#include <map>

struct LGL;

#define LGL_UNIT_BUFFER_MAX_SIZE 5000
#define LGL_CLS_BUFFER_MAX_SIZE 10000

class Lingeling : public PortfolioSolverInterface {

private:
	LGL* solver;
	std::string name;
	int stopSolver;
	LearnedClauseCallback* callback;
	Mutex clauseAddMutex;
	int maxvar;
	double lastTermCallbackTime;
    
	// Friends: Callbacks for Lingeling and logging inside these callbacks
	friend int cbCheckTerminate(void* solverPtr);
	friend void cbProduce(void* sp, int* cls, int glue);
	friend void cbProduceUnit(void* sp, int lit);
	friend void cbConsumeUnits(void* sp, int** start, int** end);
	friend void cbConsumeCls(void* sp, int** clause, int* glue);
	friend void slog(PortfolioSolverInterface* slv, int verbosityLevel, const char* fmt, ...);

	// clause addition
	vector<vector<int> > clausesToAdd;
	vector<int> assumptions;

	RingBuffer<std::vector<int>> learnedClauses;
	RingBuffer<int> learnedUnits;
	
	int* unitsBuffer;
	size_t unitsBufferSize;
	int* clsBuffer;
	size_t clsBufferSize;
    
    volatile bool suspendSolver;
    Mutex suspendMutex;
    ConditionVariable suspendCond;

	unsigned int numDiversifications;
	unsigned int glueLimit;
	unsigned int sizeLimit;

public:
	Lingeling(const SolverSetup& setup);
	 ~Lingeling() override;

	// Add a (list of) permanent clause(s) to the formula
	void addLiteral(int lit) override;

	void diversify(int seed) override;
	void setPhase(const int var, const bool phase) override;

	// Solve the formula with a given set of assumptions
	SatResult solve(const vector<int>& assumptions) override;

	void setSolverInterrupt() override;
	void unsetSolverInterrupt() override;
    void setSolverSuspend() override;
    void unsetSolverSuspend() override;

	vector<int> getSolution() override;
	set<int> getFailedAssumptions() override;

	// Add a learned clause to the formula
	// The learned clauses might be added later or possibly never
	void addLearnedClause(const int* begin, int size) override;

	// Set a function that should be called for each learned clause
	void setLearnedClauseCallback(LearnedClauseCallback* callback) override;

	// Request the solver to produce more clauses
	void increaseClauseProduction() override;
	
	// Get the number of variables of the formula
	int getVariablesCount() override;

	int getNumOriginalDiversifications() override;
	
	// Get a variable suitable for search splitting
	int getSplittingVariable() override;

	// Get solver statistics
	SolvingStatistics getStatistics() override;

    
};

#endif /* LINGELING_H_ */
