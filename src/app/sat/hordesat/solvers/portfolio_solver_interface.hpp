/*
 * portfolioSolverInterface.h
 *
 *  Created on: Oct 10, 2014
 *      Author: balyo
 * 
 * portfolio_solver_interface.hpp
 * 
 *  Modified by Dominik Schreiber, 2019-*
 */

#ifndef PORTFOLIOSOLVERINTERFACE_H_
#define PORTFOLIOSOLVERINTERFACE_H_

#include <vector>
#include <set>
#include <stdexcept>

#include "app/sat/hordesat/utilities/logging_interface.hpp"

using namespace std;

enum SatResult {
	SAT = 10,
	UNSAT = 20,
	UNKNOWN = 0
};

struct SolvingStatistics {
	SolvingStatistics():propagations(0),decisions(0),conflicts(0),restarts(0),memPeak(0) {}
	unsigned long propagations;
	unsigned long decisions;
	unsigned long conflicts;
	unsigned long restarts;
	double memPeak;
};

class LearnedClauseCallback {
public:
	virtual void processClause(vector<int>& cls, int solverId) = 0;
	virtual ~LearnedClauseCallback() {};
};

void updateTimer(std::string jobName);

/**
 * Interface for solvers that can be used in the portfolio.
 */
class PortfolioSolverInterface {

protected:
	LoggingInterface& _logger;


// ************** INTERFACE TO IMPLEMENT **************

public:

	// constructor
	PortfolioSolverInterface(LoggingInterface& logger, int globalId, int localId, std::string jobname);

    // destructor
	virtual ~PortfolioSolverInterface() {}

	// Get the number of variables of the formula
	virtual int getVariablesCount() = 0;

	// Get a variable suitable for search splitting
	virtual int getSplittingVariable() = 0;

	// Set initial phase for a given variable
	// Used only for diversification of the portfolio
	virtual void setPhase(const int var, const bool phase) = 0;

	// Solve the formula with a given set of assumptions
	virtual SatResult solve(const vector<int>& assumptions = vector<int>()) = 0;

	// Get a solution vector containing lit or -lit for each lit in the model
	virtual vector<int> getSolution() = 0;

	// Get a set of failed assumptions
	virtual set<int> getFailedAssumptions() = 0;

	// Add a permanent literal to the formula (zero for clause separator)
	virtual void addLiteral(int lit) = 0;

	// Add a learned clause to the formula
	// The learned clauses might be added later or possibly never
	virtual void addLearnedClause(const int* begin, int size) = 0;

	// Set a function that should be called for each learned clause
	virtual void setLearnedClauseCallback(LearnedClauseCallback* callback) = 0;

	// Request the solver to produce more clauses
	virtual void increaseClauseProduction() = 0;

	// Get solver statistics
	virtual SolvingStatistics getStatistics() = 0;

	// You are solver #rank of #size solvers, diversify your parameters (seeds, heuristics, etc.) accordingly.
	virtual void diversify(int rank, int size) = 0;

	// How many "true" different diversifications do you have?
	// May be used to decide when to apply additional diversifications.
	virtual int getNumOriginalDiversifications() = 0;

protected:
	// Interrupt the SAT solving, solving cannot continue until interrupt is unset.
	virtual void setSolverInterrupt() = 0;

	// Resume SAT solving after it was interrupted.
	virtual void unsetSolverInterrupt() = 0;

    // Suspend the SAT solver DURING its execution (ASYNCHRONOUSLY), 
	// temporarily freeing up CPU for other threads
    virtual void setSolverSuspend() = 0;

	// Resume SAT solving after it was suspended.
    virtual void unsetSolverSuspend() = 0;

// ************** END OF INTERFACE TO IMPLEMENT **************


// Other methods

public:
	int getGlobalId() {return _global_id;}
	int getLocalId() {return _local_id;}
	
	void interrupt();
	void uninterrupt();
	void suspend();
	void resume();

	// Friend function implemented in .cpp
	friend void slog(PortfolioSolverInterface* slv, int verbosityLevel, const char* fmt, ...);

private:
	std::string _global_name;
	std::string _job_name;
	int _global_id;
	int _local_id;
};

// Returns the elapsed time (seconds) since the currently registered solver's start time.
double getTime();

void slog(PortfolioSolverInterface* slv, int verbosityLevel, const char* fmt, ...);

#endif /* PORTFOLIOSOLVERINTERFACE_H_ */
